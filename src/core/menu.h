// The menu model of docs/PLAN.md 4.9: the File / Edit / View menus as data,
// the right-click Edit menu (the first items of the Edit menu, decision B6),
// and what an item shows at a given moment: its key text from the current
// keymap (decision B1), whether it is enabled (decision B2) and its check or
// radio mark. The Win32 layer (src/win/menu_bar.*) builds its menus from
// this and nothing else.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "key.h"
#include "keymap.h"

namespace geditor {

class Editor;

enum class MenuItemKind { kCommand, kSeparator, kSubmenu };
enum class MenuCheck { kNone, kCheck, kRadio };

struct MenuItem {
  MenuItemKind kind = MenuItemKind::kSeparator;
  // The item text with its '&' mnemonic (decision B3); nullptr for a
  // separator.
  const wchar_t* text = nullptr;
  const char* command = nullptr;    // kCommand
  const wchar_t* argument = nullptr;  // the text argument of the command; nullptr: none
  MenuCheck check = MenuCheck::kNone;
  std::span<const MenuItem> submenu = {};  // kSubmenu
};

struct Menu {
  const wchar_t* title;  // with its '&' mnemonic
  std::span<const MenuItem> items;
};

// File, Edit and View (PLAN 4.9).
std::span<const Menu> menu_bar_menus();
// The right-click menu (PLAN 4.9): Undo / Redo / - / Cut / Copy / Paste /
// Select All, the same items (same objects) as the start of the Edit menu.
std::span<const MenuItem> context_menu_items();

// Every item gets an id from 1, depth first through File, Edit and View
// (separators and submenu items included). 0: not an item of the model.
int menu_item_id(const MenuItem& item);
const MenuItem* menu_item_for_id(int id);

// The text without the '&' of the mnemonic ("&Undo" -> "Undo"; "&&" -> "&").
std::wstring menu_text_without_mnemonic(std::wstring_view text);

// The command call of a command item (its argument as a text argument).
CommandCall menu_item_call(const MenuItem& item);

// The key text of each command call that has a key (decision B1, two slots
// since 2026-11-15). A candidate is a key sequence bound to exactly that call
// (a single call, not a sequence) that the global keymap now in effect still
// runs; a key whose binding is shadowed (rebound or unbound by a later layer)
// does not count.
// - Windows-side slot: the shortest candidate of the settings (file order)
//   and the Windows compatible keys; ties go to the settings.
// - Emacs slot: the shortest candidate of the Emacs default layer; ties go to
//   the first one in the table. For cut / copy / paste / select-all the
//   candidates also include the keys of kill-region / kill-ring-save / yank /
//   mark-whole-buffer (and back), the same-action table of menu.cpp (display
//   only; ties go to the item's own command).
// The text is "<windows>  <emacs>" (two spaces); just the one key when only
// one slot has a key or both slots have the same key; empty when neither.
class MenuKeyTexts {
 public:
  explicit MenuKeyTexts(const Editor& editor);
  // "C-S-s  C-x C-s"; empty when no key runs `item`.
  std::wstring text_for(const MenuItem& item) const;

 private:
  using Choices = std::vector<std::pair<CommandCall, KeySequence>>;
  Choices windows_;
  Choices emacs_;
};

// What a command item shows now.
struct MenuItemState {
  bool enabled = false;
  bool checked = false;  // check mark or radio dot
  std::wstring key_text;
};

// Decision B2: disabled when the command is registered as not implemented
// yet, or while the minibuffer is active; nothing else greys an item. Check
// and radio marks of the View menu follow the view state of the window
// (toggle-* / set-wrap-mode and the settings). In the File menu (M4-9),
// Encoding for Saving marks the buffer's encoding, Line Endings its default
// line end and Read Only follows read-only-mode.
MenuItemState menu_item_state(const Editor& editor, const MenuItem& item, const MenuKeyTexts& keys);
bool menu_item_enabled(const Editor& editor, const MenuItem& item);
bool menu_item_checked(const Editor& editor, const MenuItem& item);

}  // namespace geditor
