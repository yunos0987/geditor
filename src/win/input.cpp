#include "input.h"

#include <windows.h>

#include "unicode.h"

namespace geditor::win {
namespace {

struct NamedVirtualKey {
  unsigned vk;
  char32_t code;
};

constexpr NamedVirtualKey kNamedKeys[] = {
    {VK_RETURN, kKeyReturn}, {VK_TAB, kKeyTab},     {VK_ESCAPE, kKeyEscape},
    {VK_BACK, kKeyBackspace}, {VK_DELETE, kKeyDelete}, {VK_INSERT, kKeyInsert},
    {VK_HOME, kKeyHome},     {VK_END, kKeyEnd},     {VK_PRIOR, kKeyPrior},
    {VK_NEXT, kKeyNext},     {VK_LEFT, kKeyLeft},   {VK_RIGHT, kKeyRight},
    {VK_UP, kKeyUp},         {VK_DOWN, kKeyDown},
};

// Keys that never become a Key: modifiers, lock keys, IME keys.
bool is_ignored_key(unsigned vk) {
  switch (vk) {
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
    case VK_PROCESSKEY:
    case VK_PACKET:
    case VK_KANA:
    case VK_KANJI:
    case VK_CONVERT:
    case VK_NONCONVERT:
    case VK_ACCEPT:
    case VK_MODECHANGE:
      return true;
    default:
      // OEM IME keys of the Japanese keyboard (VK_DBE_*: 0xF0..0xF6).
      return vk >= 0xF0 && vk <= 0xF6;
  }
}

std::optional<char32_t> named_key(unsigned vk) {
  for (const NamedVirtualKey& k : kNamedKeys) {
    if (k.vk == vk) return k.code;
  }
  if (vk >= VK_F1 && vk <= VK_F24) return static_cast<char32_t>(kKeyF1 + (vk - VK_F1));
  return std::nullopt;
}

}  // namespace

std::optional<Key> translate_key_down(unsigned vk, unsigned scan_code, ModifierState mods,
                                      LayoutCharFn layout, void* context) {
  if (is_ignored_key(vk)) return std::nullopt;
  uint8_t m = 0;
  if (mods.ctrl) m |= kModCtrl;
  if (mods.alt) m |= kModMeta;
  if (mods.shift) m |= kModShift;
  if (const std::optional<char32_t> code = named_key(vk)) return Key{*code, m};
  if (!mods.ctrl && !mods.alt) return std::nullopt;  // arrives as WM_CHAR

  char32_t c = layout == nullptr ? 0 : layout(context, vk, scan_code, mods.shift);
  if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0) || (c >= 0xD800 && c <= 0xDFFF) ||
      c > 0x10FFFF) {
    return std::nullopt;
  }
  if (c >= U'A' && c <= U'Z') {
    // Letters are written lower case; Shift becomes S- (C-S-a).
    c = c - U'A' + U'a';
  } else if (!(c >= U'a' && c <= U'z')) {
    // Other characters already include Shift (C-?, M-<, M-%).
    m &= static_cast<uint8_t>(~kModShift);
  }
  return Key{c, m};
}

bool passes_to_system_when_unbound(const Key& key) {
  return key.mods == kModMeta && (key.code == kKeyF1 + 3 || key.code == U' ');
}

std::optional<Key> CharAssembler::feed(wchar_t unit) {
  if (is_high_surrogate(unit)) {
    high_ = unit;  // a previous unpaired high surrogate is dropped
    return std::nullopt;
  }
  if (is_low_surrogate(unit)) {
    const wchar_t high = high_;
    high_ = 0;
    if (high == 0) return std::nullopt;
    return Key{combine_surrogates(high, unit), 0};
  }
  high_ = 0;
  switch (unit) {
    case L'\r': return Key{kKeyReturn, 0};
    case L'\t': return Key{kKeyTab, 0};
    case L'\b': return Key{kKeyBackspace, 0};
    case 0x1B: return Key{kKeyEscape, 0};
    default: break;
  }
  if (unit < 0x20 || unit == 0x7F || (unit >= 0x80 && unit < 0xA0)) return std::nullopt;
  return Key{static_cast<char32_t>(unit), 0};
}

}  // namespace geditor::win
