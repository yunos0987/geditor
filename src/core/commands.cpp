#include "commands.h"

#include <algorithm>
#include <string>

#include "editor.h"
#include "layout.h"
#include "unicode.h"

namespace geditor {
namespace {

// ---- Helpers ----

WordClass class_at(const PieceTree& t, size_t p) {
  return word_class(code_point_at(t, p));
}

// Class of the word that a run of extenders ending before `p` belongs to: the
// class of the first non-extender before them, or kKatakana when there is no
// word character there.
WordClass class_before_extenders(const PieceTree& t, size_t p) {
  while (p > 0) {
    const WordClass c = class_at(t, prev_char_offset(t, p));
    if (c != WordClass::kExtender) return c == WordClass::kNone ? WordClass::kKatakana : c;
    p = prev_char_offset(t, p);
  }
  return WordClass::kKatakana;
}

size_t forward_word_end(const PieceTree& t, size_t p) {
  const size_t n = t.length();
  while (p < n && class_at(t, p) == WordClass::kNone) p = next_char_offset(t, p);
  if (p >= n) return n;
  WordClass cls = class_at(t, p);
  if (cls == WordClass::kExtender) cls = class_before_extenders(t, p);
  while (p < n) {
    const WordClass c = class_at(t, p);
    if (c != cls && c != WordClass::kExtender) break;
    p = next_char_offset(t, p);
  }
  return p;
}

size_t backward_word_start(const PieceTree& t, size_t p) {
  while (p > 0 && class_at(t, prev_char_offset(t, p)) == WordClass::kNone) p = prev_char_offset(t, p);
  if (p == 0) return 0;
  const WordClass cls = class_before_extenders(t, p);
  while (p > 0) {
    const WordClass c = class_at(t, prev_char_offset(t, p));
    if (c != cls && c != WordClass::kExtender) break;
    p = prev_char_offset(t, p);
  }
  return p;
}

bool region_active(const Buffer& b) {
  return b.mark_active && b.has_mark && b.mark != b.point;
}

void push_mark(Editor& ed, Buffer& b) {
  if (b.mark_active) return;
  b.has_mark = true;
  b.mark = b.point;
  ed.set_message(L"Mark set");
}

// Copies [from, to) to the clipboard; false (with a message) on failure.
bool copy_range(Editor& ed, const Buffer& b, size_t from, size_t to) {
  if (ed.host().set_clipboard(b.text.substr(from, to - from))) return true;
  ed.fail(L"Cannot write to the clipboard");
  return false;
}

// The prefix argument as a count and a direction: `n` steps forward, or
// backward when negative (decision D6). Capped so that loops stay sane.
int signed_count(const Editor& ed) {
  return std::clamp(ed.prefix_count(), -1000000000, 1000000000);
}

// Calls `step` |count| times (`back` for a negative count), stopping at the
// first step that returns false (the step shows its own message).
template <typename Step, typename Back>
void repeat_steps(Editor& ed, Step step, Back back) {
  const int n = signed_count(ed);
  const int times = n < 0 ? -n : n;
  for (int i = 0; i < times; ++i) {
    if (!(n < 0 ? back(ed) : step(ed))) return;
  }
}

// Count for commands that insert N copies; false (with a message) when the
// count is not positive.
// `unit_length`: characters inserted per copy; the whole insertion may not
// exceed kMaxRepeatedInsert characters (a huge C-u count would freeze the
// editor and exhaust memory).
bool insert_count(Editor& ed, int* count, size_t unit_length = 0) {
  *count = signed_count(ed);
  if (*count <= 0) {
    ed.fail(L"Repeat count must be positive");
    return false;
  }
  if (unit_length > 0 && static_cast<size_t>(*count) > kMaxRepeatedInsert / unit_length) {
    ed.fail(L"Repeat count too large");
    return false;
  }
  return true;
}

std::wstring repeated(std::wstring_view s, int count) {
  std::wstring out;
  out.reserve(s.size() * static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) out += s;
  return out;
}

// Copies [from, to) to the clipboard and deletes it unless read-only or the
// clipboard cannot be written.
void kill_range(Editor& ed, Buffer& b, size_t from, size_t to) {
  if (from >= to) return;
  if (!copy_range(ed, b, from, to)) return;
  if (!ed.check_writable(b)) return;
  ed.erase(b, from, to);
}

bool require_mark(Editor& ed, const Buffer& b) {
  if (b.has_mark) return true;
  ed.fail(L"The mark is not set now, so there is no region");
  return false;
}

// For commands on the region: false (with a message) when there is no mark
// or the region is empty. An empty region leaves the clipboard alone.
bool require_region(Editor& ed, const Buffer& b) {
  if (!require_mark(ed, b)) return false;
  if (b.mark != b.point) return true;
  ed.fail(L"Region is empty");
  return false;
}

// ---- Movement ----

bool step_forward_char(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point >= b.text.length()) {
    ed.fail(L"End of buffer");
    return false;
  }
  b.point = next_char_offset(b.text, b.point);
  return true;
}

bool step_backward_char(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point == 0) {
    ed.fail(L"Beginning of buffer");
    return false;
  }
  b.point = prev_char_offset(b.text, b.point);
  return true;
}

