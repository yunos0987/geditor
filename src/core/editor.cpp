#include "editor.h"

#include <algorithm>
#include <chrono>
#include <limits>

#include "commands.h"
#include "layout.h"
#include "menu.h"
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
// The scroll bar by character offset (B6) has at most this many positions.
constexpr size_t kScrollBarOffsetRange = 0xFFFF;

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
  // M5: inside the minibuffer and query-replace a key that their own keymap
  // does not have falls back to the global keymap without the Windows
  // compatible keys (an explicit null in their keymap does not fall
  // through). isearch has only its own keymap: handle_key_impl ends isearch
  // for any other key and looks it up again in global_in_modes_.
  if (minibuffer_active() || query_replace_active()) {
    const KeymapKind kind = minibuffer_active() ? KeymapKind::kMinibuffer : KeymapKind::kQueryReplace;
    const Keymap::Lookup r = keymap(kind).lookup(keys);
    if (r.result != Keymap::Result::kUndefined) return r;
    return global_in_modes_.lookup(keys);
  }
  if (isearch_active()) return keymap(KeymapKind::kIsearch).lookup(keys);
  return (sequence_in_mode_ ? global_in_modes_ : keymap(KeymapKind::kGlobal)).lookup(keys);
}

void Editor::handle_key(const Key& key) {
  message_.clear();
  message_is_settings_ = false;
  if (macro_recording_ && !macro_executing_) record_macro_key(key);
  // After kmacro-end-and-call-macro, `e` runs the macro again (as Emacs).
  const bool repeat = macro_repeat_ && !macro_executing_ && key == Key{U'e', 0} && pending_.empty() &&
                      !meta_pending_ && !prefix_.active && !quoted_insert_pending_;
  if (!macro_executing_) macro_repeat_ = false;
  if (repeat) {
    last_key_ = key;
    key_display_ = L"e";
    dispatch(command_binding("kmacro-end-and-call-macro"), false);
  } else {
    handle_key_impl(key);
  }
  after_input();
  run_pending_macro();
}

void Editor::handle_key_impl(const Key& key) {
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
  const Key typed = k;

  const Keymap::Lookup r =
      lookup_shift_translated(pending_, [this](const KeySequence& s) { return lookup(s); });
  if (r.result == Keymap::Result::kPrefix) return;
  sequence_in_mode_ = false;  // the key sequence is complete

  if (isearch_active() && r.result == Keymap::Result::kUndefined) {
    const Key last = pending_.back();
    const bool printable = pending_.size() == 1 && last.is_char() && (last.mods & ~kModShift) == 0 &&
                           is_self_insert_key(Key{last.code, 0}) && !(last.code >= 0xD800 && last.code <= 0xDFFF);
    if (printable) {
      // A character extends the search string (isearch-printing-char).
      pending_.clear();
      pending_display_.clear();
      prefix_ = PrefixArg();
      prefix_display_.clear();
      last_key_ = last;
      this_command_ = "isearch-printing-char";
      isearch_add_text(to_utf16((last.mods & kModShift) != 0 ? to_upper(last.code) : last.code));
      finish_command(main_);
      return;
    }
    // Any other key ends isearch at the current match and then runs as it
    // would have outside isearch (as in Emacs), but without the Windows
    // compatible keys (M5): C-S-z is C-z there, not undo.
    const KeySequence keys = std::move(pending_);
    pending_.clear();
    pending_display_.clear();
    isearch_end(false);
    for (const Key& again : keys) {
      sequence_in_mode_ = true;
      handle_key_impl(again);
    }
    return;
  }

  k = pending_.back();  // without S- when shift translation applied
  const bool shift_translated = (typed.mods & kModShift) != 0 && (k.mods & kModShift) == 0;
  const size_t length = pending_.size();
  std::wstring display = std::move(pending_display_);
  pending_.clear();
  pending_display_.clear();
  last_key_ = k;
  if (r.result == Keymap::Result::kCommand) {
    // A copy: the keymaps may be rebuilt (settings reload) while it runs.
    const Binding binding = *r.binding;
    key_display_ = std::move(display);
    shift_select_ = shift_translated;
    dispatch(binding, false);
    shift_select_ = false;
    return;
  }
  // query-replace types no characters into the buffer (M5): an unbound
  // character key is undefined there (not in a minibuffer opened during it).
  if (r.result == Keymap::Result::kUndefined && length == 1 && is_self_insert_key(k) &&
      !(query_replace_active() && !minibuffer_active())) {
    key_display_ = std::move(display);
    dispatch(command_binding("self-insert-command"), true);
    return;
  }
  prefix_ = PrefixArg();
  prefix_display_.clear();
  message_ = key == kCtrlG && r.result == Keymap::Result::kUndefined ? std::wstring(L"Quit")
                                                                     : display + L" is undefined";
  ++errors_;  // stops a keyboard macro run
  note_non_command();
}

bool Editor::key_is_bound(const Key& key) const {
  if (quoted_insert_pending_) return true;
  Key k = key;
  if (meta_pending_) k.mods |= kModMeta;
  KeySequence keys = pending_;
  keys.push_back(k);
  Keymap::Result r = lookup_shift_translated(keys, [this](const KeySequence& s) { return lookup(s); }).result;
  if (isearch_active() && r == Keymap::Result::kUndefined && keys.size() == 1) {
    // isearch takes every key: a character extends the search, another key
    // ends isearch and runs as outside it.
    if (is_self_insert_key(Key{k.code, 0}) && (k.mods & ~kModShift) == 0) return true;
    r = lookup_shift_translated(keys, [this](const KeySequence& s) { return global_in_modes_.lookup(s); }).result;
  }
  return r == Keymap::Result::kPrefix || r == Keymap::Result::kCommand;
}

void Editor::execute_command(std::string_view name) {
  message_.clear();
  message_is_settings_ = false;
  cancel_pending_input();
  key_display_.clear();
  end_search_modes();
  macro_repeat_ = false;
  dispatch(command_binding(std::string(name)), false);
  after_input();
  run_pending_macro();
}

bool Editor::execute_menu_item(const MenuItem& item) {
  if (item.kind != MenuItemKind::kCommand || !menu_item_enabled(*this, item)) return false;
  message_.clear();
  message_is_settings_ = false;
  // Prefix keys end; a prefix argument typed before the menu applies to the
  // item, as to a key bound to its command.
  pending_.clear();
  pending_display_.clear();
  meta_pending_ = false;
  quoted_insert_pending_ = false;
  key_display_.clear();
  macro_repeat_ = false;
  Binding binding;
  binding.calls.push_back(menu_item_call(item));
  dispatch(binding, false);
  after_input();
  run_pending_macro();
  return true;
}

