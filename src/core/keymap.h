// Keymaps: key -> binding (a command, a command with arguments or a command
// sequence), nested prefix keymap, or an explicit "unbound" marker.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "key.h"

namespace geditor {

// An argument of a bound command (docs/PLAN.md 4.3 "C-c d": ["insert-text", "..."]).
struct CommandArg {
  enum class Type { kText, kInteger };
  Type type = Type::kText;
  std::wstring text;
  int64_t integer = 0;

  bool operator==(const CommandArg&) const = default;
};

struct CommandCall {
  std::string name;
  std::vector<CommandArg> args;

  bool operator==(const CommandCall&) const = default;
};

// What a key runs: one call, or a sequence of calls ("C-c l": [[...], [...]])
// that runs as one undo group.
struct Binding {
  std::vector<CommandCall> calls;
  bool sequence = false;

  bool operator==(const Binding&) const = default;
};

Binding command_binding(std::string command);

class Keymap {
 public:
  // kUnbound: the key was unbound explicitly (settings value null). It does
  // not fall through to lower layers and a character key is not inserted.
  enum class Result { kUndefined, kPrefix, kCommand, kUnbound };
  struct Lookup {
    Result result = Result::kUndefined;
    std::string_view command;          // name of the first call (kCommand)
    const Binding* binding = nullptr;  // kCommand
  };

  Keymap();
  ~Keymap();
  Keymap(Keymap&&) noexcept;
  Keymap& operator=(Keymap&&) noexcept;

  // Binds `keys`, creating prefix keymaps as needed and replacing an earlier
  // binding or unbound marker of the same keys. Returns false (and changes
  // nothing) when the binding conflicts with a prefix: a binding on a prefix
  // key, or a prefix through a key bound to a command.
  bool bind(const KeySequence& keys, Binding binding);
  bool bind(const KeySequence& keys, std::string command);
  bool bind(std::wstring_view notation, std::string command);
  // Marks `keys` as unbound. A prefix key loses all bindings under it. False
  // (nothing changed) when a key before the last one is bound to a command.
  bool unbind(const KeySequence& keys);

  Lookup lookup(const KeySequence& keys) const;

 private:
  struct Entry {
    std::unique_ptr<Binding> binding;
    std::unique_ptr<Keymap> prefix;
    bool unbound = false;
  };
  // True when every key before the last one of `keys` is free, unbound or a
  // prefix (no command on the way).
  bool path_is_free(const KeySequence& keys) const;
  // The map that holds the last key of `keys`, creating prefix maps.
  Keymap* prefix_map_for(const KeySequence& keys);

  std::map<uint64_t, Entry> entries_;
};

// Emacs shift translation: when `keys` is undefined and its last key has S-,
// looks the sequence up again without S-. When that succeeds, the last key
// of `keys` is replaced by the unshifted key. A binding of the S- key itself
// (or an explicit unbinding of it) always wins. `lookup` maps a KeySequence
// to a Keymap::Lookup.
template <typename LookupFn>
Keymap::Lookup lookup_shift_translated(KeySequence& keys, LookupFn&& lookup) {
  const Keymap::Lookup shifted = lookup(keys);
  if (shifted.result != Keymap::Result::kUndefined || keys.empty() ||
      (keys.back().mods & kModShift) == 0) {
    return shifted;
  }
  KeySequence unshifted = keys;
  unshifted.back().mods = static_cast<uint8_t>(unshifted.back().mods & ~kModShift);
  const Keymap::Lookup r = lookup(unshifted);
  if (r.result == Keymap::Result::kUndefined || r.result == Keymap::Result::kUnbound) {
    return shifted;
  }
  keys = std::move(unshifted);
  return r;
}

struct DefaultBinding {
  const wchar_t* keys;
  const char* command;
};

// The Emacs-compatible global keys of docs/PLAN.md 4.4 (including the keys of
// commands that later milestones implement) and the Windows editing keys that
// do not conflict with Emacs.
std::span<const DefaultBinding> default_global_bindings();
// The 12 Windows-compatible C-S- keys of PLAN 4.4; they take precedence over
// the Emacs layer (and, being bound, over shift translation to C-<letter>).
std::span<const DefaultBinding> default_compat_bindings();
// Bindings active only while the minibuffer reads input (over global).
std::span<const DefaultBinding> default_minibuffer_bindings();
// Keys during isearch and query-replace (PLAN 4.4; the modes arrive in M5).
std::span<const DefaultBinding> default_isearch_bindings();
std::span<const DefaultBinding> default_query_replace_bindings();

Keymap make_keymap(std::span<const DefaultBinding> bindings);

}  // namespace geditor
