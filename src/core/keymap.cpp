#include "keymap.h"

#include <utility>

namespace geditor {

Keymap::Keymap() = default;
Keymap::~Keymap() = default;
Keymap::Keymap(Keymap&&) noexcept = default;
Keymap& Keymap::operator=(Keymap&&) noexcept = default;

bool Keymap::bind(const KeySequence& keys, std::string command) {
  if (keys.empty()) return false;
  Keymap* map = this;
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    Entry& e = map->entries_[keys[i].id()];
    if (!e.command.empty()) return false;
    if (!e.prefix) e.prefix = std::make_unique<Keymap>();
    map = e.prefix.get();
  }
  Entry& last = map->entries_[keys.back().id()];
  if (last.prefix) return false;
  last.command = std::move(command);
  return true;
}

bool Keymap::bind(std::wstring_view notation, std::string command) {
  const std::optional<KeySequence> keys = parse_key_sequence(notation);
  return keys && bind(*keys, std::move(command));
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
      } else if (!e.command.empty()) {
        result.result = Result::kCommand;
        result.command = e.command;
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

}  // namespace

std::span<const DefaultBinding> default_global_bindings() {
  return kGlobalBindings;
}

std::span<const DefaultBinding> default_minibuffer_bindings() {
  return kMinibufferBindings;
}

Keymap make_keymap(std::span<const DefaultBinding> bindings) {
  Keymap map;
  for (const DefaultBinding& b : bindings) map.bind(b.keys, b.command);
  return map;
}

}  // namespace geditor