void Editor::cancel_pending_input() {
  pending_.clear();
  pending_display_.clear();
  sequence_in_mode_ = false;
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
    ++errors_;
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
    ++errors_;
    note_non_command();
    return false;
  }
  main_.undo.begin_command(main_.point, false);
  mini_.undo.begin_command(mini_.point, false);
  bool ok = true;
  ++kill_defer_;  // kills in the sequence write the clipboard once (R1)
  for (int i = 0; ok && i < count; ++i) {
    for (const CommandCall& call : binding.calls) {
      if (!run_call(call, PrefixArg(), false, false)) {
        ok = false;
        break;
      }
    }
  }
  if (--kill_defer_ == 0 && !flush_kills()) ok = false;
  main_.undo.end_command(main_.point);
  mini_.undo.end_command(mini_.point);
  return ok;
}

bool Editor::run_call(const CommandCall& call_in, const PrefixArg& prefix, bool typing, bool own_group) {
  const CommandInfo* info = find_command(call_in.name);
  if (info == nullptr || info->fn == nullptr) {
    message_ = info == nullptr ? L"Unknown command: " + widen(call_in.name)
                               : widen(call_in.name) + L" is not implemented yet";
    ++errors_;
    if (own_group) note_non_command();
    return false;
  }
  // goto-line with a prefix argument takes the line from it (as Emacs:
  // C-u 5 M-g g goes to line 5) instead of asking.
  CommandCall filled;
  const CommandCall* use = &call_in;
  if (call_in.name == "goto-line" && call_in.args.empty() && prefix.active) {
    filled = call_in;
    CommandArg line;
    line.type = CommandArg::Type::kInteger;
    line.integer = prefix.count();
    filled.args.push_back(std::move(line));
    use = &filled;
  }
  const CommandCall& call = *use;
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
  // Shift selection (PLAN 4.4): only for a movement command run by a key of
  // its own (not inside a command sequence).
  const bool movement = is_shift_select_command(info->name);
  // A command that is no kill ends the kills in a row: their waiting
  // clipboard write is made first, so that the clipboard holds them before
  // anything (yank, copy, quit, ...) reads or replaces it (retry R1).
  if (kill_pending_ && !info->prefix_argument && !is_kill_command(info->name)) flush_kills();
  if (own_group && movement) shift_select(b, shift_select_);
  shift_select_ = false;
  if (own_group) b.undo.begin_command(b.point, typing);
  info->fn(*this);
  // The group keeps the point after finish_command's cluster snapping, so
  // that redo puts point where the command left it (deferred fix E1: after a
  // joining insertion, the end of the joined cluster).
  finish_command(b);
  if (own_group) b.undo.end_command(b.point);
  // A region made otherwise (C-SPC, C-x C-x, C-x h, select-all, a kill or
  // an edit that ended it) is no shift selection any more.
  if (!movement && !info->prefix_argument) {
    for (Buffer* x : {&main_, &mini_}) {
      if (!x->mark_active || is_mark_command(info->name)) x->shift_mark = false;
    }
  }
  // The text of a kill sequence is kept only while kills follow each other
  // (the prefix argument commands do not end a sequence). A waiting
  // clipboard write was made before this command ran (above).
  if (!info->prefix_argument && !is_kill_command(info->name) && !kill_pending_) clear_kills();
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
  // find-file-with-encoding: the file name starts like find-file's.
  const bool file_name = reading_call_.name == "find-file-with-encoding";
  start_minibuffer(MinibufferPurpose::kCommandArgument, info.params.front().prompt,
                   file_name ? default_directory() : std::wstring());
}

bool Editor::reads_file_name() const {
  return purpose_ == MinibufferPurpose::kCommandArgument && reading_call_.name == "find-file-with-encoding" &&
         reading_call_.args.empty();
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
    std::wstring prompt = info->params[call.args.size()].prompt;
    // query-replace names the string being replaced, as in Emacs:
    // "Query replace foo with: ".
    if (call.name == "query-replace" || call.name == "query-replace-regexp") {
      prompt = std::wstring(call.name == "query-replace" ? L"Query replace " : L"Query replace regexp ") +
               call.args.front().text + L" with: ";
    }
    start_minibuffer(MinibufferPurpose::kCommandArgument, std::move(prompt), L"");
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
  // The same global keymap without the Windows compatible keys (its errors
  // are those of the global keymap, already reported).
  global_in_modes_ = build_keymap(emacs_layer(KeymapKind::kGlobal), {},
                                  config.keys[static_cast<size_t>(KeymapKind::kGlobal)], nullptr);
}

bool Editor::apply_settings_text(std::string_view utf8) {
  cancel_pending_input();
  ConfigParse parsed = parse_config(utf8);
  std::vector<Diagnostic> errors = std::move(parsed.errors);
  const CaseFold case_fold_before = config_.case_fold;
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
  isearch_after_case_fold_change(case_fold_before);
  return parsed.syntax_ok;
}

void Editor::isearch_after_case_fold_change(CaseFold before) {
  // S5: a reload that changes search.case_fold for the isearch pattern (not
  // overridden with M-c) searches again at once, as M-c does: the match, the
  // highlights, the failing state and the prompt follow the new rule.
  // The states that DEL goes back to are brought up to date when DEL
  // returns to them (isearch_delete_char; retry R4 / Q4), not all here.
  if (!isearch_active()) return;
  const IsearchStep& s = isearch_.now;
  if (s.pattern.empty() || s.case_override >= 0 ||
      search_case_sensitive(before, s.pattern, s.regexp) ==
          search_case_sensitive(config_.case_fold, s.pattern, s.regexp)) {
    return;
  }
  const uint64_t errors = errors_;
  const std::wstring shown = message_;
  message_.clear();
  isearch_search(false);
  errors_ = errors;
  if (message_is_settings_) {
    message_ = shown;  // the settings errors stay in the echo area
  } else {
    const std::wstring prompt = isearch_prompt();
    message_ = message_.empty() ? prompt : prompt + (prompt.ends_with(L' ') ? L"[" : L" [") + message_ + L"]";
  }
  ensure_point_visible();
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
        const CaseFold case_fold_before = config_.case_fold;
        config_ = Config();
        apply_view_settings();
        settings_errors_.clear();
        rebuild_keymaps(config_, nullptr);
        show(L"Settings file removed; using defaults");
        isearch_after_case_fold_change(case_fold_before);
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
    // An edit can join the clusters around point or the mark (deleting a TAB
    // between "e" and U+0301, yanking text that starts with a combining
    // mark). Point right after inserted text goes forward to the end of the
    // cluster the insertion joined (a ZWJ typed between two emoji, a virama
    // or a Prepend character typed before text): typing goes on after it, in
    // order (decision G4). Otherwise point and the mark go back to the
    // boundary at or before them (decision G3).
    const bool after_insert = b->inserted_in_command && b->point == b->insert_end;
    const size_t start = cluster_start(b->text, b->point);
    if (start != b->point) b->point = after_insert ? next_cluster_offset(b->text, b->point) : start;
    if (b->has_mark) b->mark = cluster_start(b->text, b->mark);
    b->inserted_in_command = false;
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
  finish_command(b);
  b.undo.end_command(b.point);  // after the snapping (E1)
}

