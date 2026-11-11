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
  ed.set_message(L"Cannot write to the clipboard");
  return false;
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
  ed.set_message(L"The mark is not set now, so there is no region");
  return false;
}

// For commands on the region: false (with a message) when there is no mark
// or the region is empty. An empty region leaves the clipboard alone.
bool require_region(Editor& ed, const Buffer& b) {
  if (!require_mark(ed, b)) return false;
  if (b.mark != b.point) return true;
  ed.set_message(L"Region is empty");
  return false;
}

// ---- Movement ----

void forward_char(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point >= b.text.length()) {
    ed.set_message(L"End of buffer");
    return;
  }
  b.point = next_char_offset(b.text, b.point);
}

void backward_char(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point == 0) {
    ed.set_message(L"Beginning of buffer");
    return;
  }
  b.point = prev_char_offset(b.text, b.point);
}

void vertical_move(Editor& ed, bool down) {
  Buffer& b = ed.current();
  const PieceTree& t = b.text;
  const std::string& last = ed.last_command();
  if (last != "next-line" && last != "previous-line") {
    ed.set_goal_column(column_at(t, b.point, ed.tab_width()));
  }
  size_t line = t.line_of(b.point);
  if (down) {
    if (line + 1 >= t.line_count()) {
      b.point = t.length();
      ed.set_message(L"End of buffer");
      return;
    }
    ++line;
  } else {
    if (line == 0) {
      b.point = 0;
      ed.set_message(L"Beginning of buffer");
      return;
    }
    --line;
  }
  b.point = offset_at_column(t, line, ed.goal_column(), ed.tab_width());
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

void forward_word(Editor& ed) {
  Buffer& b = ed.current();
  b.point = forward_word_end(b.text, b.point);
}

void backward_word(Editor& ed) {
  Buffer& b = ed.current();
  b.point = backward_word_start(b.text, b.point);
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

// Emacs C-v with scroll-error-top-bottom nil: when the last line is already
// visible, nothing moves and "End of buffer" is shown. Otherwise the window
// scrolls by visible - 2 lines, but never past the top at which the last line
// sits on the bottom row. Point moves to the new top line only when it would
// leave the window.
void scroll_up_command(Editor& ed) {
  Buffer& b = ed.main_buffer();
  const size_t lines = b.text.line_count();
  const size_t visible = static_cast<size_t>(ed.visible_lines());
  const size_t max_top = lines > visible ? lines - visible : 0;
  const size_t top = ed.top_line();
  if (top >= max_top) {
    ed.set_message(L"End of buffer");
    return;
  }
  const size_t new_top = std::min(top + scroll_amount(ed), max_top);
  ed.set_top_line(new_top);
  if (b.text.line_of(b.point) < new_top) b.point = b.text.line_start(new_top);
}

// Mirror of scroll_up_command: at top 0 nothing moves and "Beginning of
// buffer" is shown.
void scroll_down_command(Editor& ed) {
  Buffer& b = ed.main_buffer();
  const size_t top = ed.top_line();
  if (top == 0) {
    ed.set_message(L"Beginning of buffer");
    return;
  }
  const size_t amount = scroll_amount(ed);
  const size_t new_top = top > amount ? top - amount : 0;
  ed.set_top_line(new_top);
  const size_t bottom = new_top + static_cast<size_t>(ed.visible_lines()) - 1;
  if (b.text.line_of(b.point) > bottom) b.point = b.text.line_start(bottom);
}

void recenter_top_bottom(Editor& ed) {
  Buffer& b = ed.main_buffer();
  int& cycle = ed.recenter_cycle();
  cycle = ed.last_command() == "recenter-top-bottom" ? (cycle + 1) % 3 : 0;
  const size_t line = b.text.line_of(b.point);
  const size_t visible = static_cast<size_t>(ed.visible_lines());
  size_t top = 0;
  if (cycle == 0) {
    top = line >= visible / 2 ? line - visible / 2 : 0;
  } else if (cycle == 1) {
    top = line;
  } else {
    top = line >= visible - 1 ? line - (visible - 1) : 0;
  }
  ed.set_top_line(top);
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
    ed.set_message(L"No mark set in this buffer");
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
  ed.set_message(L"Quit");
}

// ---- Delete and kill ----

void delete_char(Editor& ed) {
  Buffer& b = ed.current();
  if (region_active(b)) {
    ed.erase(b, std::min(b.point, b.mark), std::max(b.point, b.mark));
    return;
  }
  if (b.point >= b.text.length()) {
    ed.set_message(L"End of buffer");
    return;
  }
  ed.erase(b, b.point, next_char_offset(b.text, b.point));
}

void delete_backward_char(Editor& ed) {
  Buffer& b = ed.current();
  if (region_active(b)) {
    ed.erase(b, std::min(b.point, b.mark), std::max(b.point, b.mark));
    return;
  }
  if (b.point == 0) {
    ed.set_message(L"Beginning of buffer");
    return;
  }
  ed.erase(b, prev_char_offset(b.text, b.point), b.point);
}

void kill_line(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point >= b.text.length()) {
    ed.set_message(L"End of buffer");
    return;
  }
  size_t end = b.text.line_end(b.text.line_of(b.point));
  if (end == b.point) end = b.point + 1;
  kill_range(ed, b, b.point, end);
}

void kill_word(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point >= b.text.length()) {
    ed.set_message(L"End of buffer");
    return;
  }
  kill_range(ed, b, b.point, forward_word_end(b.text, b.point));
}

