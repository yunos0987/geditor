// Keymaps: key -> command name or nested prefix keymap.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "key.h"

namespace geditor {

class Keymap {
 public:
  enum class Result { kUndefined, kPrefix, kCommand };
  struct Lookup {
    Result result = Result::kUndefined;
    std::string_view command;
  };

  Keymap();
  ~Keymap();
  Keymap(Keymap&&) noexcept;
  Keymap& operator=(Keymap&&) noexcept;

  // Binds `keys` to `command`, creating prefix keymaps as needed. Returns
  // false when the binding conflicts with a prefix (a command bound on a
  // prefix key, or a prefix through a key bound to a command).
  bool bind(const KeySequence& keys, std::string command);
  bool bind(std::wstring_view notation, std::string command);

  Lookup lookup(const KeySequence& keys) const;

 private:
  struct Entry {
    std::string command;
    std::unique_ptr<Keymap> prefix;
  };
  std::map<uint64_t, Entry> entries_;
};

// Emacs shift translation: when `keys` is undefined and its last key has S-,
// looks the sequence up again without S-. When that succeeds, the last key
// of `keys` is replaced by the unshifted key. A binding of the S- key itself
// always wins. `lookup` maps a KeySequence to a Keymap::Lookup.
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
  if (r.result == Keymap::Result::kUndefined) return shifted;
  keys = std::move(unshifted);
  return r;
}

struct DefaultBinding {
  const wchar_t* keys;
  const char* command;
};

// Default bindings of M1 (Emacs keys of docs/PLAN.md 4.4 that have an M1
// command, plus the Windows editing keys that do not conflict with Emacs).
std::span<const DefaultBinding> default_global_bindings();
// Bindings active only while the minibuffer reads input (over global).
std::span<const DefaultBinding> default_minibuffer_bindings();

Keymap make_keymap(std::span<const DefaultBinding> bindings);

}  // namespace geditor