void Editor::insert_text(std::wstring_view text) {
  message_.clear();
  message_is_settings_ = false;
  if (!macro_executing_) {
    macro_repeat_ = false;
    if (macro_recording_ && !text.empty()) record_macro_text(text);
  }
  cancel_pending_input();
  quoted_insert_pending_ = false;
  if (text.empty()) return;
  if (isearch_active()) {
    // IME text extends the search string (one input for DEL).
    this_command_ = "isearch-printing-char";
    isearch_add_text(normalize_line_ends(text));
    finish_command(main_);
    after_input();
    return;
  }
  if (query_replace_active() && !minibuffer_active()) {
    // query-replace types nothing into the buffer (a minibuffer opened
    // during it, such as M-x, takes text as usual).
    message_ = L"Text cannot be typed during query-replace";
    after_input();
    return;
  }
  this_command_ = "insert-text";
  Buffer& b = current();
  main_.edited_in_command = false;
  mini_.edited_in_command = false;
  b.undo.begin_command(b.point, false);
  insert(b, normalize_line_ends(text));
  finish_command(b);
  b.undo.end_command(b.point);  // after the snapping (E1)
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
  const bool is_modified = modified();
  const wchar_t* flag = main_.read_only ? (is_modified ? L"%*" : L"%%") : (is_modified ? L"**" : L"--");
  // "Def" while a keyboard macro is being recorded (PLAN 4.6).
  return std::wstring(L"-") + flag + L" " + buffer_name() + L"  " + encoding_text() + L" " + eol_text() +
         L"  L" + std::to_wstring(point_line() + 1) + L" C" + std::to_wstring(point_column()) + L"  wrap:" +
         wrap_mode_name(wrap_mode()) + (macro_recording_ ? L"  Def" : L"");
}

std::wstring Editor::encoding_text() const {
  std::wstring text = encoding_name(main_.format);
  if (main_.format.undecodable) text += L'!';
  return text;
}

size_t Editor::other_line_ends() const {
  return main_.text.line_feed_count() - main_.text.eol_count(main_.format.eol);
}

std::wstring Editor::eol_text() const {
  const std::wstring name = eol_name(main_.format.eol);
  return other_line_ends() > 0 ? L"Mixed(" + name + L")" : name;
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
  // A buffer that is still new and empty takes new_file.eol (decision F1)
  // and new_file.encoding (unless its encoding was chosen).
  if (main_.text.length() == 0 && main_.undo.undo_count() == 0 && main_.undo.redo_count() == 0 &&
      !(disk_known_ && disk_.exists)) {
    main_.format.eol = new_file_eol();
    if (!encoding_named_) {
      const FileFormat format = new_file_format();
      main_.format.codec = format.codec;
      main_.format.bom = format.bom;
    }
  }
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
  const PieceTree& t = main_.text;
  const size_t lines = t.line_count();
  s.max = lines - 1;
  s.page = static_cast<size_t>(visible_lines());
  s.pos = std::min(top_.line, s.max);
  if (lines > s.page) return s;  // the logical lines do not fit: by lines
  // Few logical lines (B6): when their rows do not fit either, the bar
  // works by character offset. Only the rows on screen are laid out (from
  // top_, or from the start to see whether everything fits), never a whole
  // long line.
  const WrapLayout& l = layout();
  RowPos top = top_;
  if (top.line >= lines || !l.has_row(t, top.line, top.row)) top = RowPos{};
  RowPos end = top;
  const size_t moved = l.forward_rows(t, &end, s.page);
  if (top == RowPos{} && moved < s.page) return s;  // everything fits: disabled
  const size_t from = l.row_start(t, top.line, top.row);
  const size_t to = moved == s.page ? l.row_start(t, end.line, end.row) : t.length();
  // Positions count `unit` characters each, so that the range fits the 16
  // bits of a WM_VSCROLL message (a posted SB_THUMBTRACK works as a drag).
  s.by_offset = true;
  s.unit = t.length() / kScrollBarOffsetRange + 1;
  s.max = t.length() / s.unit;
  s.page = std::max<size_t>(1, (to - from) / s.unit);
  s.pos = from / s.unit;
  return s;
}

void Editor::scroll_to_bar_position(size_t pos) {
  const ScrollBarState s = scroll_bar();
  if (!s.by_offset) {
    scroll_to_line(pos);
    return;
  }
  end_macro_repeat();
  main_.undo.begin_command(main_.point, false);
  const WrapLayout& l = layout();
  const PieceTree& t = main_.text;
  const size_t visible = static_cast<size_t>(visible_lines());
  // The thumb at the bottom (pos + page past the range, as SB_BOTTOM): the
  // last screen. Otherwise the row of the offset goes to the top; when the
  // rows from there do not fill the window, the last screen too (from where
  // that walk ended).
  RowPos top;
  if (pos + s.page > s.max) {
    top = l.last_row(t);
    l.backward_rows(t, &top, visible - 1);
  } else {
    top = l.row_of(t, clamp_main_offset(pos * s.unit), nullptr);
    RowPos bottom = top;
    if (l.forward_rows(t, &bottom, visible - 1) < visible - 1) {
      top = bottom;
      l.backward_rows(t, &top, visible - 1);
    }
  }
  top_ = top;
  keep_point_on_screen();
  main_.undo.end_command(main_.point);
  last_command_ = "scroll-bar";
}