void backward_kill_word(Editor& ed) {
  Buffer& b = ed.current();
  if (b.point == 0) {
    ed.set_message(L"Beginning of buffer");
    return;
  }
  kill_range(ed, b, backward_word_start(b.text, b.point), b.point);
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
  if (!ed.host().get_clipboard(text) || text.empty()) {
    ed.set_message(L"Clipboard is empty");
    return;
  }
  if (!ed.check_writable(b)) return;
  b.has_mark = true;
  b.mark = b.point;
  ed.insert(b, normalize_line_ends(text));
}

// ---- Input ----

void self_insert_command(Editor& ed) {
  Buffer& b = ed.current();
  ed.insert(b, to_utf16(ed.last_key().code));
}

void newline(Editor& ed) {
  ed.insert(ed.current(), L"\n");
}

void open_line(Editor& ed) {
  Buffer& b = ed.current();
  ed.insert_at(b, b.point, L"\n");
}

void insert_tab(Editor& ed) {
  ed.insert(ed.current(), L"\t");
}

void quoted_insert(Editor& ed) {
  ed.start_quoted_insert();
}

void transpose_chars(Editor& ed) {
  Buffer& b = ed.current();
  const PieceTree& t = b.text;
  const size_t p = b.point;
  if (p == 0) {
    ed.set_message(L"Beginning of buffer");
    return;
  }
  const bool at_eol = p >= t.length() || t.char_at(p) == L'\n';
  size_t a = 0;
  size_t mid = 0;
  size_t c = 0;
  if (at_eol) {
    mid = prev_char_offset(t, p);
    if (mid == 0) {
      ed.set_message(L"Beginning of buffer");
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

void undo(Editor& ed) {
  Buffer& b = ed.current();
  if (!ed.check_writable(b)) return;
  ed.set_message(ed.undo(b) ? L"Undo" : L"No further undo information");
}

void redo(Editor& ed) {
  Buffer& b = ed.current();
  if (!ed.check_writable(b)) return;
  ed.set_message(ed.redo(b) ? L"Redo" : L"No further redo information");
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

// ---- Minibuffer ----

bool require_minibuffer(Editor& ed) {
  if (ed.minibuffer_active()) return true;
  ed.set_message(L"Not in the minibuffer");
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
    {"downcase-word", &downcase_word},
    {"end-of-buffer", &end_of_buffer},
    {"end-of-line", &end_of_line},
    {"exchange-point-and-mark", &exchange_point_and_mark},
    {"exit-minibuffer", &exit_minibuffer},
    {"find-file", &find_file},
    {"forward-char", &forward_char},
    {"forward-word", &forward_word},
    {"insert-tab", &insert_tab},
    {"keyboard-quit", &keyboard_quit},
    {"kill-line", &kill_line},
    {"kill-region", &kill_region},
    {"kill-ring-save", &kill_ring_save},
    {"kill-word", &kill_word},
    {"mark-whole-buffer", &mark_whole_buffer},
    {"minibuffer-complete", &minibuffer_complete},
    {"newline", &newline},
    {"next-history-element", &next_history_element},
    {"next-line", &next_line},
    {"open-line", &open_line},
    {"paste", &yank},
    {"previous-history-element", &previous_history_element},
    {"previous-line", &previous_line},
    {"quit", &quit},
    {"quoted-insert", &quoted_insert},
    {"read-only-mode", &read_only_mode},
    {"recenter-top-bottom", &recenter_top_bottom},
    {"redo", &redo},
    {"save-buffer", &save_buffer},
    {"scroll-down-command", &scroll_down_command},
    {"scroll-up-command", &scroll_up_command},
    {"select-all", &select_all},
    {"self-insert-command", &self_insert_command},
    {"set-mark-command", &set_mark_command},
    {"transpose-chars", &transpose_chars},
    {"undo", &undo},
    {"upcase-word", &upcase_word},
    {"write-file", &write_file},
    {"yank", &yank},
};

}  // namespace

std::span<const CommandInfo> all_commands() {
  return kCommands;
}

const CommandInfo* find_command(std::string_view name) {
  for (const CommandInfo& c : kCommands) {
    if (name == c.name) return &c;
  }
  return nullptr;
}

}  // namespace geditor
