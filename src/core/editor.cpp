#include "editor.h"

#include <algorithm>

#include "commands.h"
#include "layout.h"
#include "path.h"
#include "unicode.h"

namespace geditor {
namespace {

// Moves the mark of a buffer while undo/redo change its text.
class MarkFollower final : public EditListener {
 public:
  explicit MarkFollower(Buffer& b) : b_(b) {}
  void on_insert(size_t offset, size_t count) override {
    if (b_.has_mark && b_.mark > offset) b_.mark += count;
  }
  void on_erase(size_t offset, size_t count) override {
    if (!b_.has_mark) return;
    if (b_.mark >= offset + count) {
      b_.mark -= count;
    } else if (b_.mark > offset) {
      b_.mark = offset;
    }
  }

 private:
  Buffer& b_;
};

constexpr Key kCtrlG{U'g', kModCtrl};
constexpr Key kEscape{kKeyEscape, 0};

}  // namespace

Editor::Editor(Host& host)
    : host_(host),
      global_map_(make_keymap(default_global_bindings())),
      minibuffer_map_(make_keymap(default_minibuffer_bindings())) {}

// ---- Key dispatch ----

Keymap::Lookup Editor::lookup(const KeySequence& keys) const {
  if (minibuffer_active()) {
    const Keymap::Lookup r = minibuffer_map_.lookup(keys);
    if (r.result != Keymap::Result::kUndefined) return r;
  }
  return global_map_.lookup(keys);
}

void Editor::handle_key(const Key& key) {
  message_.clear();
  if (quoted_insert_pending_) {
    quoted_insert_pending_ = false;
    handle_quoted_insert(key);
    return;
  }
  Key k = key;
  if (meta_pending_) {
    meta_pending_ = false;
    k.mods |= kModMeta;
  } else if (k == kEscape) {
    KeySequence probe = pending_;
    probe.push_back(k);
    if (lookup(probe).result == Keymap::Result::kUndefined) {
      // ESC followed by a key means M-<key>.
      meta_pending_ = true;
      if (!pending_display_.empty()) pending_display_ += L' ';
      pending_display_ += L"ESC";
      return;
    }
  }
  if (!pending_display_.empty()) pending_display_ += L' ';
  pending_display_ += key_to_string(key);
  pending_.push_back(k);

  const Keymap::Lookup r =
      lookup_shift_translated(pending_, [this](const KeySequence& s) { return lookup(s); });
  if (r.result == Keymap::Result::kPrefix) return;

  k = pending_.back();  // without S- when shift translation applied
  const size_t length = pending_.size();
  const std::wstring display = std::move(pending_display_);
  pending_.clear();
  pending_display_.clear();
  last_key_ = k;
  if (r.result == Keymap::Result::kCommand) {
    run_command(r.command, false);
    return;
  }
  if (length == 1 && is_self_insert_key(k)) {
    run_command("self-insert-command", true);
    return;
  }
  message_ = key == kCtrlG ? std::wstring(L"Quit") : display + L" is undefined";
  note_non_command();
}

bool Editor::key_is_bound(const Key& key) const {
  if (quoted_insert_pending_) return true;
  Key k = key;
  if (meta_pending_) k.mods |= kModMeta;
  KeySequence keys = pending_;
  keys.push_back(k);
  return lookup_shift_translated(keys, [this](const KeySequence& s) { return lookup(s); }).result !=
         Keymap::Result::kUndefined;
}

void Editor::execute_command(std::string_view name) {
  message_.clear();
  pending_.clear();
  pending_display_.clear();
  meta_pending_ = false;
  run_command(name, false);
}

void Editor::run_command(std::string_view name, bool typing) {
  const CommandInfo* info = find_command(name);
  if (info == nullptr) {
    message_ = L"Unknown command: " + widen(name);
    note_non_command();
    return;
  }
  this_command_ = info->name;
  Buffer& b = current();
  main_.edited_in_command = false;
  mini_.edited_in_command = false;
  b.undo.begin_command(b.point, typing);
  info->fn(*this);
  b.undo.end_command(b.point);
  finish_command(b);
}

void Editor::finish_command(Buffer&) {
  for (Buffer* b : {&main_, &mini_}) {
    if (b->edited_in_command) b->mark_active = false;
    b->edited_in_command = false;
  }
  last_command_ = this_command_;
  ensure_point_visible();
}

void Editor::note_non_command() {
  Buffer& b = current();
  b.undo.begin_command(b.point, false);
  b.undo.end_command(b.point);
  last_command_ = "undefined";
}

void Editor::handle_quoted_insert(const Key& key) {
  char32_t c = 0;
  bool ok = !key.meta();
  if (ok && key.is_char()) {
    c = key.code;
    if (key.ctrl()) {
      if (c >= U'a' && c <= U'z') {
        c = c - U'a' + 1;
      } else if (c == U'@' || c == U' ') {
        c = 0;
      } else if (c >= U'[' && c <= U'_') {
        c = c - U'[' + 0x1B;
      } else if (c == U'?') {
        c = 0x7F;
      } else {
        ok = false;
      }
    } else if ((key.mods & kModShift) != 0) {
      c = to_upper(c);
    }
  } else if (ok) {
    switch (key.code) {
      case kKeyReturn: c = U'\r'; break;
      case kKeyTab: c = U'\t'; break;
      case kKeyEscape: c = 0x1B; break;
      case kKeyBackspace: c = 0x7F; break;
      default: ok = false; break;
    }
  }
  last_key_ = key;
  this_command_ = "quoted-insert";
  Buffer& b = current();
  main_.edited_in_command = false;
  mini_.edited_in_command = false;
  b.undo.begin_command(b.point, false);
  if (ok && c == U'\r') {
    // The buffer keeps line ends as "\n" only (PLAN 2 "newly entered line ends").
    message_ = L"Cannot insert a carriage return";
  } else if (ok) {
    insert(b, to_utf16(c));
  } else {
    message_ = key_to_string(key) + L" cannot be inserted";
  }
  b.undo.end_command(b.point);
  finish_command(b);
}

void Editor::insert_text(std::wstring_view text) {
  message_.clear();
  pending_.clear();
  pending_display_.clear();
  meta_pending_ = false;
  quoted_insert_pending_ = false;
  if (text.empty()) return;
  this_command_ = "insert-text";
  Buffer& b = current();
  main_.edited_in_command = false;
  mini_.edited_in_command = false;
  b.undo.begin_command(b.point, false);
  insert(b, normalize_line_ends(text));
  b.undo.end_command(b.point);
  finish_command(b);
}

// ---- View state ----

std::wstring Editor::echo_area_text() const {
  if (minibuffer_active()) {
    std::wstring s = prompt_ + mini_.text.text();
    if (!message_.empty()) s += L" " + message_;
    return s;
  }
  return message_;
}

std::wstring Editor::pending_keys_text() const {
  if (quoted_insert_pending_) return L"C-q-";
  if (pending_display_.empty()) return std::wstring();
  return pending_display_ + L"-";
}

std::wstring Editor::buffer_name() const {
  return main_.path.empty() ? std::wstring(L"untitled") : file_name_of(main_.path);
}

std::wstring Editor::mode_line() const {
  const bool modified = main_.modified();
  const wchar_t* flag = main_.read_only ? (modified ? L"%*" : L"%%") : (modified ? L"**" : L"--");
  return std::wstring(L"-") + flag + L" " + buffer_name() + L"  " + encoding_name(main_.format) + L" " +
         eol_name(main_.format.eol) + L"  L" + std::to_wstring(point_line() + 1) + L" C" +
         std::to_wstring(point_column());
}

size_t Editor::point_line() const {
  return main_.text.line_of(main_.point);
}

int Editor::point_column() const {
  return column_at(main_.text, main_.point, tab_width_);
}

void Editor::set_tab_width(int width) {
  tab_width_ = std::max(1, width);
}

void Editor::set_point(size_t offset) {
  offset = std::min(offset, main_.text.length());
  if (offset > 0 && offset < main_.text.length() && is_low_surrogate(main_.text.char_at(offset)) &&
      is_high_surrogate(main_.text.char_at(offset - 1))) {
    --offset;
  }
  // A click is a command of its own: it ends a typing group and the goal
  // column of consecutive vertical motion.
  Buffer& b = current();
  b.undo.begin_command(b.point, false);
  b.undo.end_command(b.point);
  if (&b != &main_) {
    main_.undo.begin_command(main_.point, false);
    main_.undo.end_command(main_.point);
  }
  main_.point = offset;
  last_command_ = "set-point";
  ensure_point_visible();
}

int Editor::visible_lines() {
  return std::max(1, host_.visible_lines());
}

void Editor::ensure_point_visible() {
  const size_t last_line = main_.text.line_count() - 1;
  if (top_line_ > last_line) top_line_ = last_line;
  const size_t line = main_.text.line_of(main_.point);
  const size_t visible = static_cast<size_t>(visible_lines());
  if (line < top_line_) {
    top_line_ = line;
  } else if (line >= top_line_ + visible) {
    top_line_ = line - visible + 1;
  }
}

// ---- Editing primitives ----

bool Editor::check_writable(Buffer& b) {
  if (!b.read_only) return true;
  message_ = L"Buffer is read-only";
  return false;
}

void Editor::adjust_markers(Buffer& b, size_t offset, size_t inserted, size_t erased) {
  auto adjust = [&](size_t& m) {
    if (inserted > 0) {
      if (m > offset) m += inserted;
    } else if (m >= offset + erased) {
      m -= erased;
    } else if (m > offset) {
      m = offset;
    }
  };
  adjust(b.point);
  if (b.has_mark) adjust(b.mark);
}

bool Editor::insert_at(Buffer& b, size_t offset, std::wstring_view text) {
  if (!check_writable(b)) return false;
  if (&b == &mini_) {
    // The minibuffer reads one line: keep the text before the first line end.
    const size_t eol = text.find_first_of(L"\r\n");
    if (eol != std::wstring_view::npos) text = text.substr(0, eol);
  }
  if (text.empty()) return true;
  const PieceList pieces = b.text.insert(offset, text);
  b.undo.record_insert(offset, pieces);
  adjust_markers(b, offset, text.size(), 0);
  b.edited_in_command = true;
  return true;
}

bool Editor::insert(Buffer& b, std::wstring_view text) {
  const size_t offset = b.point;
  const size_t length_before = b.text.length();
  if (!insert_at(b, offset, text)) return false;
  b.point = offset + (b.text.length() - length_before);
  return true;
}

bool Editor::replace(Buffer& b, size_t from, size_t to, std::wstring_view text) {
  if (!check_writable(b)) return false;
  const bool had_mark = b.has_mark;
  const size_t mark = b.mark;
  const size_t length_before = b.text.length();
  erase(b, from, to);
  insert_at(b, from, text);
  if (had_mark) {
    const size_t inserted = b.text.length() - (length_before - (to - from));
    if (mark <= from) {
      b.mark = mark;
    } else if (mark >= to) {
      b.mark = mark - (to - from) + inserted;
    } else {
      b.mark = from + std::min(mark - from, inserted);
    }
  }
  return true;
}

bool Editor::erase(Buffer& b, size_t from, size_t to) {
  if (!check_writable(b)) return false;
  if (from >= to) return true;
  b.undo.record_erase(from, b.text.erase(from, to - from));
  adjust_markers(b, from, 0, to - from);
  b.edited_in_command = true;
  return true;
}

bool Editor::undo(Buffer& b) {
  MarkFollower follower(b);
  size_t point = b.point;
  if (!b.undo.undo(b.text, &follower, &point)) return false;
  b.point = std::min(point, b.text.length());
  b.edited_in_command = true;
  return true;
}

bool Editor::redo(Buffer& b) {
  MarkFollower follower(b);
  size_t point = b.point;
  if (!b.undo.redo(b.text, &follower, &point)) return false;
  b.point = std::min(point, b.text.length());
  b.edited_in_command = true;
  return true;
}

// ---- Minibuffer ----

void Editor::start_minibuffer(MinibufferPurpose purpose, std::wstring prompt, std::wstring_view initial) {
  mini_.reset();
  mini_.text = PieceTree(std::wstring(initial));
  mini_.point = initial.size();
  purpose_ = purpose;
  prompt_ = std::move(prompt);
  file_history_.reset_position();
}

void Editor::abort_minibuffer() {
  purpose_ = MinibufferPurpose::kNone;
  prompt_.clear();
  mini_.reset();
  after_save_ = AfterSave::kNone;
}

void Editor::exit_minibuffer() {
  const std::wstring input = mini_.text.text();
  const MinibufferPurpose purpose = purpose_;
  file_history_.add(input);
  purpose_ = MinibufferPurpose::kNone;
  prompt_.clear();
  mini_.reset();
  if (purpose == MinibufferPurpose::kFindFile) {
    request_open(input);
  } else if (purpose == MinibufferPurpose::kWriteFile) {
    finish_write_file(input);
  }
}

void Editor::complete_minibuffer() {
  const std::wstring input = mini_.text.text();
  const FileCompletion r = complete_file_name(input, host_);
  if (r.text != input) set_minibuffer_text(r.text);
  message_ = r.message;
}

void Editor::set_minibuffer_text(std::wstring_view text) {
  erase(mini_, 0, mini_.text.length());
  insert_at(mini_, 0, text);
  mini_.point = mini_.text.length();
}

void Editor::minibuffer_history_previous() {
  std::wstring text;
  if (!file_history_.previous(mini_.text.text(), &text)) {
    message_ = L"Beginning of history; no preceding item";
    return;
  }
  set_minibuffer_text(text);
}

void Editor::minibuffer_history_next() {
  std::wstring text;
  if (!file_history_.next(&text)) {
    message_ = L"End of history; no default available";
    return;
  }
  set_minibuffer_text(text);
}

}  // namespace geditor