void Editor::scroll_by_rows(long long rows) {
  // A scroll bar action is a command of its own, like a click (set_point).
  end_macro_repeat();
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
  end_macro_repeat();
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

size_t Editor::clamp_main_offset(size_t offset) const {
  // Decision G3: a position from outside snaps to the cluster boundary at or
  // before it (this covers the halves of a surrogate pair too).
  return cluster_start(main_.text, std::min(offset, main_.text.length()));
}

void Editor::begin_mouse_command() {
  message_.clear();
  message_is_settings_ = false;
  cancel_pending_input();
  quoted_insert_pending_ = false;
  // A click is no shift selection, and `e` after C-x e is no longer a repeat.
  main_.shift_mark = false;
  end_macro_repeat();
  // As set_point: the typing undo group and the goal column end here.
  main_.undo.begin_command(main_.point, false);
  main_.undo.end_command(main_.point);
}

size_t Editor::offset_at_screen(long long row, int column) const {
  const WrapLayout& l = layout();
  const PieceTree& t = main_.text;
  RowPos pos = top_;
  if (row > 0) {
    if (l.forward_rows(t, &pos, static_cast<size_t>(row)) < static_cast<size_t>(row)) return t.length();
  } else if (row < 0) {
    l.backward_rows(t, &pos, static_cast<size_t>(-row));
  }
  return l.offset_at(t, pos.line, pos.row, std::max(0, column));
}

size_t Editor::offset_at_click(long long row, int column) {
  return offset_at_screen(std::clamp<long long>(row, 0, visible_lines() - 1), column);
}

bool Editor::mouse_press(size_t offset, bool shift) {
  // M5: a click ends isearch at its match (as Emacs); query-replace ignores
  // clicks, like the minibuffer.
  if (minibuffer_active() || query_replace_active()) return false;
  if (isearch_active()) isearch_end(false);
  begin_mouse_command();
  offset = clamp_main_offset(offset);
  if (shift) {
    if (!(main_.mark_active && main_.has_mark)) {
      main_.has_mark = true;
      main_.mark = main_.point;
    }
    main_.mark_active = true;
    mouse_anchor_ = main_.mark;
    mouse_dragged_ = true;
    last_command_ = "mouse-extend-region";
  } else {
    main_.mark_active = false;
    mouse_anchor_ = offset;
    mouse_dragged_ = false;
    last_command_ = "mouse-set-point";
  }
  main_.point = offset;
  mouse_pressed_ = true;
  ensure_point_visible();
  return true;
}

bool Editor::mouse_drag(size_t offset) {
  if (!mouse_pressed_ || minibuffer_active()) return false;
  offset = clamp_main_offset(offset);
  if (!mouse_dragged_ && offset == mouse_anchor_) return true;
  mouse_dragged_ = true;
  main_.has_mark = true;
  main_.mark = mouse_anchor_;
  main_.mark_active = true;
  main_.point = offset;
  last_command_ = "mouse-drag-region";
  ensure_point_visible();
  return true;
}

bool Editor::mouse_double_click(size_t offset) {
  if (minibuffer_active() || query_replace_active()) return false;
  if (isearch_active()) isearch_end(false);
  begin_mouse_command();
  offset = clamp_main_offset(offset);
  size_t from = offset;
  size_t to = offset;
  word_bounds_at(main_.text, offset, &from, &to);
  main_.has_mark = true;
  main_.mark = from;
  main_.point = to;
  main_.mark_active = from != to;
  mouse_pressed_ = false;
  last_command_ = "mouse-select-word";
  ensure_point_visible();
  return true;
}

void Editor::set_point(size_t offset) {
  end_macro_repeat();
  offset = clamp_main_offset(offset);
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
  // New line ends get the default line end (PLAN 2 "newly entered line ends").
  const PieceList pieces = b.text.insert(offset, text, b.format.eol);
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
  b.inserted_in_command = true;
  b.insert_end = b.point;
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

void Editor::set_eol(Eol eol) {
  Buffer& b = main_;
  if (!check_writable(b)) return;
  const size_t others = b.text.line_feed_count() - b.text.eol_count(eol);
  if (others == 0 && b.format.eol == eol) {
    message_ = std::wstring(L"Line endings are already ") + eol_name(eol);
    return;
  }
  PieceList before;
  PieceList after;
  if (others > 0) {
    // The same text in the same pieces, with the new line end: no text is
    // copied, and undo puts the old pieces back.
    before = b.text.pieces();
    after = before;
    for (Piece& p : after) p.eol = eol;
    b.text.assign_pieces(after);
  }
  b.undo.record_eol_change(std::move(before), std::move(after), b.format.eol, eol);
  b.format.eol = eol;
  b.edited_in_command = true;
  message_ = std::wstring(L"Line endings: ") + eol_name(eol);
}

void Editor::toggle_read_only() {
  main_.read_only = !main_.read_only;
  message_ = main_.read_only ? L"Read-only mode enabled" : L"Read-only mode disabled";
}

bool Editor::undo(Buffer& b) {
  MarkFollower follower(b, &b == &main_ ? &wrap_ : nullptr);
  size_t point = b.point;
  if (!b.undo.undo(b.text, &follower, &point, &b.format.eol)) return false;
  b.point = std::min(point, b.text.length());
  b.edited_in_command = true;
  return true;
}

bool Editor::redo(Buffer& b) {
  MarkFollower follower(b, &b == &main_ ? &wrap_ : nullptr);
  size_t point = b.point;
  if (!b.undo.redo(b.text, &follower, &point, &b.format.eol)) return false;
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
  } else if (reads_file_name()) {
    r = complete_file_name(input, host_);
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

// ---- Search and replace (M5) ----

namespace {

std::wstring occurrences(size_t n) {
  return L"Replaced " + std::to_wstring(n) + (n == 1 ? L" occurrence" : L" occurrences");
}

}  // namespace

void Editor::after_input() {
  if (query_replace_active() && !minibuffer_active() &&
      (main_.text.revision() != replace_.revision || main_.point != replace_.point)) {
    // Another command (through the global keymap) edited the text or moved
    // point: the current match is the next one from point.
    query_replace_next(main_.point, false);
  }
  if (!(isearch_active() || query_replace_active()) || minibuffer_active() || message_is_settings_) return;
  const std::wstring prompt = isearch_active() ? isearch_prompt() : query_replace_prompt();
  if (message_ == prompt) return;
  if (message_.empty()) {
    message_ = prompt;
  } else {
    message_ = prompt + (prompt.ends_with(L' ') ? L"[" : L" [") + message_ + L"]";
  }
}

std::wstring Editor::isearch_prompt() const {
  const IsearchStep& s = isearch_.now;
  // Emacs words: "Failing wrapped regexp I-search backward: ".
  std::wstring p;
  if (s.failing) p += L"failing ";
  if (s.wrapped) p += L"wrapped ";
  if (s.case_override == 1) p += L"case-sensitive ";
  if (s.case_override == 0) p += L"case-insensitive ";
  if (s.regexp) p += L"regexp ";
  p += L"I-search";
  if (!s.forward) p += L" backward";
  p += L": ";
  if (p[0] >= L'a' && p[0] <= L'z') p[0] = static_cast<wchar_t>(p[0] - L'a' + L'A');
  return p + s.pattern;
}

std::wstring Editor::query_replace_prompt() const {
  return std::wstring(replace_.regexp ? L"Query replacing regexp " : L"Query replacing ") + replace_.from +
         L" with " + replace_.to + L": ";
}

SearchMatch Editor::snap_match(const SearchMatch& m) const {
  const PieceTree& t = main_.text;
  const size_t start = cluster_start(t, std::min(m.start, t.length()));
  size_t end = std::min(m.end, t.length());
  const size_t end_start = cluster_start(t, end);
  if (end_start != end) end = next_cluster_offset(t, end_start);
  return SearchMatch{start, std::max(start, end)};
}

Editor::SearchHighlights Editor::search_highlights(int rows) const {
  SearchHighlights h;
  SearchMatch current;
  if (isearch_active()) {
    const IsearchStep& s = isearch_.now;
    if (s.pattern.empty() || s.invalid) return h;
    h.has_current = s.has_match && !s.failing;
    current = s.match;
  } else if (query_replace_active()) {
    h.has_current = true;
    current = replace_.match;
  } else {
    return h;
  }
  if (!matcher_.valid()) return SearchHighlights();
  const WrapLayout& l = layout();
  const PieceTree& t = main_.text;
  RowPos top = top_;
  if (top.line >= t.line_count() || !l.has_row(t, top.line, top.row)) top = RowPos{};
  const size_t from = l.row_start(t, top.line, top.row);
  size_t to = t.length();
  RowPos end = top;
  const size_t wanted = static_cast<size_t>(std::max(1, rows));
  if (l.forward_rows(t, &end, wanted) == wanted) to = l.row_start(t, end.line, end.row);
  matcher_.find_all(t, from, to, kMaxHighlights, &h.matches);
  for (SearchMatch& m : h.matches) m = snap_match(m);
  if (h.has_current) h.current = snap_match(current);
  return h;
}

void Editor::isearch_start(bool forward, bool regexp) {
  if (query_replace_active() && !minibuffer_active()) {
    // S6: no minibuffer is open; C-s / C-r are not query-replace answers
    // (Emacs), and query-replace goes on.
    fail(L"I-search cannot be started during query-replace");
    return;
  }
  if (input_mode_active()) {
    fail(kRecursiveMinibufferMessage);
    return;
  }
  isearch_ = IsearchState();
  isearch_.active = true;
  isearch_.start_point = main_.point;
  isearch_.start_top = top_;
  IsearchStep& s = isearch_.now;
  s.forward = forward;
  s.regexp = regexp;
  s.origin = main_.point;
  s.point = main_.point;
}

void Editor::isearch_push() {
  isearch_.history.push_back(isearch_.now);
}

bool Editor::isearch_sensitive(const IsearchStep& s) const {
  return s.case_override >= 0 ? s.case_override == 1 : search_case_sensitive(config_.case_fold, s.pattern, s.regexp);
}

bool Editor::isearch_prepare() {
  IsearchStep& s = isearch_.now;
  const bool sensitive = isearch_sensitive(s);
  s.invalid = !matcher_.set(s.pattern, s.regexp, sensitive);
  if (s.invalid) message_ = matcher_.error();
  return !s.invalid;
}

void Editor::isearch_search(bool repeat) {
  IsearchStep& s = isearch_.now;
  if (s.pattern.empty()) {
    s.invalid = false;
    return;
  }
  if (!isearch_prepare()) return;  // point stays at the last match
  s.searched_sensitive = isearch_sensitive(s) ? 1 : 0;
  const PieceTree& t = main_.text;
  if (repeat && s.failing) {
    // A failing search wraps around (Emacs: "Wrapped I-search").
    s.wrapped = true;
    s.origin = s.forward ? 0 : t.length();
    s.has_match = false;
    repeat = false;
  }
  SearchMatch m;
  SearchResult r = SearchResult::kNotFound;
  if (s.forward) {
    if (repeat && s.has_match) {
      // 2026-11-18 (decision 4 a): a repeat returns only a match that moves
      // the cursor, one whose displayed end (snap_match, where the cursor
      // goes) is after the current match's displayed end. The matches that
      // end inside the same cluster are skipped, each searched from the end
      // of the one before as the repeat does from the current match.
      // shown_end is a cluster boundary, so a match's displayed end is at or
      // before it exactly when its raw end is (no cluster walk per match).
      const size_t shown_end = snap_match(s.match).end;
      r = matcher_.find_forward(t, s.match.end, true, &m);
      while (!raw_repeat_ && r == SearchResult::kFound && m.end <= shown_end) {
        r = matcher_.find_forward(t, m.end, true, &m);
      }
    } else {
      r = matcher_.find_forward(t, s.has_match ? s.match.start : s.origin, false, &m);
    }
  } else if (repeat && s.has_match && old_backward_repeat_) {
    r = matcher_.find_backward(t, s.match.start, s.match.start, &m);
    if (r == SearchResult::kFound && m == s.match) {
      // The same empty match: one further back.
      r = s.match.start > 0 ? matcher_.find_backward(t, s.match.start - 1, s.match.start, &m)
                            : SearchResult::kNotFound;
    }
  } else if (repeat && s.has_match) {
    // [C12] (2026-11-18): a repeated C-r never returns a match that starts
    // at the current match's start; the next one starts before it (the limit
    // stays the current match's start, S1). Before, the repeat first looked
    // for a match up to that start itself: after a non-empty match of a
    // pattern that can also match empty (.*, o*) that gave the empty match
    // at the same start, so the C-r after each non-empty match stood still
    // (every second C-r for .*; the always empty \b and () took the step
    // back every time and did not stand still). A match that cannot
    // be empty ends at or before the limit only by starting before it, so
    // its answers are unchanged.
    // 2026-11-18 (decision 4 a): only a match whose displayed start
    // (snap_match, where the cursor goes) is before the current match's
    // displayed start is returned; the others (starting inside the same
    // cluster) are skipped, each with the same step from the skipped match.
    // shown_start is a cluster boundary, so a match's displayed start is
    // before it exactly when its raw start is.
    const size_t shown_start = snap_match(s.match).start;
    SearchMatch from = s.match;
    for (;;) {
      r = from.start > 0 ? matcher_.find_backward(t, from.start - 1, from.start, &m) : SearchResult::kNotFound;
      if (raw_repeat_ || r != SearchResult::kFound || m.start < shown_start) break;
      from = m;
    }
  } else {
    // From the origin, a match must end there; a longer pattern keeps the
    // current match's start (or one before it) and may end anywhere on its
    // line, as the forward search does (M5 retry R2: a bound at the origin
    // made "C-s fo C-s C-r o" fail after a turn-around).
    r = s.has_match ? matcher_.find_backward(t, s.match.start, t.length(), &m)
                    : matcher_.find_backward(t, s.origin, s.origin, &m);
  }
  if (r == SearchResult::kFound) {
    s.has_match = true;
    s.match = m;
    s.failing = false;
    const SearchMatch snapped = snap_match(m);
    main_.point = s.forward ? snapped.end : snapped.start;
  } else {
    s.failing = true;
    ++errors_;  // a failing search stops a keyboard macro run (as Emacs)
    if (r == SearchResult::kError) message_ = matcher_.error();
  }
  s.point = main_.point;
}

void Editor::isearch_add_text(std::wstring_view text) {
  isearch_push();
  isearch_.now.pattern += text;
  isearch_search(false);
}

void Editor::isearch_repeat(bool forward) {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  isearch_push();
  IsearchStep& s = isearch_.now;
  if (s.pattern.empty()) {
    // C-s C-s: the last search string.
    s.forward = forward;
    const std::wstring& last = last_search_[s.regexp ? 1 : 0];
    if (last.empty()) {
      message_ = L"No previous search string";
      return;
    }
    s.pattern = last;
    isearch_search(false);
    return;
  }
  if (s.forward != forward) {
    // Turning around: first to the other end of the current match.
    s.forward = forward;
    if (s.has_match && !s.failing && !s.invalid) {
      const SearchMatch snapped = snap_match(s.match);
      main_.point = forward ? snapped.end : snapped.start;
      s.point = main_.point;
      // The search now runs from here in the new direction (R2).
      s.origin = main_.point;
      return;
    }
    s.has_match = false;
    s.failing = false;
    s.origin = main_.point;
    isearch_search(false);
    return;
  }
  isearch_search(true);
}

void Editor::isearch_delete_char() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  if (isearch_.history.empty()) return;
  isearch_.now = std::move(isearch_.history.back());
  isearch_.history.pop_back();
  main_.point = std::min(isearch_.now.point, main_.text.length());
  IsearchStep& s = isearch_.now;
  if (s.pattern.empty()) return;
  if (s.searched_sensitive >= 0 && s.searched_sensitive != (isearch_sensitive(s) ? 1 : 0)) {
    // Retry R4 / Q4: the step was searched before a settings reload changed
    // search.case_fold for its pattern; searched again now, from where it
    // stood, as the reload did for the current step.
    const uint64_t errors = errors_;
    isearch_search(false);
    errors_ = errors;  // going back is no failing input
    return;
  }
  isearch_prepare();
}

void Editor::isearch_delete_pattern() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  if (isearch_.now.pattern.empty()) return;
  // One input as a typed character: DEL goes back to the step before. The
  // search string is not kept for the next empty C-s (isearch_end keeps the
  // pattern it ends with only).
  isearch_push();
  IsearchStep& s = isearch_.now;
  s.pattern.clear();
  s.has_match = false;
  s.match = SearchMatch();
  s.failing = false;
  s.wrapped = false;
  s.invalid = false;
  s.searched_sensitive = -1;
  main_.point = std::min(isearch_.start_point, main_.text.length());
  s.origin = main_.point;
  s.point = main_.point;
}

void Editor::isearch_toggle_regexp() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  isearch_push();
  isearch_.now.regexp = !isearch_.now.regexp;
  isearch_search(false);
}