void forward_char(Editor& ed) {
  repeat_steps(ed, step_forward_char, step_backward_char);
}

void backward_char(Editor& ed) {
  repeat_steps(ed, step_backward_char, step_forward_char);
}

// One screen row down or up at the goal column (cells within the row, PLAN 2
// "vertical motion in wrapped lines"); false at the end (point goes to the
// end / start of the buffer, as in Emacs). The minibuffer has no wrapping and
// moves by logical lines.
bool vertical_step(Editor& ed, bool down) {
  Buffer& b = ed.current();
  const PieceTree& t = b.text;
  if (&b == &ed.main_buffer()) {
    const WrapLayout& layout = ed.layout();
    RowPos pos = layout.row_of(t, b.point, nullptr);
    if (down ? !layout.next_row(t, &pos) : !layout.previous_row(t, &pos)) {
      b.point = down ? t.length() : 0;
      ed.fail(down ? L"End of buffer" : L"Beginning of buffer");
      return false;
    }
    b.point = layout.offset_at(t, pos.line, pos.row, ed.goal_column());
    return true;
  }
  size_t line = t.line_of(b.point);
  if (down) {
    if (line + 1 >= t.line_count()) {
      b.point = t.length();
      ed.fail(L"End of buffer");
      return false;
    }
    ++line;
  } else {
    if (line == 0) {
      b.point = 0;
      ed.fail(L"Beginning of buffer");
      return false;
    }
    --line;
  }
  b.point = offset_at_column(t, line, ed.goal_column(), ed.tab_width());
  return true;
}

void vertical_move(Editor& ed, bool down) {
  Buffer& b = ed.current();
  const std::string& last = ed.last_command();
  if (last != "next-line" && last != "previous-line") {
    ed.set_goal_column(&b == &ed.main_buffer() ? ed.point_view().column
                                               : column_at(b.text, b.point, ed.tab_width()));
  }
  repeat_steps(
      ed, [down](Editor& e) { return vertical_step(e, down); },
      [down](Editor& e) { return vertical_step(e, !down); });
}

void next_line(Editor& ed) {
  vertical_move(ed, true);
}

void previous_line(Editor& ed) {
  vertical_move(ed, false);
}

void beginning_of_line(Editor& ed) {
  Buffer& b = ed.current();
  b.point = b.text.line_start(b.text.line_of(b.point));
}

void end_of_line(Editor& ed) {
  Buffer& b = ed.current();
  b.point = b.text.line_end(b.text.line_of(b.point));
}

// Word steps stop quietly at the ends of the buffer (as Emacs forward-word).
bool step_forward_word(Editor& ed) {
  Buffer& b = ed.current();
  const size_t p = forward_word_end(b.text, b.point);
  if (p == b.point) return false;
  b.point = p;
  return true;
}

bool step_backward_word(Editor& ed) {
  Buffer& b = ed.current();
  const size_t p = backward_word_start(b.text, b.point);
  if (p == b.point) return false;
  b.point = p;
  return true;
}

void forward_word(Editor& ed) {
  repeat_steps(ed, step_forward_word, step_backward_word);
}

void backward_word(Editor& ed) {
  repeat_steps(ed, step_backward_word, step_forward_word);
}

void beginning_of_buffer(Editor& ed) {
  Buffer& b = ed.current();
  push_mark(ed, b);
  b.point = 0;
}

void end_of_buffer(Editor& ed) {
  Buffer& b = ed.current();
  push_mark(ed, b);
  b.point = b.text.length();
}

