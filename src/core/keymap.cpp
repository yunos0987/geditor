#include "keymap.h"

#include <utility>

namespace geditor {

Keymap::Keymap() = default;
Keymap::~Keymap() = default;
Keymap::Keymap(Keymap&&) noexcept = default;
Keymap& Keymap::operator=(Keymap&&) noexcept = default;

Binding command_binding(std::string command) {
  Binding b;
  b.calls.push_back(CommandCall{std::move(command), {}});
  return b;
}

bool Keymap::path_is_free(const KeySequence& keys) const {
  const Keymap* map = this;
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    const auto it = map->entries_.find(keys[i].id());
    if (it == map->entries_.end()) return true;
    const Entry& e = it->second;
    if (e.binding) return false;
    if (!e.prefix) return true;
    map = e.prefix.get();
  }
  return true;
}

Keymap* Keymap::prefix_map_for(const KeySequence& keys) {
  Keymap* map = this;
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    Entry& e = map->entries_[keys[i].id()];
    e.unbound = false;  // an unbound key becomes a prefix again
    if (!e.prefix) e.prefix = std::make_unique<Keymap>();
    map = e.prefix.get();
  }
  return map;
}

bool Keymap::bind(const KeySequence& keys, Binding binding) {
  if (keys.empty() || binding.calls.empty() || !path_is_free(keys)) return false;
  // A binding on an existing prefix key conflicts with the keys under it.
  {
    const Lookup existing = lookup(keys);
    if (existing.result == Result::kPrefix) return false;
  }
  Entry& last = prefix_map_for(keys)->entries_[keys.back().id()];
  last.binding = std::make_unique<Binding>(std::move(binding));
  last.unbound = false;
  return true;
}

bool Keymap::bind(const KeySequence& keys, std::string command) {
  return bind(keys, command_binding(std::move(command)));
}

bool Keymap::bind(std::wstring_view notation, std::string command) {
  const std::optional<KeySequence> keys = parse_key_sequence(notation);
  return keys && bind(*keys, std::move(command));
}

bool Keymap::unbind(const KeySequence& keys) {
  if (keys.empty() || !path_is_free(keys)) return false;
  Entry& last = prefix_map_for(keys)->entries_[keys.back().id()];
  last.binding.reset();
  last.prefix.reset();
  last.unbound = true;
  return true;
}

Keymap::Lookup Keymap::lookup(const KeySequence& keys) const {
  Lookup result;
  const Keymap* map = this;
  for (size_t i = 0; i < keys.size(); ++i) {
    const auto it = map->entries_.find(keys[i].id());
    if (it == map->entries_.end()) return result;
    const Entry& e = it->second;
    if (i + 1 == keys.size()) {
      if (e.prefix) {
        result.result = Result::kPrefix;
      } else if (e.binding) {
        result.result = Result::kCommand;
        result.command = e.binding->calls.front().name;
        result.binding = e.binding.get();
      } else if (e.unbound) {
        result.result = Result::kUnbound;
      }
      return result;
    }
    if (!e.prefix) return result;
    map = e.prefix.get();
  }
  return result;
}