void Editor::isearch_toggle_case_fold() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  isearch_push();
  IsearchStep& s = isearch_.now;
  const bool sensitive =
      s.case_override >= 0 ? s.case_override == 1 : search_case_sensitive(config_.case_fold, s.pattern, s.regexp);
  s.case_override = sensitive ? 0 : 1;
  isearch_search(false);
}

void Editor::isearch_end(bool abort) {
  const IsearchStep s = isearch_.now;
  if (!s.pattern.empty()) last_search_[s.regexp ? 1 : 0] = s.pattern;
  const size_t start = isearch_.start_point;
  const RowPos top = isearch_.start_top;
  isearch_ = IsearchState();
  if (abort) {
    main_.point = std::min(start, main_.text.length());
    top_ = top;
    ensure_point_visible();
    return;
  }
  // Emacs: the mark goes where the search started (unless a region is
  // active), so that C-x C-x returns there.
  if (main_.point != start && !(main_.mark_active && main_.has_mark)) {
    main_.has_mark = true;
    main_.mark = std::min(start, main_.text.length());
    message_ = L"Mark saved where search started";
  }
}

void Editor::isearch_exit() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  isearch_end(false);
}

void Editor::isearch_abort() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  isearch_end(true);
  fail(L"Quit");
}

void Editor::isearch_query_replace() {
  if (!isearch_active()) {
    fail(L"Not in isearch");
    return;
  }
  const IsearchStep s = isearch_.now;
  // query-replace starts at the current match (Emacs: its beginning).
  const size_t at = s.has_match && !s.failing ? snap_match(s.match).start : main_.point;
  if (!s.pattern.empty()) last_search_[s.regexp ? 1 : 0] = s.pattern;
  isearch_ = IsearchState();
  main_.point = at;
  const char* name = s.regexp ? "query-replace-regexp" : "query-replace";
  if (s.pattern.empty()) {
    start_reading_arguments(*find_command(name), PrefixArg());
    return;
  }
  CommandArg from;
  from.text = s.pattern;
  CommandCall call{name, {std::move(from)}};
  start_minibuffer(MinibufferPurpose::kCommandArgument,
                   std::wstring(s.regexp ? L"Query replace regexp " : L"Query replace ") + s.pattern + L" with: ",
                   L"");
  reading_call_ = std::move(call);
  minibuffer_prefix_ = PrefixArg();
}

