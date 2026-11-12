// Keyboard messages -> core Key events (docs/PLAN.md 4.2). Pure logic: the
// keyboard layout is reached through a callback so that tests can stub it.
#pragma once

#include <optional>

#include "key.h"

namespace geditor::win {

struct ModifierState {
  bool ctrl = false;
  bool alt = false;
  bool shift = false;
};

// Character that the layout produces for `vk` with only Shift applied (Ctrl,
// Alt and Caps Lock released), or 0 when the key produces none (dead keys
// included). The real implementation calls ToUnicodeEx without changing the
// keyboard state.
using LayoutCharFn = char32_t (*)(void* context, unsigned vk, unsigned scan_code, bool shift);

// Translates WM_KEYDOWN / WM_SYSKEYDOWN. Returns a key for named keys (RET,
// TAB, ESC, DEL, arrows, F1..F24, ...) with any modifiers, and for character
// keys pressed with Ctrl or Alt. Returns nothing for keys that should go on to
// TranslateMessage: unmodified characters (they arrive as WM_CHAR), modifier
// keys alone, IME keys (VK_PROCESSKEY) and keys that produce no character.
std::optional<Key> translate_key_down(unsigned vk, unsigned scan_code, ModifierState mods,
                                      LayoutCharFn layout, void* context);

// Keys that Windows handles when the keymap does not bind them: M-<f4>
// (close the window) and M-SPC (system menu).
bool passes_to_system_when_unbound(const Key& key);

// Turns WM_CHAR units into keys: a surrogate pair delivered as two messages
// becomes one key; CR, TAB, BS and ESC become RET, TAB, DEL and ESC; other
// control characters are dropped.
class CharAssembler {
 public:
  std::optional<Key> feed(wchar_t unit);
  void reset() { high_ = 0; }

 private:
  wchar_t high_ = 0;
};

}  // namespace geditor::win