size_t scroll_amount(Editor& ed) {
  const size_t visible = static_cast<size_t>(ed.visible_lines());
  return visible > 2 ? visible - 2 : 1;
}

// Emacs C-v with scroll-error-top-bottom nil: when the last screen row is
// already visible, nothing moves and "End of buffer" is shown. Otherwise the
// window scrolls by `amount` screen rows (visible - 2 without a prefix
// argument; decision E4), but never past the top at which the last row sits
// on the bottom row. Point moves to the new top row only when it would leave
// the window.
void scroll_up_by(Editor& ed, size_t amount) {
  Buffer& b = ed.main_buffer();
  const PieceTree& t = b.text;
  const WrapLayout& layout = ed.layout();
  const size_t visible = static_cast<size_t>(ed.visible_lines());
  RowPos max_top = layout.last_row(t);
  layout.backward_rows(t, &max_top, visible - 1);
  const RowPos top = ed.top();
  if (!(top < max_top)) {
    ed.fail(L"End of buffer");
    return;
  }
  RowPos new_top = top;
  layout.forward_rows(t, &new_top, amount);
  if (max_top < new_top) new_top = max_top;
  ed.set_top(new_top);
  if (layout.row_of(t, b.point, nullptr) < new_top) b.point = layout.row_start(t, new_top.line, new_top.row);
}

// Mirror of scroll_up_by: at the first row nothing moves and "Beginning of
// buffer" is shown.
void scroll_down_by(Editor& ed, size_t amount) {
  Buffer& b = ed.main_buffer();
  const PieceTree& t = b.text;
  const WrapLayout& layout = ed.layout();
  const RowPos top = ed.top();
  if (top == RowPos{}) {
    ed.fail(L"Beginning of buffer");
    return;
  }
  RowPos new_top = top;
  layout.backward_rows(t, &new_top, amount);
  ed.set_top(new_top);
  RowPos bottom = new_top;
  layout.forward_rows(t, &bottom, static_cast<size_t>(ed.visible_lines()) - 1);
  if (bottom < layout.row_of(t, b.point, nullptr)) b.point = layout.row_start(t, bottom.line, bottom.row);
}

// With a prefix argument N, C-v / M-v scroll by N lines (the other way for a
// negative N), as in Emacs.
void scroll_command(Editor& ed, bool up) {
  if (!ed.has_prefix_arg()) {
    up ? scroll_up_by(ed, scroll_amount(ed)) : scroll_down_by(ed, scroll_amount(ed));
    return;
  }
  const int n = signed_count(ed);
  if (n == 0) return;
  const size_t amount = static_cast<size_t>(n < 0 ? -static_cast<int64_t>(n) : n);
  ((n > 0) == up) ? scroll_up_by(ed, amount) : scroll_down_by(ed, amount);
}

void scroll_up_command(Editor& ed) {
  scroll_command(ed, true);
}

void scroll_down_command(Editor& ed) {
  scroll_command(ed, false);
}

void recenter_top_bottom(Editor& ed) {
  Buffer& b = ed.main_buffer();
  int& cycle = ed.recenter_cycle();
  cycle = ed.last_command() == "recenter-top-bottom" ? (cycle + 1) % 3 : 0;
  // By screen rows (decision E4).
  const WrapLayout& layout = ed.layout();
  RowPos top = layout.row_of(b.text, b.point, nullptr);
  const size_t visible = static_cast<size_t>(ed.visible_lines());
  if (cycle == 0) {
    layout.backward_rows(b.text, &top, visible / 2);
  } else if (cycle == 2) {
    layout.backward_rows(b.text, &top, visible - 1);
  }
  ed.set_top(top);
}

// ---- Mark ----

void set_mark_command(Editor& ed) {
  Buffer& b = ed.current();
  if (ed.last_command() == "set-mark-command" && b.mark_active && b.has_mark && b.mark == b.point) {
    b.mark_active = false;
    ed.set_message(L"Mark deactivated");
    return;
  }
  b.has_mark = true;
  b.mark = b.point;
  b.mark_active = true;
  ed.set_message(L"Mark set");
}

void exchange_point_and_mark(Editor& ed) {
  Buffer& b = ed.current();
  if (!b.has_mark) {
    ed.fail(L"No mark set in this buffer");
    return;
  }
  std::swap(b.point, b.mark);
  b.mark_active = true;
}