void Editor::query_replace(bool regexp, std::wstring_view from, std::wstring_view to) {
  if (input_mode_active()) {
    fail(kRecursiveMinibufferMessage);
    return;
  }
  if (!check_writable(main_)) return;
  if (from.empty()) {
    fail(L"The string to replace is empty");
    return;
  }
  const bool sensitive = search_case_sensitive(config_.case_fold, from, regexp);
  if (!matcher_.set(from, regexp, sensitive)) {
    fail(L"Invalid regexp: " + matcher_.error());
    return;
  }
  replace_ = ReplaceState();
  replace_.active = true;
  replace_.regexp = regexp;
  replace_.from = from;
  replace_.to = to;
  replace_.start_point = main_.point;
  replace_.first_group = main_.undo.next_group_id();
  replace_.merge_from = replace_.first_group;
  replace_.merge_point = replace_.start_point;
  query_replace_next(main_.point, false);
}

void Editor::query_replace_next(size_t from, bool not_empty_at_from, bool show) {
  SearchMatch m;
  const SearchResult r =
      matcher_.find_forward(main_.text, std::min(from, main_.text.length()), not_empty_at_from, &m, true);
  if (r != SearchResult::kFound) {
    query_replace_finish(r == SearchResult::kError ? L"Search stopped (" + matcher_.error() + L"); " +
                                                         occurrences(query_replace_standing())
                                                   : occurrences(query_replace_standing()));
    return;
  }
  replace_.match = m;
  main_.point = snap_match(m).end;
  replace_.revision = main_.text.revision();
  replace_.point = main_.point;
  if (show) ensure_point_visible();
}

