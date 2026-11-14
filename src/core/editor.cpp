#include "editor.h"

#include <algorithm>
#include <limits>

#include "commands.h"
#include "layout.h"
#include "path.h"
#include "unicode.h"

namespace geditor {
namespace {

// Moves the mark of a buffer while undo/redo change its text, and tells the
// wrap layout (main buffer only) where the text changed.
class MarkFollower final : public EditListener {
 public:
  MarkFollower(Buffer& b, WrapLayout* layout) : b_(b), layout_(layout) {}
  void on_insert(size_t offset, size_t count) override {
    if (layout_ != nullptr) layout_->note_edit(b_.text, offset, count, 0);
    if (b_.has_mark && b_.mark > offset) b_.mark += count;
  }
  void on_erase(size_t offset, size_t count) override {
    if (layout_ != nullptr) layout_->note_edit(b_.text, offset, 0, count);
    if (!b_.has_mark) return;
    if (b_.mark >= offset + count) {
      b_.mark -= count;
    } else if (b_.mark > offset) {
      b_.mark = offset;
    }
  }

 private:
  Buffer& b_;
  WrapLayout* layout_;
};

constexpr Key kCtrlG{U'g', kModCtrl};
constexpr Key kEscape{kKeyEscape, 0};
constexpr int kMaxPrefixValue = 100000000;  // digits beyond this are ignored
constexpr int kMaxPrefixMultiplier = 1 << 28;
constexpr size_t kEchoLines = 10;  // PLAN 2 "settings errors": up to 10 lines
// Most repeats of a command sequence (C-u N on a sequence).
constexpr size_t kMaxSequenceRepeat = 1'000'000;

// Command names are ASCII; false for anything else.
bool narrow_name(std::wstring_view s, std::string* out) {
  out->clear();
  for (const wchar_t c : s) {
    if (c <= 0 || c >= 0x80) return false;
    out->push_back(static_cast<char>(c));
  }
  return true;
}

void add_prefix_digit(PrefixArg& p, int digit) {
  if (!p.digits) {
    p.digits = true;
    p.value = 0;
    p.multiplier = 1;
  }
  if (p.value < kMaxPrefixValue) p.value = p.value * 10 + digit;
}

std::wstring join_display(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return a + L" " + b;
}

}  // namespace

int PrefixArg::count() const {
  if (!active) return 1;
  if (digits) return negative ? -value : value;
  if (negative) return -1;
  return multiplier;
}

Editor::Editor(Host& host) : host_(host) {
  rebuild_keymaps(config_, nullptr);
}

// ---- Key dispatch ----

Keymap::Lookup Editor::lookup(const KeySequence& keys) const {
  if (minibuffer_active()) {
    // An explicit null in minibuffer_keys does not fall through to global.
    const Keymap::Lookup r = keymap(KeymapKind::kMinibuffer).lookup(keys);
    if (r.result != Keymap::Result::kUndefined) return r;
  }
  return keymap(KeymapKind::kGlobal).lookup(keys);
}

void Editor::handle_key(const Key& key) {
  message_.clear();
  message_is_settings_ = false;
  if (quoted_insert_pending_) {
    quoted_insert_pending_ = false;
    handle_quoted_insert(key);
    return;
  }
  // C-g typed in the middle of a key sequence (after prefix keys, ESC or a
  // prefix argument) quits, as in Emacs, whatever ESC C-g (= C-M-g) or the
  // prefix + C-g would be bound to. A real Ctrl+Alt+G is not affected.
  if (key == kCtrlG && (meta_pending_ || !pending_.empty() || prefix_.active)) {
    cancel_pending_input();
    message_ = L"Quit";
    note_non_command();
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
  // Digits and "-" continue a prefix argument (C-u 1 2, C-u -).
  if (prefix_.active && pending_.empty() && k.mods == 0) {
    if (k.code >= U'0' && k.code <= U'9') {
      add_prefix_digit(prefix_, static_cast<int>(k.code - U'0'));
      prefix_display_ = join_display(prefix_display_, key_to_string(k));
      return;
    }
    if (k.code == U'-' && !prefix_.digits && !prefix_.negative) {
      prefix_.negative = true;
      prefix_.multiplier = 1;
      prefix_display_ = join_display(prefix_display_, L"-");
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
  std::wstring display = std::move(pending_display_);
  pending_.clear();
  pending_display_.clear();
  last_key_ = k;
  if (r.result == Keymap::Result::kCommand) {
    // A copy: the keymaps may be rebuilt (settings reload) while it runs.
    const Binding binding = *r.binding;
    key_display_ = std::move(display);
    dispatch(binding, false);
    return;
  }
  if (r.result == Keymap::Result::kUndefined && length == 1 && is_self_insert_key(k)) {
    key_display_ = std::move(display);
    dispatch(command_binding("self-insert-command"), true);
    return;
  }
  prefix_ = PrefixArg();
  prefix_display_.clear();
  message_ = key == kCtrlG && r.result == Keymap::Result::kUndefined ? std::wstring(L"Quit")
                                                                     : display + L" is undefined";
  note_non_command();
}

bool Editor::key_is_bound(const Key& key) const {
  if (quoted_insert_pending_) return true;
  Key k = key;
  if (meta_pending_) k.mods |= kModMeta;
  KeySequence keys = pending_;
  keys.push_back(k);
  const Keymap::Result r =
      lookup_shift_translated(keys, [this](const KeySequence& s) { return lookup(s); }).result;
  return r == Keymap::Result::kPrefix || r == Keymap::Result::kCommand;
}

void Editor::execute_command(std::string_view name) {
  message_.clear();
  message_is_settings_ = false;
  cancel_pending_input();
  key_display_.clear();
  dispatch(command_binding(std::string(name)), false);
}

void Editor::cancel_pending_input() {
  pending_.clear();
  pending_display_.clear();
  meta_pending_ = false;
  prefix_ = PrefixArg();
  prefix_display_.clear();
}

PrefixArg Editor::take_prefix() {
  const PrefixArg p = prefix_;
  command_prefix_display_ = std::move(prefix_display_);
  prefix_ = PrefixArg();
  prefix_display_.clear();
  return p;
}

bool Editor::dispatch(const Binding& binding, bool typing) {
  const bool ok = run_binding(binding, typing);
  // A command chosen in the minibuffer (M-x, or the last argument read)
  // runs after the command that closed the minibuffer, as its own command.
  while (deferred_call_) {
    const std::pair<CommandCall, PrefixArg> next = std::move(*deferred_call_);
    deferred_call_.reset();
    key_display_.clear();
    command_prefix_display_.clear();
    run_call(next.first, next.second, false, true);
  }
  return ok;
}

bool Editor::run_binding(const Binding& binding, bool typing) {
  const PrefixArg prefix = take_prefix();
  if (!binding.sequence) return run_call(binding.calls.front(), prefix, typing, true);
  // A command sequence (decision D7): the prefix argument repeats the whole
  // sequence; all repeats are one undo group; the first failure stops it and
  // keeps the edits made before it.
  const int count = prefix.count();
  if (count <= 0) {
    message_ = L"Repeat count must be positive";
    note_non_command();
    return false;
  }
  // A huge count must not freeze the editor: refuse before running when the
  // text the sequence inserts (known from its calls) times the count is too
  // much, or when the count alone is beyond any sensible repeat.
  size_t inserted_per_run = 0;
  for (const CommandCall& call : binding.calls) {
    if (call.name == "insert-text" && !call.args.empty()) {
      inserted_per_run += call.args.front().text.size();
    } else if (call.name == "self-insert-command" || call.name == "newline" ||
               call.name == "open-line" || call.name == "insert-tab") {
      inserted_per_run += 1;
    }
  }
  if (static_cast<size_t>(count) > kMaxSequenceRepeat ||
      (inserted_per_run > 0 && static_cast<size_t>(count) > kMaxRepeatedInsert / inserted_per_run)) {
    message_ = L"Repeat count too large";
    note_non_command();
    return false;
  }
  main_.undo.begin_command(main_.point, false);
  mini_.undo.begin_command(mini_.point, false);
  bool ok = true;
  for (int i = 0; ok && i < count; ++i) {
    for (const CommandCall& call : binding.calls) {
      if (!run_call(call, PrefixArg(), false, false)) {
        ok = false;
        break;
      }
    }
  }
  main_.undo.end_command(main_.point);
  mini_.undo.end_command(mini_.point);
  return ok;
}

bool Editor::run_call(const CommandCall& call, const PrefixArg& prefix, bool typing, bool own_group) {
  const CommandInfo* info = find_command(call.name);
  if (info == nullptr || info->fn == nullptr) {
    message_ = info == nullptr ? L"Unknown command: " + widen(call.name)
                               : widen(call.name) + L" is not implemented yet";
    if (own_group) note_non_command();
    return false;
  }
  if (!info->params.empty() && call.args.empty()) {
    // Run without its arguments: ask for them in the minibuffer.
    failed_ = false;
    start_reading_arguments(*info, prefix);
    const bool ok = !failed_;
    failed_ = false;
    if (own_group) note_non_command();
    return ok;
  }
  this_command_ = info->name;
  command_prefix_ = prefix;
  command_args_ = call.args;
  failed_ = false;
  const std::string previous_command = last_command_;
  Buffer& b = current();
  main_.edited_in_command = false;
  mini_.edited_in_command = false;
  if (own_group) b.undo.begin_command(b.point, typing);
  info->fn(*this);
  if (own_group) b.undo.end_command(b.point);
  finish_command(b);
  // The prefix argument commands are transparent for last-command checks
  // (C-n C-u 3 C-n keeps the goal column).
  if (info->prefix_argument) last_command_ = previous_command;
  const bool ok = !failed_;
  command_prefix_ = PrefixArg();
  command_args_.clear();
  failed_ = false;
  return ok;
}

// ---- Prefix argument ----

void Editor::universal_argument() {
  PrefixArg p = command_prefix_;
  if (p.active && !p.digits && !p.negative) {
    if (p.multiplier < kMaxPrefixMultiplier) p.multiplier *= 4;
    prefix_display_ = join_display(command_prefix_display_, key_display_);
  } else {
    p = PrefixArg();
    p.active = true;
    p.multiplier = 4;
    prefix_display_ = key_display_;
  }
  prefix_ = p;
}

void Editor::digit_argument() {
  PrefixArg p = command_prefix_;
  prefix_display_ = p.active ? join_display(command_prefix_display_, key_display_) : key_display_;
  p.active = true;
  const char32_t c = last_key_.code;
  add_prefix_digit(p, c >= U'0' && c <= U'9' ? static_cast<int>(c - U'0') : 0);
  prefix_ = p;
}

void Editor::negative_argument() {
  PrefixArg p = command_prefix_;
  prefix_display_ = p.active ? join_display(command_prefix_display_, key_display_) : key_display_;
  p.active = true;
  p.negative = !p.negative;
  p.multiplier = 1;
  prefix_ = p;
}

// ---- M-x and command arguments ----

void Editor::execute_extended_command() {
  if (minibuffer_active()) {
    fail(kRecursiveMinibufferMessage);
    return;
  }
  minibuffer_prefix_ = command_prefix_;
  start_minibuffer(MinibufferPurpose::kExtendedCommand, L"M-x ", L"");
}

void Editor::start_reading_arguments(const CommandInfo& info, const PrefixArg& prefix) {
  if (minibuffer_active()) {
    fail(kRecursiveMinibufferMessage);
    return;
  }
  reading_call_ = CommandCall{info.name, {}};
  minibuffer_prefix_ = prefix;
  start_minibuffer(MinibufferPurpose::kCommandArgument, info.params.front().prompt, L"");
}

void Editor::finish_command_name(const std::wstring& input) {
  std::string name;
  if (!narrow_name(input, &name) || find_command(name) == nullptr) {
    message_ = L"[No match]";  // the minibuffer stays for another try
    return;
  }
  command_history_.add(input);
  const PrefixArg prefix = minibuffer_prefix_;
  abort_minibuffer();
  deferred_call_.emplace(CommandCall{name, {}}, prefix);
}

void Editor::finish_argument(const std::wstring& input) {
  const CommandInfo* info = find_command(reading_call_.name);
  if (info == nullptr || reading_call_.args.size() >= info->params.size()) {
    abort_minibuffer();
    return;
  }
  const Param& param = info->params[reading_call_.args.size()];
  CommandArg arg;
  switch (param.type) {
    case ParamType::kText:
      arg.text = input;
      break;
    case ParamType::kInteger: {
      size_t i = input.size() > 0 && input[0] == L'-' ? 1 : 0;
      const bool ok = i < input.size() && input.size() - i <= 18 &&
                      std::all_of(input.begin() + static_cast<std::ptrdiff_t>(i), input.end(),
                                  [](wchar_t c) { return c >= L'0' && c <= L'9'; });
      if (!ok) {
        message_ = L"[Please enter a whole number]";
        return;
      }
      int64_t value = 0;
      for (; i < input.size(); ++i) value = value * 10 + (input[i] - L'0');
      arg.type = CommandArg::Type::kInteger;
      arg.integer = input[0] == L'-' ? -value : value;
      break;
    }
    case ParamType::kChoice: {
      const bool ok = std::any_of(param.choices.begin(), param.choices.end(),
                                  [&](const wchar_t* c) { return input == c; });
      if (!ok) {
        message_ = L"[No match]";
        return;
      }
      arg.text = input;
      break;
    }
  }
  argument_history_.add(input);
  reading_call_.args.push_back(std::move(arg));
  if (reading_call_.args.size() < info->params.size()) {
    CommandCall call = std::move(reading_call_);
    const PrefixArg prefix = minibuffer_prefix_;
    start_minibuffer(MinibufferPurpose::kCommandArgument, info->params[call.args.size()].prompt, L"");
    reading_call_ = std::move(call);
    minibuffer_prefix_ = prefix;
    return;
  }
  CommandCall call = std::move(reading_call_);
  const PrefixArg prefix = minibuffer_prefix_;
  abort_minibuffer();
  deferred_call_.emplace(std::move(call), prefix);
}

History& Editor::history_for(MinibufferPurpose purpose) {
  switch (purpose) {
    case MinibufferPurpose::kExtendedCommand: return command_history_;
    case MinibufferPurpose::kCommandArgument: return argument_history_;
    default: return file_history_;
  }
}

// ---- Settings ----

void Editor::rebuild_keymaps(const Config& config, std::vector<Diagnostic>* errors) {
  for (int i = 0; i < kKeymapCount; ++i) {
    const auto kind = static_cast<KeymapKind>(i);
    maps_[i] = build_keymap(emacs_layer(kind), compat_layer(kind), config.keys[static_cast<size_t>(i)],
                            errors);
  }
}

bool Editor::apply_settings_text(std::string_view utf8) {
  cancel_pending_input();
  ConfigParse parsed = parse_config(utf8);
  std::vector<Diagnostic> errors = std::move(parsed.errors);
  if (parsed.syntax_ok) {
    rebuild_keymaps(parsed.config, &errors);
    config_ = std::move(parsed.config);
    apply_view_settings();
  }
  std::stable_sort(errors.begin(), errors.end(), [](const Diagnostic& a, const Diagnostic& b) {
    return a.pos.line != b.pos.line ? a.pos.line < b.pos.line : a.pos.column < b.pos.column;
  });
  settings_errors_ = std::move(errors);
  show_settings_errors();
  return parsed.syntax_ok;
}

void Editor::show_settings_errors() {
  if (settings_errors_.empty()) {
    // A corrected file clears the errors shown for the previous one.
    if (message_is_settings_) message_.clear();
    message_is_settings_ = false;
    ensure_point_visible();
    return;
  }
  const std::wstring name = settings_path_.empty() ? std::wstring(L"settings") : file_name_of(settings_path_);
  // With the minibuffer active its line counts as one of the 10.
  const std::vector<std::wstring> lines =
      format_diagnostics(settings_errors_, name, minibuffer_active() ? kEchoLines - 1 : kEchoLines);
  message_.clear();
  for (const std::wstring& line : lines) {
    if (!message_.empty()) message_ += L'\n';
    message_ += line;
  }
  message_is_settings_ = true;
  ensure_point_visible();  // the echo area grew: keep point on screen
}

void Editor::reload_settings() {
  cancel_pending_input();
  if (settings_path_.empty()) return;
  const std::wstring& path = settings_path_;
  auto show = [this](std::wstring text) {
    message_ = std::move(text);
    message_is_settings_ = true;
    ensure_point_visible();
  };
  switch (host_.file_kind(path)) {
    case FileKind::kNone:
      if (settings_file_seen_) {
        settings_file_seen_ = false;
        config_ = Config();
        apply_view_settings();
        settings_errors_.clear();
        rebuild_keymaps(config_, nullptr);
        show(L"Settings file removed; using defaults");
      }
      return;
    case FileKind::kError:
      show(L"Cannot access " + path + L" (settings kept)");
      return;
    case FileKind::kDirectory:
      show(path + L" is a directory (settings kept)");
      return;
    case FileKind::kFile:
      break;
  }
  std::string bytes;
  std::wstring error;
  if (!host_.read_file(path, bytes, error)) {
    show((error.empty() ? L"Cannot read " + path : error) + L" (settings kept)");
    return;
  }
  settings_file_seen_ = true;
  apply_settings_text(bytes);
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
  message_is_settings_ = false;
  cancel_pending_input();
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

std::vector<std::wstring> Editor::message_lines(int max_lines) const {
  std::vector<std::wstring> lines;
  if (max_lines <= 0) return lines;
  size_t start = 0;
  for (;;) {
    const size_t end = message_.find(L'\n', start);
    lines.push_back(message_.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  const size_t max = static_cast<size_t>(max_lines);
  if (lines.size() <= max) return lines;
  if (message_is_settings_ && !settings_errors_.empty()) {
    // Re-format so that "... and N more" counts every hidden error.
    const std::wstring name = settings_path_.empty() ? std::wstring(L"settings") : file_name_of(settings_path_);
    return format_diagnostics(settings_errors_, name, max);
  }
  const size_t hidden = lines.size() - (max - 1);
  lines.resize(max - 1);
  lines.push_back(L"... and " + std::to_wstring(hidden) + L" more");
  return lines;
}

int Editor::echo_line_count(int max_rows) const {
  if (!message_is_multi_line()) return 1;
  const int minibuffer = minibuffer_active() ? 1 : 0;
  const int lines = static_cast<int>(std::count(message_.begin(), message_.end(), L'\n')) + 1;
  const int shown = std::min(lines, std::max(0, max_rows - minibuffer));
  return std::max(1, shown + minibuffer);
}

std::wstring Editor::pending_keys_text() const {
  if (quoted_insert_pending_) return L"C-q-";
  // The prefix argument being typed ("C-u 1 2") and the prefix keys.
  const std::wstring keys = join_display(prefix_display_, pending_display_);
  if (keys.empty()) return std::wstring();
  return keys + L"-";
}

std::wstring Editor::buffer_name() const {
  return main_.path.empty() ? std::wstring(L"untitled") : file_name_of(main_.path);
}

std::wstring Editor::mode_line() const {
  const bool modified = main_.modified();
  const wchar_t* flag = main_.read_only ? (modified ? L"%*" : L"%%") : (modified ? L"**" : L"--");
  return std::wstring(L"-") + flag + L" " + buffer_name() + L"  " + encoding_name(main_.format) + L" " +
         eol_name(main_.format.eol) + L"  L" + std::to_wstring(point_line() + 1) + L" C" +
         std::to_wstring(point_column()) + L"  wrap:" + wrap_mode_name(wrap_mode());
}

size_t Editor::point_line() const {
  return main_.text.line_of(main_.point);
}

int Editor::point_column() const {
  // Through the wrap layout: long lines start from a recorded state.
  int64_t logical = 0;
  layout().row_of(main_.text, main_.point, nullptr, &logical);
  return static_cast<int>(std::min<int64_t>(logical, std::numeric_limits<int>::max()));
}

void Editor::set_tab_width(int width) {
  tab_width_ = std::max(1, width);
}

// ---- View ----

void Editor::apply_view_settings() {
  tab_width_ = std::max(1, config_.tab_width);
  line_numbers_view_.reload(config_.line_numbers);
  eol_marks_view_.reload(config_.eol_marks_show);
  whitespace_view_.reload(whitespace_flags(config_));
  wrap_view_.reload(config_.wrap_mode);
}

DisplayOptions Editor::display_options() const {
  DisplayOptions o;
  o.line_numbers = line_numbers();
  o.eol_marks = eol_marks();
  const WhitespaceFlags ws = whitespace_view_.get(whitespace_flags(config_));
  o.space = ws[0];
  o.tab = ws[1];
  o.fullwidth_space = ws[2];
  o.trailing = ws[3];
  o.eol_lf = config_.eol_mark_lf;
  o.eol_crlf = config_.eol_mark_crlf;
  o.eol_cr = config_.eol_mark_cr;
  return o;
}

WrapMode Editor::wrap_mode() const {
  return wrap_view_.get(config_.wrap_mode);
}

bool Editor::line_numbers() const {
  return line_numbers_view_.get(config_.line_numbers);
}

bool Editor::eol_marks() const {
  return eol_marks_view_.get(config_.eol_marks_show);
}

bool Editor::whitespace_marks() const {
  const WhitespaceFlags ws = whitespace_view_.get(whitespace_flags(config_));
  return std::any_of(ws.begin(), ws.end(), [](bool b) { return b; });
}

int Editor::gutter_columns() const {
  return geditor::gutter_columns(main_.text.line_count(), line_numbers());
}

int Editor::text_columns() const {
  return std::max(1, host_.visible_columns() - gutter_columns());
}

int Editor::wrap_width() const {
  return geditor::wrap_width(wrap_mode(), config_.wrap_column, text_columns());
}

const WrapLayout& Editor::layout() const {
  wrap_.configure(wrap_width(), tab_width_);
  return wrap_;
}

Editor::PointView Editor::point_view() const {
  PointView v;
  v.pos = layout().row_of(main_.text, main_.point, &v.column);
  return v;
}

void Editor::display_row(RowPos pos, int left, int columns, DisplayRow* out) const {
  build_display_row(main_.text, layout(), main_.format.eol, display_options(), pos, left, columns, out);
}

bool Editor::next_row(RowPos* pos) const {
  return layout().next_row(main_.text, pos);
}

RowPos Editor::max_top() {
  const WrapLayout& l = layout();
  RowPos top = l.last_row(main_.text);
  l.backward_rows(main_.text, &top, static_cast<size_t>(visible_lines()) - 1);
  return top;
}

void Editor::keep_point_on_screen() {
  const WrapLayout& l = layout();
  const PieceTree& t = main_.text;
  const RowPos p = l.row_of(t, main_.point, nullptr);
  if (p < top_) {
    main_.point = l.row_start(t, top_.line, top_.row);
    return;
  }
  RowPos bottom = top_;
  l.forward_rows(t, &bottom, static_cast<size_t>(visible_lines()) - 1);
  if (bottom < p) main_.point = l.row_start(t, bottom.line, bottom.row);
}

ScrollBarState Editor::scroll_bar() {
  ScrollBarState s;
  const size_t lines = main_.text.line_count();
  s.max = lines - 1;
  s.page = static_cast<size_t>(visible_lines());
  s.pos = std::min(top_.line, s.max);
  return s;
}

void Editor::scroll_by_rows(long long rows) {
  // A scroll bar action is a command of its own, like a click (set_point).
  main_.undo.begin_command(main_.point, false);
  const WrapLayout& l = layout();
  if (rows > 0) {
    const RowPos limit = max_top();
    if (top_ < limit) {
      l.forward_rows(main_.text, &top_, static_cast<size_t>(rows));
      if (limit < top_) top_ = limit;
    }
  } else if (rows < 0) {
    l.backward_rows(main_.text, &top_, static_cast<size_t>(-rows));
  }
  keep_point_on_screen();
  main_.undo.end_command(main_.point);
  last_command_ = "scroll-bar";
}

void Editor::scroll_to_line(size_t line) {
  main_.undo.begin_command(main_.point, false);
  const RowPos limit = max_top();
  top_ = RowPos{std::min(line, main_.text.line_count() - 1), 0};
  if (limit < top_) top_ = limit;
  keep_point_on_screen();
  main_.undo.end_command(main_.point);
  last_command_ = "scroll-bar";
}

double Editor::font_pixels(double dpi) const {
  return text_scale_pixels(config_.font_size, text_scale_, dpi);
}

void Editor::toggle_line_numbers() {
  const bool on = !line_numbers();
  line_numbers_view_.set(on, config_.line_numbers);
  message_ = on ? L"Line numbers on" : L"Line numbers off";
}

void Editor::toggle_eol_marks() {
  const bool on = !eol_marks();
  eol_marks_view_.set(on, config_.eol_marks_show);
  message_ = on ? L"Line ending marks on" : L"Line ending marks off";
}

void Editor::toggle_whitespace_marks() {
  const WhitespaceFlags file = whitespace_flags(config_);
  const bool on = !whitespace_marks();
  whitespace_view_.set(on ? shown_whitespace_flags(file) : WhitespaceFlags{}, file);
  message_ = on ? L"Whitespace marks on" : L"Whitespace marks off";
}

void Editor::set_wrap_mode(WrapMode mode) {
  wrap_view_.set(mode, config_.wrap_mode);
  message_ = std::wstring(L"Wrap: ") + wrap_mode_name(mode);
}

void Editor::text_scale_adjust(int steps) {
  const double dpi = static_cast<double>(std::max(1, host_.dpi()));
  if (steps == 0) {
    text_scale_ = 0;
    message_ = L"Text scale reset";
    return;
  }
  const double now = font_pixels(dpi);
  const double next = text_scale_pixels(config_.font_size, text_scale_ + steps, dpi);
  if (steps > 0 ? next <= now : next >= now) {
    fail(steps > 0 ? L"Text is at the largest size" : L"Text is at the smallest size");
    return;
  }
  text_scale_ += steps;
  message_ = L"Text scale " + std::wstring(text_scale_ > 0 ? L"+" : L"") + std::to_wstring(text_scale_);
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
  const PieceTree& t = main_.text;
  const WrapLayout& l = layout();
  const size_t last_line = t.line_count() - 1;
  if (top_.line > last_line) top_ = RowPos{last_line, 0};
  if (!l.has_row(t, top_.line, top_.row)) top_.row = l.row_count(t, top_.line) - 1;
  const RowPos p = l.row_of(t, main_.point, nullptr);
  const size_t visible = static_cast<size_t>(visible_lines());
  if (p < top_) {
    top_ = p;
    return;
  }
  RowPos bottom = top_;
  l.forward_rows(t, &bottom, visible - 1);
  if (bottom < p) {
    top_ = p;
    l.backward_rows(t, &top_, visible - 1);
  }
}

void Editor::note_text_edit(const Buffer& b, size_t offset, size_t inserted, size_t erased) {
  if (&b == &main_) wrap_.note_edit(main_.text, offset, inserted, erased);
}

// ---- Editing primitives ----

bool Editor::check_writable(Buffer& b) {
  if (!b.read_only) return true;
  fail(L"Buffer is read-only");
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
  note_text_edit(b, offset, text.size(), 0);
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
  note_text_edit(b, from, 0, to - from);
  adjust_markers(b, from, 0, to - from);
  b.edited_in_command = true;
  return true;
}

bool Editor::undo(Buffer& b) {
  MarkFollower follower(b, &b == &main_ ? &wrap_ : nullptr);
  size_t point = b.point;
  if (!b.undo.undo(b.text, &follower, &point)) return false;
  b.point = std::min(point, b.text.length());
  b.edited_in_command = true;
  return true;
}

bool Editor::redo(Buffer& b) {
  MarkFollower follower(b, &b == &main_ ? &wrap_ : nullptr);
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
  history_for(purpose).reset_position();
}

void Editor::abort_minibuffer() {
  purpose_ = MinibufferPurpose::kNone;
  prompt_.clear();
  mini_.reset();
  after_save_ = AfterSave::kNone;
  reading_call_ = CommandCall();
  minibuffer_prefix_ = PrefixArg();
}

void Editor::exit_minibuffer() {
  const std::wstring input = mini_.text.text();
  const MinibufferPurpose purpose = purpose_;
  if (purpose == MinibufferPurpose::kExtendedCommand) {
    finish_command_name(input);
    return;
  }
  if (purpose == MinibufferPurpose::kCommandArgument) {
    finish_argument(input);
    return;
  }
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
  FileCompletion r;
  if (purpose_ == MinibufferPurpose::kExtendedCommand) {
    // Every registered command, implemented or not (decision D8).
    std::vector<std::wstring> names;
    for (const CommandInfo& c : all_commands()) names.push_back(widen(c.name));
    r = complete_name(input, std::move(names));
  } else if (purpose_ == MinibufferPurpose::kCommandArgument) {
    const CommandInfo* info = find_command(reading_call_.name);
    if (info == nullptr || reading_call_.args.size() >= info->params.size() ||
        info->params[reading_call_.args.size()].type != ParamType::kChoice) {
      message_ = L"[No completions]";
      return;
    }
    std::vector<std::wstring> names;
    for (const wchar_t* c : info->params[reading_call_.args.size()].choices) names.emplace_back(c);
    r = complete_name(input, std::move(names));
  } else {
    r = complete_file_name(input, host_);
  }
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
  if (!history_for(purpose_).previous(mini_.text.text(), &text)) {
    fail(L"Beginning of history; no preceding item");
    return;
  }
  set_minibuffer_text(text);
}

void Editor::minibuffer_history_next() {
  std::wstring text;
  if (!history_for(purpose_).next(&text)) {
    fail(L"End of history; no default available");
    return;
  }
  set_minibuffer_text(text);
}

}  // namespace geditor