void mark_whole_buffer(Editor& ed) {
  Buffer& b = ed.current();
  b.has_mark = true;
  b.mark = b.text.length();
  b.point = 0;
  b.mark_active = true;
  ed.set_message(L"Mark set");
}

void select_all(Editor& ed) {
  Buffer& b = ed.current();
  b.has_mark = true;
  b.mark = 0;
  b.point = b.text.length();
  b.mark_active = true;
}

void keyboard_quit(Editor& ed) {
  if (ed.minibuffer_active()) ed.abort_minibuffer();
  ed.main_buffer().mark_active = false;
  ed.minibuffer().mark_active = false;
  ed.fail(L"Quit");
}

// ---- Delete and kill ----

// The offset `n` characters away from `p` (backward for a negative n), or
// npos when the buffer ends first.
size_t chars_away(const PieceTree& t, size_t p, int n) {
  for (int i = 0; i < n; ++i) {
    if (p >= t.length()) return std::wstring::npos;
    p = next_char_offset(t, p);
  }
  for (int i = 0; i > n; --i) {
    if (p == 0) return std::wstring::npos;
    p = prev_char_offset(t, p);
  }
  return p;
}

// Deletes `n` characters after point (before it for a negative n). Like
// Emacs, nothing is deleted when the buffer ends first.
void delete_chars(Editor& ed, int n) {
  Buffer& b = ed.current();
  if (n == 0) return;
  const size_t other = chars_away(b.text, b.point, n);
  if (other == std::wstring::npos) {
    ed.fail(n > 0 ? L"End of buffer" : L"Beginning of buffer");
    return;
  }
  ed.erase(b, std::min(b.point, other), std::max(b.point, other));
}

void delete_char(Editor& ed) {
  Buffer& b = ed.current();
  if (region_active(b) && !ed.has_prefix_arg()) {
    ed.erase(b, std::min(b.point, b.mark), std::max(b.point, b.mark));
    return;
  }
  delete_chars(ed, signed_count(ed));
}

void delete_backward_char(Editor& ed) {
  Buffer& b = ed.current();
  if (region_active(b) && !ed.has_prefix_arg()) {
    ed.erase(b, std::min(b.point, b.mark), std::max(b.point, b.mark));
    return;
  }
  delete_chars(ed, -signed_count(ed));
}

// Without a prefix argument: to the end of the line, or the line end alone.
// With N: N whole lines forward (from point to the start of the Nth next
// line), 0: back to the start of the line, -N: back over N line starts.
void kill_line(Editor& ed) {
  Buffer& b = ed.current();
  const PieceTree& t = b.text;
  const size_t line = t.line_of(b.point);
  if (!ed.has_prefix_arg()) {
    if (b.point >= t.length()) {
      ed.fail(L"End of buffer");
      return;
    }
    size_t end = t.line_end(line);
    if (end == b.point) end = b.point + 1;
    kill_range(ed, b, b.point, end);
    return;
  }
  const int n = signed_count(ed);
  if (n > 0) {
    if (b.point >= t.length()) {
      ed.fail(L"End of buffer");
      return;
    }
    const size_t target = line + static_cast<size_t>(n);
    const size_t end = target < t.line_count() ? t.line_start(target) : t.length();
    kill_range(ed, b, b.point, end);
    return;
  }
  if (n < 0 && b.point == 0) {
    ed.fail(L"Beginning of buffer");
    return;
  }
  const size_t back = static_cast<size_t>(-static_cast<int64_t>(n));
  const size_t start = t.line_start(line > back ? line - back : 0);
  kill_range(ed, b, start, b.point);
}

// End of the |n|th word from p (start of a word for a negative n).
size_t words_away(const PieceTree& t, size_t p, int n) {
  for (int i = 0; i < n && p < t.length(); ++i) p = forward_word_end(t, p);
  for (int i = 0; i > n && p > 0; --i) p = backward_word_start(t, p);
  return p;
}

void kill_words(Editor& ed, int n) {
  Buffer& b = ed.current();
  if (n == 0) return;
  if (n > 0 && b.point >= b.text.length()) {
    ed.fail(L"End of buffer");
    return;
  }
  if (n < 0 && b.point == 0) {
    ed.fail(L"Beginning of buffer");
    return;
  }
  const size_t other = words_away(b.text, b.point, n);
  kill_range(ed, b, std::min(b.point, other), std::max(b.point, other));
}

