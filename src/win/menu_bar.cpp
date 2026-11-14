#include "menu_bar.h"

#include <span>

namespace geditor::win {
namespace {

void set_text_and_state(MENUITEMINFOW& mii, const std::wstring& text, const MenuItemState& state) {
  mii.fMask |= MIIM_STRING | MIIM_STATE;
  mii.dwTypeData = const_cast<wchar_t*>(text.c_str());
  mii.fState = (state.enabled ? MFS_ENABLED : MFS_DISABLED) | (state.checked ? MFS_CHECKED : MFS_UNCHECKED);
}

// Appends `items` to `menu`; false (the caller destroys `menu` and with it
// the submenus already attached) when a Win32 call fails.
bool append_items(HMENU menu, std::span<const MenuItem> items, const Editor& editor, const MenuKeyTexts& keys) {
  for (const MenuItem& item : items) {
    MENUITEMINFOW mii{};
    mii.cbSize = sizeof(mii);
    std::wstring text;
    HMENU submenu = nullptr;
    if (item.kind == MenuItemKind::kSeparator) {
      mii.fMask = MIIM_FTYPE;
      mii.fType = MFT_SEPARATOR;
    } else {
      const MenuItemState state = menu_item_state(editor, item, keys);
      text = menu_item_text(item, state.key_text);
      mii.fMask = MIIM_FTYPE | MIIM_ID;
      mii.fType = item.check == MenuCheck::kRadio ? MFT_RADIOCHECK : MFT_STRING;
      mii.wID = menu_command_id(item);
      set_text_and_state(mii, text, state);
      if (item.kind == MenuItemKind::kSubmenu) {
        submenu = CreatePopupMenu();
        if (submenu == nullptr) return false;
        if (!append_items(submenu, item.submenu, editor, keys)) {
          DestroyMenu(submenu);
          return false;
        }
        mii.fMask |= MIIM_SUBMENU;
        mii.hSubMenu = submenu;
      }
    }
    if (!InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &mii)) {
      if (submenu != nullptr) DestroyMenu(submenu);
      return false;
    }
  }
  return true;
}

void read_items(HMENU menu, const std::wstring& prefix, std::vector<MenuEntry>* out) {
  const int count = GetMenuItemCount(menu);
  for (int i = 0; i < count; ++i) {
    wchar_t buffer[256] = {};
    MENUITEMINFOW mii{};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING;
    mii.dwTypeData = buffer;
    mii.cch = static_cast<UINT>(std::size(buffer));
    if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &mii)) continue;
    MenuEntry e;
    e.id = mii.wID;
    e.separator = (mii.fType & MFT_SEPARATOR) != 0;
    e.submenu = mii.hSubMenu != nullptr;
    e.enabled = (mii.fState & MFS_DISABLED) == 0;
    e.checked = (mii.fState & MFS_CHECKED) != 0;
    e.radio = (mii.fType & MFT_RADIOCHECK) != 0;
    if (!e.separator) {
      const std::wstring full = buffer;
      const size_t tab = full.find(L'\t');
      e.text = full.substr(0, tab);
      // The key as the core writes it ("&&" in the menu text is one '&').
      if (tab != std::wstring::npos) e.key_text = menu_text_without_mnemonic(full.substr(tab + 1));
    }
    const std::wstring name = e.separator ? std::wstring(L"-") : menu_text_without_mnemonic(e.text);
    e.path = prefix.empty() ? name : prefix + L" > " + name;
    out->push_back(e);
    if (e.submenu) read_items(mii.hSubMenu, e.path, out);
  }
}

}  // namespace

UINT menu_command_id(const MenuItem& item) {
  const int id = menu_item_id(item);
  return id > 0 ? kMenuCommandBase + static_cast<UINT>(id) : 0;
}

const MenuItem* menu_item_for_command(UINT id) {
  if (id <= kMenuCommandBase) return nullptr;
  return menu_item_for_id(static_cast<int>(id - kMenuCommandBase));
}

std::wstring menu_item_text(const MenuItem& item, const std::wstring& key_text) {
  std::wstring text = item.text != nullptr ? item.text : L"";
  if (key_text.empty()) return text;
  // A '&' in a key ("C-c &") is shown as itself, not as a mnemonic (R2).
  text += L'\t';
  for (const wchar_t c : key_text) {
    if (c == L'&') text += L'&';
    text += c;
  }
  return text;
}

HMENU create_menu_bar(const Editor& editor) {
  HMENU bar = CreateMenu();
  if (bar == nullptr) return nullptr;
  const MenuKeyTexts keys(editor);
  for (const Menu& menu : menu_bar_menus()) {
    HMENU popup = CreatePopupMenu();
    if (popup == nullptr || !append_items(popup, menu.items, editor, keys)) {
      const DWORD error = GetLastError();
      if (popup != nullptr) DestroyMenu(popup);
      DestroyMenu(bar);
      SetLastError(error);
      return nullptr;
    }
    MENUITEMINFOW mii{};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_SUBMENU;
    mii.dwTypeData = const_cast<wchar_t*>(menu.title);
    mii.hSubMenu = popup;
    if (!InsertMenuItemW(bar, static_cast<UINT>(GetMenuItemCount(bar)), TRUE, &mii)) {
      const DWORD error = GetLastError();
      DestroyMenu(popup);
      DestroyMenu(bar);
      SetLastError(error);
      return nullptr;
    }
  }
  return bar;
}

HMENU create_context_menu(const Editor& editor) {
  HMENU popup = CreatePopupMenu();
  if (popup == nullptr) return nullptr;
  if (!append_items(popup, context_menu_items(), editor, MenuKeyTexts(editor))) {
    const DWORD error = GetLastError();
    DestroyMenu(popup);
    SetLastError(error);
    return nullptr;
  }
  return popup;
}

void update_menu(HMENU menu, const Editor& editor) {
  const MenuKeyTexts keys(editor);
  struct Walker {
    const Editor& editor;
    const MenuKeyTexts& keys;
    void walk(HMENU m) {
      const int count = GetMenuItemCount(m);
      for (int i = 0; i < count; ++i) {
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(m, static_cast<UINT>(i), TRUE, &mii)) continue;
        if (mii.hSubMenu != nullptr) walk(mii.hSubMenu);
        const MenuItem* item = menu_item_for_command(mii.wID);
        if (item == nullptr || (mii.fType & MFT_SEPARATOR) != 0) continue;
        const MenuItemState state = menu_item_state(editor, *item, keys);
        const std::wstring text = menu_item_text(*item, state.key_text);
        MENUITEMINFOW set{};
        set.cbSize = sizeof(set);
        set_text_and_state(set, text, state);
        SetMenuItemInfoW(m, static_cast<UINT>(i), TRUE, &set);
      }
    }
  };
  Walker{editor, keys}.walk(menu);
}

std::vector<MenuEntry> read_menu(HMENU menu, std::wstring_view prefix) {
  std::vector<MenuEntry> out;
  read_items(menu, std::wstring(prefix), &out);
  return out;
}

bool menu_bar_wraps(HMENU bar, HMENU in, WPARAM key, bool system_menu) {
  if (bar == nullptr || in == nullptr || !system_menu || (key != VK_LEFT && key != VK_RIGHT)) return false;
  if (in == bar) return true;
  const int count = GetMenuItemCount(bar);
  for (int i = 0; i < count; ++i) {
    if (GetSubMenu(bar, i) == in) return true;
  }
  return false;
}

}  // namespace geditor::win