void Editor::query_replace_finish(std::wstring message) {
  if (!replace_.active) return;
  replace_.active = false;
  // Every replacement of this run that stands is one undo unit; undo puts
  // point where query-replace started. The groups are found by id (S2): an
  // undo of older history during the run neither splits the unit nor joins
  // an older edit to it.
  main_.undo.merge_groups_from_id(replace_.merge_from, replace_.merge_point);
  // S4: the edit replaced exactly the matched units, which may end inside a
  // grapheme cluster (the highlight is widened to clusters, the edit is not).
  // Point stays after the replaced text: at the end of that cluster, not
  // back before it (finish_command would move it back).
  const size_t point = std::min(main_.point, main_.text.length());
  if (cluster_start(main_.text, point) != point) main_.point = next_cluster_offset(main_.text, point);
  message_ = std::move(message);
}

void Editor::query_replace_count(size_t n) {
  if (!main_.undo.add_count(n)) replace_.unrecorded += n;
}

size_t Editor::query_replace_standing() const {
  return main_.undo.count_from_id(replace_.first_group) + replace_.unrecorded;
}

bool Editor::query_replace_replace_current() {
  const SearchMatch m = replace_.match;
  std::wstring text;
  if (!matcher_.expand(replace_.to, &text)) {
    query_replace_finish(L"Invalid replacement: " + matcher_.error() + L"; " + occurrences(query_replace_standing()));
    failed_ = true;
    ++errors_;
    return false;
  }
  if (main_.read_only) {
    // read-only-mode was turned on during query-replace (C-x C-q through
    // the global keymap): it ends, keeping what it replaced.
    query_replace_finish(L"Buffer is read-only");
    failed_ = true;
    ++errors_;
    return false;
  }
  replace(main_, m.start, m.end, text);
  query_replace_count(1);
  main_.point = m.start + text.size();
  return true;
}

void Editor::query_replace_act(bool and_exit) {
  if (!query_replace_active()) {
    fail(L"Not in query-replace");
    return;
  }
  const SearchMatch m = replace_.match;
  if (!query_replace_replace_current()) return;
  if (and_exit) {
    query_replace_finish(occurrences(query_replace_standing()));
    return;
  }
  query_replace_next(main_.point, m.start == m.end);
}

void Editor::query_replace_skip() {
  if (!query_replace_active()) {
    fail(L"Not in query-replace");
    return;
  }
  const SearchMatch m = replace_.match;
  query_replace_next(m.end, m.start == m.end);
}

void Editor::query_replace_all() {
  if (!query_replace_active()) {
    fail(L"Not in query-replace");
    return;
  }
  // M5 retry R3 (profiled): searching again after each replacement gathered
  // the whole edited line (O(line) per match) and kept point on screen per
  // match (a walk of the wrapped line). Now every remaining match of a line
  // is found on one copy of the line and replaced in one edit, and the view
  // follows point once, when the command ends (finish_command).
  while (query_replace_active()) {
    if (main_.read_only) {
      query_replace_finish(L"Buffer is read-only");
      failed_ = true;
      ++errors_;
      return;
    }
    LineReplacement line;
    const bool ok = matcher_.expand_rest_of_line(replace_.to, &line);
    if (line.count > 0) {
      replace(main_, line.from, line.to, line.text);
      query_replace_count(line.count);
      main_.point = line.from + line.point;
    }
    if (!ok) {
      query_replace_finish(L"Invalid replacement: " + matcher_.error() + L"; " +
                           occurrences(query_replace_standing()));
      failed_ = true;
      ++errors_;
      return;
    }
    query_replace_next(main_.point, line.last_empty, false);
  }
}

void Editor::query_replace_exit() {
  if (!query_replace_active()) {
    fail(L"Not in query-replace");
    return;
  }
  query_replace_finish(occurrences(query_replace_standing()));
}

void Editor::goto_line(int64_t line) {
  Buffer& b = current();
  const size_t lines = b.text.line_count();
  const size_t index = line <= 1 ? 0 : static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(line) - 1, lines - 1));
  // Emacs pushes the mark at the old position unless a region is active.
  if (!b.mark_active) {
    b.has_mark = true;
    b.mark = b.point;
    message_ = L"Mark set";
  }
  b.point = b.text.line_start(index);
}

void Editor::end_search_modes() {
  if (isearch_active()) isearch_end(false);
  if (query_replace_active()) query_replace_finish(occurrences(query_replace_standing()));
}

// ---- Shift selection (M6, PLAN 4.4) ----

void Editor::shift_select(Buffer& b, bool shifted) {
  if (shifted) {
    // Emacs shift-select: a region already active (shifted or not) is kept
    // and extended; otherwise a temporary mark goes where point is now.
    if (!(b.mark_active && b.has_mark)) {
      b.has_mark = true;
      b.mark = b.point;
      b.mark_active = true;
      b.shift_mark = true;
    }
    return;
  }
  if (b.shift_mark) {
    b.mark_active = false;
    b.shift_mark = false;
  }
}

// ---- Keyboard macros (M6) ----

bool Editor::macro_sequence_starts() const {
  return pending_.empty() && !meta_pending_ && !prefix_.active && !quoted_insert_pending_ && !minibuffer_active();
}

void Editor::record_macro_key(const Key& key) {
  if (key == kCtrlG && !quoted_insert_pending_) {
    // C-g ends the recording (Emacs: quitting ends the definition); the
    // last macro stays. The key then quits as usual.
    macro_recording_ = false;
    macro_recorded_.clear();
    return;
  }
  if (macro_sequence_starts()) macro_sequence_start_ = macro_recorded_.size();
  MacroEvent e;
  e.key = key;
  macro_recorded_.push_back(std::move(e));
}

void Editor::record_macro_text(std::wstring_view text) {
  if (macro_sequence_starts()) macro_sequence_start_ = macro_recorded_.size();
  MacroEvent e;
  e.is_text = true;
  e.text = text;
  macro_recorded_.push_back(std::move(e));
}

void Editor::kmacro_start_macro() {
  if (macro_executing_) {
    fail(L"Cannot define a keyboard macro while one runs");
    return;
  }
  if (macro_recording_) {
    // The keys of this command are not part of the macro.
    macro_recorded_.resize(std::min(macro_sequence_start_, macro_recorded_.size()));
    fail(L"Already defining keyboard macro");
    return;
  }
  macro_recording_ = true;
  macro_recorded_.clear();
  macro_sequence_start_ = 0;
  message_ = L"Defining kbd macro...";
}

