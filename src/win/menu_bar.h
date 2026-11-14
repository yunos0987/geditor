// The Win32 menus of docs/PLAN.md 4.9, built from the core menu model
// (src/core/menu.*) and nothing else: the menu bar (File / Edit / View) and
// the right-click Edit menu. Texts carry the key of the current keymap after
// a TAB; WM_INITMENUPOPUP refreshes texts, grey items and check / radio
// marks from the core just before a menu opens. Each command item has the
// WM_COMMAND id kMenuCommandBase + its core id.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include "editor.h"
#include "menu.h"

namespace geditor::win {

inline constexpr UINT kMenuCommandBase = 0x1000;

UINT menu_command_id(const MenuItem& item);
// The item of a WM_COMMAND id; nullptr when the id is no menu item.
const MenuItem* menu_item_for_command(UINT id);

// "&Undo\tC-/" (no TAB without a key); a '&' of the key is written "&&".
std::wstring menu_item_text(const MenuItem& item, const std::wstring& key_text);

// The menu bar / the right-click menu with the texts and states of now.
// nullptr when Windows refuses to create it (GetLastError tells why).
HMENU create_menu_bar(const Editor& editor);
HMENU create_context_menu(const Editor& editor);

// WM_INITMENUPOPUP (and after a settings reload): the texts, grey states
// and check / radio marks of the items of `menu` and of its submenus.
void update_menu(HMENU menu, const Editor& editor);

// One item of a built HMENU as GetMenuItemInfo reads it back.
struct MenuEntry {
  std::wstring path;      // "File > Wrap > None": titles and texts without '&'
  std::wstring text;      // the text before the TAB, with '&'
  std::wstring key_text;  // after the TAB, "&&" read back as "&" (the core's key text)
  UINT id = 0;
  bool separator = false;
  bool submenu = false;
  bool enabled = false;
  bool checked = false;
  bool radio = false;
};

// Reads `menu` and its submenus depth first; `prefix` starts the paths.
std::vector<MenuEntry> read_menu(HMENU menu, std::wstring_view prefix = {});

}  // namespace geditor::win