void kill_word(Editor& ed) {
  kill_words(ed, signed_count(ed));
}

void backward_kill_word(Editor& ed) {
  kill_words(ed, -signed_count(ed));
}

void kill_region(Editor& ed) {
  Buffer& b = ed.current();
  if (!require_region(ed, b)) return;
  kill_range(ed, b, std::min(b.point, b.mark), std::max(b.point, b.mark));
}

void kill_ring_save(Editor& ed) {
  Buffer& b = ed.current();
  if (!require_region(ed, b)) return;
  if (!copy_range(ed, b, std::min(b.point, b.mark), std::max(b.point, b.mark))) return;
  b.mark_active = false;
}

void yank(Editor& ed) {
  Buffer& b = ed.current();
  std::wstring text;
  const ClipboardRead read = ed.host().get_clipboard(text);
  if (read == ClipboardRead::kFailed) {
    ed.fail(L"Cannot read the clipboard");
    return;
  }
  if (read == ClipboardRead::kNoText || text.empty()) {
    ed.fail(L"Clipboard is empty");
    return;
  }
  if (!ed.check_writable(b)) return;
  b.has_mark = true;
  b.mark = b.point;
  ed.insert(b, normalize_line_ends(text));
}

// ---- Input ----

// The character a key inserts: a Unicode scalar value that is not a control
// character, typed without Ctrl or Meta (Shift is allowed; S-<letter> gives
// the capital letter). Special keys (<f1>, <left>, RET, ...) have none.
bool key_character(const Key& key, char32_t* c) {
  if (!key.is_char() || key.ctrl() || key.meta()) return false;
  const char32_t code = key.code;
  if (code >= 0xD800 && code <= 0xDFFF) return false;
  if (code < 0x20 || code == 0x7F || (code >= 0x80 && code < 0xA0)) return false;
  *c = (key.mods & kModShift) != 0 ? to_upper(code) : code;
  return true;
}

void self_insert_command(Editor& ed) {
  char32_t c = 0;
  if (!key_character(ed.last_key(), &c)) {
    ed.fail(key_to_string(ed.last_key()) + L" is not a character key");
    return;
  }
  const std::wstring unit = to_utf16(c);
  int count = 0;
  if (!insert_count(ed, &count, unit.size())) return;
  ed.insert(ed.current(), repeated(unit, count));
}

void newline(Editor& ed) {
  int count = 0;
  if (!insert_count(ed, &count, 1)) return;
  ed.insert(ed.current(), repeated(L"\n", count));
}

void open_line(Editor& ed) {
  int count = 0;
  if (!insert_count(ed, &count, 1)) return;
  Buffer& b = ed.current();
  ed.insert_at(b, b.point, repeated(L"\n", count));
}

void insert_tab(Editor& ed) {
  int count = 0;
  if (!insert_count(ed, &count, 1)) return;
  ed.insert(ed.current(), repeated(L"\t", count));
}

// insert-text: inserts its argument ("\r\n" and "\r" become "\n").
void insert_text(Editor& ed) {
  const std::vector<CommandArg>& args = ed.command_args();
  if (args.empty()) return;
  ed.insert(ed.current(), normalize_line_ends(args.front().text));
}

void quoted_insert(Editor& ed) {
  ed.start_quoted_insert();
}

// With a prefix argument N: the character before point moves N characters
// forward (backward for a negative N), as Emacs transpose-chars with an
// argument. Nothing changes when the buffer ends first.
void transpose_chars_n(Editor& ed, int n) {
  Buffer& b = ed.current();
  const PieceTree& t = b.text;
  const size_t p = b.point;
  if (n == 0) return;
  if (p == 0) {
    ed.fail(L"Beginning of buffer");
    return;
  }
  const size_t a = prev_char_offset(t, p);
  const size_t other = chars_away(t, n > 0 ? p : a, n);
  if (other == std::wstring::npos) {
    ed.fail(n > 0 ? L"End of buffer" : L"Beginning of buffer");
    return;
  }
  if (!ed.check_writable(b)) return;
  const std::wstring moved = t.substr(a, p - a);
  if (n > 0) {
    const std::wstring over = t.substr(p, other - p);
    ed.replace(b, a, other, over + moved);
    b.point = other;
  } else {
    const std::wstring over = t.substr(other, a - other);
    ed.replace(b, other, p, moved + over);
    b.point = other + moved.size();
  }
}