namespace {

constexpr DefaultBinding kGlobalBindings[] = {
    // Movement
    {L"C-f", "forward-char"},
    {L"C-b", "backward-char"},
    {L"C-n", "next-line"},
    {L"C-p", "previous-line"},
    {L"C-a", "beginning-of-line"},
    {L"C-e", "end-of-line"},
    {L"M-f", "forward-word"},
    {L"M-b", "backward-word"},
    {L"M-<", "beginning-of-buffer"},
    {L"M->", "end-of-buffer"},
    {L"C-v", "scroll-up-command"},
    {L"M-v", "scroll-down-command"},
    {L"C-l", "recenter-top-bottom"},
    // Mark
    {L"C-SPC", "set-mark-command"},
    {L"C-x C-x", "exchange-point-and-mark"},
    {L"C-x h", "mark-whole-buffer"},
    {L"C-g", "keyboard-quit"},
    // ESC ESC ESC: ESC then ESC is M-ESC (decision G8).
    {L"M-ESC ESC", "keyboard-escape-quit"},
    // Delete and kill
    {L"C-d", "delete-char"},
    {L"DEL", "delete-backward-char"},
    {L"C-k", "kill-line"},
    {L"M-d", "kill-word"},
    {L"M-DEL", "backward-kill-word"},
    {L"C-w", "kill-region"},
    {L"M-w", "kill-ring-save"},
    {L"C-y", "yank"},
    // Input
    {L"RET", "newline"},
    {L"C-j", "newline"},
    {L"C-o", "open-line"},
    {L"TAB", "insert-tab"},
    {L"C-q", "quoted-insert"},
    {L"C-t", "transpose-chars"},
    {L"M-u", "upcase-word"},
    {L"M-l", "downcase-word"},
    {L"M-c", "capitalize-word"},
    // Undo
    {L"C-/", "undo"},
    {L"C-_", "undo"},
    {L"C-x u", "undo"},
    {L"C-?", "redo"},
    {L"C-M-_", "redo"},
    // Files
    {L"C-x C-f", "find-file"},
    {L"C-x C-s", "save-buffer"},
    {L"C-x C-w", "write-file"},
    {L"C-x C-q", "read-only-mode"},
    {L"C-x C-c", "quit"},
    // Search and replace (M5)
    {L"C-s", "isearch-forward"},
    {L"C-r", "isearch-backward"},
    {L"M-%", "query-replace"},
    {L"C-M-%", "query-replace-regexp"},
    {L"M-g g", "goto-line"},
    {L"M-g M-g", "goto-line"},
    // Encodings and line ends (M4)
    {L"C-x RET f", "set-buffer-file-coding-system"},
    {L"C-x RET r", "revert-buffer-with-coding-system"},
    {L"C-x RET c", "find-file-with-encoding"},
    {L"C-x RET e", "set-eol"},
    // View (M3)
    {L"C-x C-+", "text-scale-increase"},
    {L"C-x C-=", "text-scale-increase"},
    {L"C-x C--", "text-scale-decrease"},
    {L"C-x C-0", "text-scale-reset"},
    {L"<f10>", "menu-bar-open"},
    // Prefix argument
    {L"C-u", "universal-argument"},
    {L"M-0", "digit-argument"},
    {L"M-1", "digit-argument"},
    {L"M-2", "digit-argument"},
    {L"M-3", "digit-argument"},
    {L"M-4", "digit-argument"},
    {L"M-5", "digit-argument"},
    {L"M-6", "digit-argument"},
    {L"M-7", "digit-argument"},
    {L"M-8", "digit-argument"},
    {L"M-9", "digit-argument"},
    {L"M--", "negative-argument"},
    // Keyboard macros (M6). <f4> ends the macro while recording, else runs it.
    {L"C-x (", "kmacro-start-macro"},
    {L"<f3>", "kmacro-start-macro"},
    {L"C-x )", "kmacro-end-macro"},
    {L"C-x e", "kmacro-end-and-call-macro"},
    {L"<f4>", "kmacro-end-and-call-macro"},
    // Other
    {L"M-x", "execute-extended-command"},
    // Windows editing keys that do not conflict with Emacs
    {L"<left>", "backward-char"},
    {L"<right>", "forward-char"},
    {L"<up>", "previous-line"},
    {L"<down>", "next-line"},
    {L"<home>", "beginning-of-line"},
    {L"<end>", "end-of-line"},
    {L"<prior>", "scroll-down-command"},
    {L"<next>", "scroll-up-command"},
    {L"C-<left>", "backward-word"},
    {L"C-<right>", "forward-word"},
    {L"C-<home>", "beginning-of-buffer"},
    {L"C-<end>", "end-of-buffer"},
    {L"<delete>", "delete-char"},
};

constexpr DefaultBinding kMinibufferBindings[] = {
    {L"RET", "exit-minibuffer"},
    {L"C-j", "exit-minibuffer"},
    {L"TAB", "minibuffer-complete"},
    {L"M-p", "previous-history-element"},
    {L"M-n", "next-history-element"},
};

// PLAN 4.4 "Windows compatible keys" (12 keys; C-S-h and C-S-r share a row).
// Being bound, they are not shift-translated to C-<letter>.
constexpr DefaultBinding kCompatBindings[] = {
    {L"C-S-a", "select-all"},
    {L"C-S-c", "copy"},
    {L"C-S-x", "cut"},
    {L"C-S-v", "paste"},
    {L"C-S-z", "undo"},
    {L"C-S-y", "redo"},
    {L"C-S-s", "save-buffer"},
    {L"C-S-w", "quit"},
    {L"C-S-f", "isearch-forward"},
    {L"C-S-h", "query-replace"},
    {L"C-S-r", "query-replace"},
    {L"C-S-g", "goto-line"},
};

// PLAN 4.4 "during isearch".
constexpr DefaultBinding kIsearchBindings[] = {
    {L"C-s", "isearch-repeat-forward"},
    {L"C-r", "isearch-repeat-backward"},
    {L"DEL", "isearch-delete-char"},
    {L"RET", "isearch-exit"},
    {L"C-g", "isearch-abort"},
    {L"M-r", "isearch-toggle-regexp"},
    {L"M-c", "isearch-toggle-case-fold"},
    {L"M-%", "isearch-query-replace"},
};

// PLAN 4.4 "during query-replace".
constexpr DefaultBinding kQueryReplaceBindings[] = {
    {L"y", "query-replace-act"},
    {L"SPC", "query-replace-act"},
    {L"n", "query-replace-skip"},
    {L"DEL", "query-replace-skip"},
    {L"!", "query-replace-all"},
    {L".", "query-replace-act-and-exit"},
    {L"q", "query-replace-exit"},
    {L"RET", "query-replace-exit"},
};

}  // namespace

std::span<const DefaultBinding> default_global_bindings() {
  return kGlobalBindings;
}

std::span<const DefaultBinding> default_compat_bindings() {
  return kCompatBindings;
}

std::span<const DefaultBinding> default_minibuffer_bindings() {
  return kMinibufferBindings;
}

std::span<const DefaultBinding> default_isearch_bindings() {
  return kIsearchBindings;
}

std::span<const DefaultBinding> default_query_replace_bindings() {
  return kQueryReplaceBindings;
}

Keymap make_keymap(std::span<const DefaultBinding> bindings) {
  Keymap map;
  for (const DefaultBinding& b : bindings) map.bind(b.keys, b.command);
  return map;
}

}  // namespace geditor
