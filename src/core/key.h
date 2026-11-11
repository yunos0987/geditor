// Key events and the Emacs-style key notation of docs/PLAN.md 4.2.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace geditor {

enum KeyModifier : uint8_t {
  kModCtrl = 1,
  kModMeta = 2,
  kModShift = 4,
};

// Codes of non-character keys; character keys use their code point.
enum SpecialKey : char32_t {
  kKeyReturn = 0x110000,
  kKeyTab,
  kKeyEscape,
  kKeyBackspace,  // DEL
  kKeyDelete,
  kKeyInsert,
  kKeyHome,
  kKeyEnd,
  kKeyPrior,
  kKeyNext,
  kKeyLeft,
  kKeyRight,
  kKeyUp,
  kKeyDown,
  kKeyF1,
  kKeyF24 = kKeyF1 + 23,
};

struct Key {
  char32_t code = 0;
  uint8_t mods = 0;

  bool operator==(const Key&) const = default;
  uint64_t id() const { return (static_cast<uint64_t>(mods) << 32) | code; }
  bool is_char() const { return code < kKeyReturn; }
  bool ctrl() const { return (mods & kModCtrl) != 0; }
  bool meta() const { return (mods & kModMeta) != 0; }
};

using KeySequence = std::vector<Key>;

// Parses one key such as "C-x", "M-<", "<f1>", "C-S-a" or "SPC".
std::optional<Key> parse_key(std::wstring_view notation);
// Parses space-separated keys such as "C-x C-s".
std::optional<KeySequence> parse_key_sequence(std::wstring_view notation);

std::wstring key_to_string(const Key& key);
std::wstring key_sequence_to_string(const KeySequence& keys);

// True for an unmodified printable character (inserted when unbound).
bool is_self_insert_key(const Key& key);

}  // namespace geditor