void Editor::kmacro_end_macro() {
  if (!macro_recording_) {
    fail(L"Not defining kbd macro");
    return;
  }
  // The keys typed for this command (from the start of their key sequence
  // outside the minibuffer: C-x ), <f4>, or M-x kmacro-end-macro RET) are
  // not part of the macro.
  macro_recorded_.resize(std::min(macro_sequence_start_, macro_recorded_.size()));
  macro_recording_ = false;
  if (macro_recorded_.empty()) {
    message_ = L"Ignore empty macro";  // the last macro stays (Emacs)
    return;
  }
  macro_ = std::move(macro_recorded_);
  macro_recorded_.clear();
  message_ = L"Keyboard macro defined";
}

void Editor::kmacro_end_and_call_macro() {
  if (macro_recording_) {
    // While recording, C-x e and <f4> end the recording (PLAN 4.4: <f4>
    // ends it); the macro runs with the next C-x e.
    kmacro_end_macro();
    return;
  }
  if (macro_executing_) {
    fail(L"A keyboard macro cannot run itself");
    return;
  }
  if (macro_.empty()) {
    fail(L"No kbd macro has been defined");
    return;
  }
  const int count = prefix_count();
  if (count <= 0) {
    fail(L"Repeat count must be positive");
    return;
  }
  if (static_cast<size_t>(count) > 1'000'000) {
    fail(L"Repeat count too large");
    return;
  }
  macro_runs_ = count;  // run_pending_macro runs it after this command
}

void Editor::run_pending_macro() {
  if (macro_runs_ == 0 || macro_executing_) return;
  const int runs = macro_runs_;
  macro_runs_ = 0;
  const std::vector<MacroEvent> events = macro_;
  macro_executing_ = true;
  // One undo unit for the whole run (PLAN 2 "undo unit"): the groups the
  // run makes are merged, and undo puts point back where it started. The
  // typing group before the run is closed first, so that the run's first
  // characters do not join it.
  main_.undo.begin_command(main_.point, false);
  main_.undo.end_command(main_.point);
  // By id (retry R2, as S2): an event that undoes history older than the
  // call neither splits the unit nor joins an older edit to it.
  const uint64_t first = main_.undo.next_group_id();
  const size_t start = main_.point;
  const uint64_t errors = errors_;
  bool ok = true;
  bool quit = false;
  ++kill_defer_;  // the kills of the run write the clipboard once (R1)
  // C-g typed meanwhile stops the run (retry R2): the host is asked every
  // macro_poll_ms_ of the run, not after every event.
  using Clock = std::chrono::steady_clock;
  auto next_poll = Clock::now() + std::chrono::milliseconds(macro_poll_ms_);
  for (int i = 0; ok && i < runs; ++i) {
    for (const MacroEvent& e : events) {
      if (Clock::now() >= next_poll) {
        if (host_.quit_requested()) {
          quit = true;
          ok = false;
          break;
        }
        next_poll = Clock::now() + std::chrono::milliseconds(macro_poll_ms_);
      }
      if (e.is_text) {
        insert_text(e.text);
      } else {
        handle_key(e.key);
      }
      // A failure (a command that fails, an undefined key, a failing
      // search, a question answered No or Cancel) stops the run and the
      // repeats after it.
      if (errors_ != errors) {
        ok = false;
        break;
      }
    }
  }
  macro_executing_ = false;
  --kill_defer_;
  if (quit) message_ = L"Quit";
  if (!flush_kills()) ok = false;
  if (!ok) {
    // A run stopped inside isearch (a failing search) ends it as C-g does,
    // and one stopped inside query-replace ends it as q; the message of the
    // failure stays.
    const std::wstring message = message_;
    if (isearch_active()) isearch_end(true);
    if (query_replace_active()) query_replace_finish(message);
    message_ = message;
  }
  main_.undo.merge_groups_from_id(first, start);
  if (query_replace_active()) {
    // Retry R3: the run ended inside query-replace. The call (with the
    // replacements it made) stays its own undo unit; the replacements made
    // after it form another one, which undo takes back to here. They are
    // still counted from the start of query-replace (first_group).
    replace_.merge_from = main_.undo.next_group_id();
    replace_.merge_point = main_.point;
  }
  // `e` repeats only when the run left no mode open (retry R5): after a
  // run that ends inside isearch, the minibuffer or query-replace, `e` is
  // input for that mode.
  macro_repeat_ = ok && !input_mode_active();
  if (macro_repeat_ && message_.empty()) message_ = L"(Type e to repeat macro)";
}

// ---- Kills in a row (M6, retry R1) ----

std::wstring Editor::kill_joined() const {
  std::wstring joined(kill_front_rev_.rbegin(), kill_front_rev_.rend());
  joined += kill_back_;
  return joined;
}

void Editor::clear_kills() {
  if (!kill_back_.empty()) std::wstring().swap(kill_back_);
  if (!kill_front_rev_.empty()) std::wstring().swap(kill_front_rev_);
  kill_pending_ = false;
}

bool Editor::kill_add(std::wstring_view text, bool prepend) {
  const bool continuing = is_kill_command(last_command_);
  if (kill_defer_ > 0) {
    // Inside a macro run or a command sequence: joined in memory only (the
    // cost of the added text); written by flush_kills.
    if (!continuing) clear_kills();
    if (prepend) {
      kill_front_rev_.append(text.rbegin(), text.rend());
    } else {
      kill_back_.append(text);
    }
    kill_pending_ = true;
    return true;
  }
  // At the top level: written now, as before; a failed write keeps the text
  // in the buffer.
  std::wstring joined;
  if (continuing) {
    joined.reserve(kill_front_rev_.size() + kill_back_.size() + text.size());
    if (prepend) joined.append(text);
    joined.append(kill_front_rev_.rbegin(), kill_front_rev_.rend());
    joined.append(kill_back_);
    if (!prepend) joined.append(text);
  } else {
    joined = text;
  }
  if (!host_.set_clipboard(joined)) {
    fail(L"Cannot write to the clipboard");
    return false;
  }
  if (!continuing) clear_kills();
  if (prepend) {
    kill_front_rev_.append(text.rbegin(), text.rend());
  } else {
    kill_back_.append(text);
  }
  kill_pending_ = false;
  return true;
}

bool Editor::flush_kills() {
  if (!kill_pending_) return true;
  kill_pending_ = false;
  if (host_.set_clipboard(kill_joined())) return true;
  // The text was killed already: say so; undo (C-/) brings it back.
  fail(L"Cannot write to the clipboard; the killed text is not on it (C-/ brings it back)");
  return false;
}

}  // namespace geditor