void transpose_chars(Editor& ed) {
  if (ed.has_prefix_arg()) {
    transpose_chars_n(ed, signed_count(ed));
    return;
  }
  Buffer& b = ed.current();
  const PieceTree& t = b.text;
  const size_t p = b.point;
  if (p == 0) {
    ed.fail(L"Beginning of buffer");
    return;
  }
  const bool at_eol = p >= t.length() || t.char_at(p) == L'\n';
  size_t a = 0;
  size_t mid = 0;
  size_t c = 0;
  if (at_eol) {
    mid = prev_char_offset(t, p);
    if (mid == 0) {
      ed.fail(L"Beginning of buffer");
      return;
    }
    a = prev_char_offset(t, mid);
    c = p;
  } else {
    a = prev_char_offset(t, p);
    mid = p;
    c = next_char_offset(t, p);
  }
  if (!ed.check_writable(b)) return;
  const std::wstring first = t.substr(a, mid - a);
  const std::wstring second = t.substr(mid, c - mid);
  ed.replace(b, a, c, second + first);
  b.point = c;
}

enum class CaseMode { kUpper, kLower, kCapitalize };

void convert_word(Editor& ed, CaseMode mode) {
  Buffer& b = ed.current();
  const size_t from = b.point;
  const size_t to = forward_word_end(b.text, from);
  if (from == to) return;
  const std::wstring s = b.text.substr(from, to - from);
  std::wstring out;
  out.reserve(s.size());
  bool in_word = false;
  size_t i = 0;
  while (i < s.size()) {
    size_t units = 0;
    const char32_t c = decode_at(s, i, &units);
    char32_t r = c;
    if (mode == CaseMode::kUpper) {
      r = to_upper(c);
    } else if (mode == CaseMode::kLower) {
      r = to_lower(c);
    } else if (word_class(c) != WordClass::kNone) {
      r = in_word ? to_lower(c) : to_upper(c);
      in_word = true;
    } else {
      in_word = false;
    }
    append_utf16(out, r);
    i += units;
  }
  if (out != s) {
    if (!ed.check_writable(b)) return;
    ed.replace(b, from, to, out);
  }
  b.point = from + out.size();
}

void upcase_word(Editor& ed) {
  convert_word(ed, CaseMode::kUpper);
}

void downcase_word(Editor& ed) {
  convert_word(ed, CaseMode::kLower);
}

void capitalize_word(Editor& ed) {
  convert_word(ed, CaseMode::kCapitalize);
}

// ---- Undo ----

// Undo / redo N steps (the prefix argument); stops with the message when the
// history ends first.
void undo_steps(Editor& ed, bool redo) {
  Buffer& b = ed.current();
  if (!ed.check_writable(b)) return;
  int count = 0;
  if (!insert_count(ed, &count)) return;
  for (int i = 0; i < count; ++i) {
    if (!(redo ? ed.redo(b) : ed.undo(b))) {
      ed.fail(redo ? L"No further redo information" : L"No further undo information");
      return;
    }
  }
  ed.set_message(redo ? L"Redo" : L"Undo");
}

void undo(Editor& ed) {
  undo_steps(ed, false);
}

void redo(Editor& ed) {
  undo_steps(ed, true);
}

// ---- Files ----

void find_file(Editor& ed) {
  ed.find_file();
}

void save_buffer(Editor& ed) {
  ed.save_buffer();
}

void write_file(Editor& ed) {
  ed.write_file();
}

void read_only_mode(Editor& ed) {
  Buffer& b = ed.main_buffer();
  b.read_only = !b.read_only;
  ed.set_message(b.read_only ? L"Read-only mode enabled" : L"Read-only mode disabled");
}

void quit(Editor& ed) {
  ed.quit();
}

void find_user_init_file(Editor& ed) {
  ed.find_user_init_file();
}

// ---- View (per window, never written to the settings file) ----

void toggle_line_numbers(Editor& ed) {
  ed.toggle_line_numbers();
}

void toggle_eol_marks(Editor& ed) {
  ed.toggle_eol_marks();
}

void toggle_whitespace_marks(Editor& ed) {
  ed.toggle_whitespace_marks();
}

