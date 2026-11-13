#include "key.h"

#include "unicode.h"

namespace geditor {
namespace {

struct NamedKey {
  const wchar_t* name;
  char32_t code;
};

constexpr NamedKey kNamedKeys[] = {
    {L"RET", kKeyReturn},       {L"TAB", kKeyTab},          {L"ESC", kKeyEscape},
    {L"DEL", kKeyBackspace},    {L"SPC", U' '},             {L"<delete>", kKeyDelete},
    {L"<insert>", kKeyInsert},  {L"<home>", kKeyHome},      {L"<end>", kKeyEnd},
    {L"<prior>", kKeyPrior},    {L"<next>", kKeyNext},      {L"<left>", kKeyLeft},
    {L"<right>", kKeyRight},    {L"<up>", kKeyUp},          {L"<down>", kKeyDown},
};

std::optional<char32_t> parse_base(std::wstring_view s) {
  for (const NamedKey& k : kNamedKeys) {
    if (s == k.name) return k.code;
  }
  if (s.size() >= 4 && s.front() == L'<' && s.back() == L'>' && s[1] == L'f') {
    const std::wstring_view digits = s.substr(2, s.size() - 3);
    if (digits.empty() || digits.size() > 2 || digits[0] == L'0') return std::nullopt;
    int n = 0;
    for (const wchar_t d : digits) {
      if (d < L'0' || d > L'9') return std::nullopt;
      n = n * 10 + (d - L'0');
    }
    if (n < 1 || n > 24) return std::nullopt;
    return static_cast<char32_t>(kKeyF1 + (n - 1));
  }
  // A single printable code point.
  size_t units = 0;
  if (s.empty()) return std::nullopt;
  const char32_t c = decode_at(s, 0, &units);
  if (units != s.size()) return std::nullopt;
  if (c <= 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) return std::nullopt;
  if (c >= 0xD800 && c <= 0xDFFF) return std::nullopt;
  return c;
}

bool is_ascii_upper(char32_t c) {
  return c >= U'A' && c <= U'Z';
}

bool is_ascii_lower(char32_t c) {
  return c >= U'a' && c <= U'z';
}

}  // namespace

std::optional<Key> parse_key(std::wstring_view s) {
  Key key;
  // Modifier prefixes: "C-", "M-", "S-", in any order, each at most once.
  while (s.size() > 2 && s[1] == L'-') {
    uint8_t mod = 0;
    switch (s[0]) {
      case L'C': mod = kModCtrl; break;
      case L'M': mod = kModMeta; break;
      case L'S': mod = kModShift; break;
      default: return std::nullopt;
    }
    if ((key.mods & mod) != 0) return std::nullopt;
    key.mods |= mod;
    s.remove_prefix(2);
  }
  const std::optional<char32_t> base = parse_base(s);
  if (!base) return std::nullopt;
  key.code = *base;
  if (key.is_char()) {
    // Letters are written lower case with S-; other characters are written as
    // the character that Shift produces.
    if (is_ascii_upper(key.code) && key.mods != 0) return std::nullopt;
    if ((key.mods & kModShift) != 0 && !is_ascii_lower(key.code)) return std::nullopt;
  }
  return key;
}

std::optional<KeySequence> parse_key_sequence(std::wstring_view s) {
  // Keys are separated by exactly one space; no space at either end.
  KeySequence keys;
  if (s.empty()) return std::nullopt;
  size_t i = 0;
  for (;;) {
    size_t j = i;
    while (j < s.size() && s[j] != L' ') ++j;
    const std::optional<Key> key = parse_key(s.substr(i, j - i));
    if (!key) return std::nullopt;
    keys.push_back(*key);
    if (j == s.size()) break;
    i = j + 1;
    if (i == s.size()) return std::nullopt;  // trailing space
  }
  return keys;
}

std::wstring key_to_string(const Key& key) {
  std::wstring s;
  if (key.ctrl()) s += L"C-";
  if (key.meta()) s += L"M-";
  if ((key.mods & kModShift) != 0) s += L"S-";
  for (const NamedKey& k : kNamedKeys) {
    if (key.code == k.code) return s + k.name;
  }
  if (key.code >= kKeyF1 && key.code <= kKeyF24) {
    return s + L"<f" + std::to_wstring(key.code - kKeyF1 + 1) + L">";
  }
  append_utf16(s, key.code);
  return s;
}

std::wstring key_sequence_to_string(const KeySequence& keys) {
  std::wstring s;
  for (const Key& k : keys) {
    if (!s.empty()) s += L' ';
    s += key_to_string(k);
  }
  return s;
}

bool is_self_insert_key(const Key& key) {
  if (key.mods != 0 || !key.is_char()) return false;
  return key.code >= 0x20 && key.code != 0x7F && !(key.code >= 0x80 && key.code < 0xA0);
}

}  // namespace geditor