void set_wrap_mode(Editor& ed) {
  const std::vector<CommandArg>& args = ed.command_args();
  WrapMode mode = WrapMode::kNone;
  if (args.empty() || !parse_wrap_mode(args.front().text, &mode)) {
    ed.fail(L"Wrap mode must be none, window, fixed or max");
    return;
  }
  ed.set_wrap_mode(mode);
}

void text_scale_increase(Editor& ed) {
  ed.text_scale_adjust(1);
}

void text_scale_decrease(Editor& ed) {
  ed.text_scale_adjust(-1);
}

void text_scale_reset(Editor& ed) {
  ed.text_scale_adjust(0);
}

// F10 (PLAN 2 "menu bar"): the window opens its menu bar after this command;
// with menu_bar: false there is none to open.
void menu_bar_open(Editor& ed) {
  if (!ed.config().menu_bar) {
    ed.fail(L"Menu bar is hidden");
    return;
  }
  ed.host().open_menu_bar();
}

// ---- Prefix argument and M-x ----

void universal_argument(Editor& ed) {
  ed.universal_argument();
}

void digit_argument(Editor& ed) {
  ed.digit_argument();
}

void negative_argument(Editor& ed) {
  ed.negative_argument();
}

void execute_extended_command(Editor& ed) {
  ed.execute_extended_command();
}

// ---- Minibuffer ----

bool require_minibuffer(Editor& ed) {
  if (ed.minibuffer_active()) return true;
  ed.fail(L"Not in the minibuffer");
  return false;
}

void exit_minibuffer(Editor& ed) {
  if (require_minibuffer(ed)) ed.exit_minibuffer();
}

void minibuffer_complete(Editor& ed) {
  if (require_minibuffer(ed)) ed.complete_minibuffer();
}

void previous_history_element(Editor& ed) {
  if (require_minibuffer(ed)) ed.minibuffer_history_previous();
}

void next_history_element(Editor& ed) {
  if (require_minibuffer(ed)) ed.minibuffer_history_next();
}

// ---- Parameters ----

constexpr const wchar_t* kEncodingNames[] = {
    L"utf-8",       L"utf-8-bom",    L"utf-16le", L"utf-16le-bom", L"utf-16be",
    L"utf-16be-bom", L"shift_jis",   L"euc-jp",   L"iso-2022-jp",  L"windows-1252",
    L"gb18030",     L"big5",         L"euc-kr",
};
constexpr const wchar_t* kEolChoices[] = {L"lf", L"crlf", L"cr"};
constexpr const wchar_t* kWrapChoices[] = {L"none", L"window", L"fixed", L"max"};

constexpr Param kTextParams[] = {{L"Text: ", ParamType::kText, {}}};
constexpr Param kLineParams[] = {{L"Goto line: ", ParamType::kInteger, {}}};
constexpr Param kEncodingParams[] = {{L"Coding system: ", ParamType::kChoice, kEncodingNames}};
constexpr Param kFileEncodingParams[] = {{L"Find file: ", ParamType::kText, {}},
                                         {L"Coding system: ", ParamType::kChoice, kEncodingNames}};
constexpr Param kEolParams[] = {{L"Line ending (lf, crlf, cr): ", ParamType::kChoice, kEolChoices}};
constexpr Param kWrapParams[] = {{L"Wrap mode (none, window, fixed, max): ", ParamType::kChoice, kWrapChoices}};

// Commands with a null function are registered for later milestones
// (decision D9): they can be bound, completed in M-x and run, which shows
// "<command> is not implemented yet".
constexpr CommandInfo kCommands[] = {
    {"backward-char", &backward_char},
    {"backward-kill-word", &backward_kill_word},
    {"backward-word", &backward_word},
    {"beginning-of-buffer", &beginning_of_buffer},
    {"beginning-of-line", &beginning_of_line},
    {"capitalize-word", &capitalize_word},
    {"copy", &kill_ring_save},
    {"cut", &kill_region},
    {"delete-backward-char", &delete_backward_char},
    {"delete-char", &delete_char},
    {"digit-argument", &digit_argument, {}, true},
    {"downcase-word", &downcase_word},
    {"end-of-buffer", &end_of_buffer},
    {"end-of-line", &end_of_line},
    {"exchange-point-and-mark", &exchange_point_and_mark},
    {"execute-extended-command", &execute_extended_command},
    {"exit-minibuffer", &exit_minibuffer},
    {"find-file", &find_file},
    {"find-file-with-encoding", nullptr, kFileEncodingParams},
    {"find-user-init-file", &find_user_init_file},
    {"forward-char", &forward_char},
    {"forward-word", &forward_word},
    {"goto-line", nullptr, kLineParams},
    {"insert-tab", &insert_tab},
    {"insert-text", &insert_text, kTextParams},
    {"isearch-abort", nullptr},
    {"isearch-backward", nullptr},
    {"isearch-delete-char", nullptr},
    {"isearch-exit", nullptr},
    {"isearch-forward", nullptr},
    {"isearch-query-replace", nullptr},
    {"isearch-repeat-backward", nullptr},
    {"isearch-repeat-forward", nullptr},
    {"isearch-toggle-case-fold", nullptr},
    {"isearch-toggle-regexp", nullptr},
    {"keyboard-quit", &keyboard_quit},
    {"kill-line", &kill_line},
    {"kill-region", &kill_region},
    {"kill-ring-save", &kill_ring_save},
    {"kill-word", &kill_word},
    {"kmacro-end-and-call-macro", nullptr},
    {"kmacro-end-macro", nullptr},
    {"kmacro-start-macro", nullptr},
    {"mark-whole-buffer", &mark_whole_buffer},
    {"menu-bar-open", &menu_bar_open},
    {"minibuffer-complete", &minibuffer_complete},
    {"negative-argument", &negative_argument, {}, true},
    {"newline", &newline},
    {"next-history-element", &next_history_element},
    {"next-line", &next_line},
    {"open-line", &open_line},
    {"paste", &yank},
    {"previous-history-element", &previous_history_element},
    {"previous-line", &previous_line},
    {"query-replace", nullptr},
    {"query-replace-act", nullptr},
    {"query-replace-act-and-exit", nullptr},
    {"query-replace-all", nullptr},
    {"query-replace-exit", nullptr},
    {"query-replace-regexp", nullptr},
    {"query-replace-skip", nullptr},
    {"quit", &quit},
    {"quoted-insert", &quoted_insert},
    {"read-only-mode", &read_only_mode},
    {"recenter-top-bottom", &recenter_top_bottom},
    {"redo", &redo},
    {"revert-buffer-with-coding-system", nullptr, kEncodingParams},
    {"save-buffer", &save_buffer},
    {"scroll-down-command", &scroll_down_command},
    {"scroll-up-command", &scroll_up_command},
    {"select-all", &select_all},
    {"self-insert-command", &self_insert_command},
    {"set-buffer-file-coding-system", nullptr, kEncodingParams},
    {"set-eol", nullptr, kEolParams},
    {"set-mark-command", &set_mark_command},
    {"set-wrap-mode", &set_wrap_mode, kWrapParams},
    {"text-scale-decrease", &text_scale_decrease},
    {"text-scale-increase", &text_scale_increase},
    {"text-scale-reset", &text_scale_reset},
    {"toggle-eol-marks", &toggle_eol_marks},
    {"toggle-line-numbers", &toggle_line_numbers},
    {"toggle-whitespace-marks", &toggle_whitespace_marks},
    {"transpose-chars", &transpose_chars},
    {"undo", &undo},
    {"universal-argument", &universal_argument, {}, true},
    {"upcase-word", &upcase_word},
    {"write-file", &write_file},
    {"yank", &yank},
};

}  // namespace

std::span<const CommandInfo> all_commands() {
  return kCommands;
}

std::span<const wchar_t* const> encoding_names() {
  return kEncodingNames;
}

const CommandInfo* find_command(std::string_view name) {
  for (const CommandInfo& c : kCommands) {
    if (name == c.name) return &c;
  }
  return nullptr;
}

void word_bounds_at(const PieceTree& text, size_t offset, size_t* from, size_t* to) {
  const size_t n = text.length();
  if (offset >= n) {
    *from = n;
    *to = n;
    return;
  }
  if (class_at(text, offset) == WordClass::kNone) {
    *from = offset;
    *to = next_char_offset(text, offset);
    return;
  }
  // The end of the word that holds `offset`, then back over the same word.
  *to = forward_word_end(text, offset);
  *from = backward_word_start(text, *to);
}

}  // namespace geditor
