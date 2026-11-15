#include "menu.h"

#include "commands.h"
#include "config.h"
#include "display.h"
#include "editor.h"

namespace geditor {
namespace {

constexpr MenuItem command_item(const wchar_t* text, const char* command, MenuCheck check = MenuCheck::kNone,
                                const wchar_t* argument = nullptr) {
  return MenuItem{MenuItemKind::kCommand, text, command, argument, check, {}};
}

constexpr MenuItem separator() {
  return MenuItem{};
}

constexpr MenuItem submenu_item(const wchar_t* text, std::span<const MenuItem> items) {
  return MenuItem{MenuItemKind::kSubmenu, text, nullptr, nullptr, MenuCheck::kNone, items};
}

// The 9 encodings of PLAN 2 (the names of encoding_names()), radio items of
// set-buffer-file-coding-system.
constexpr MenuItem kEncodingItems[] = {
    command_item(L"utf-8", "set-buffer-file-coding-system", MenuCheck::kRadio, L"utf-8"),
    command_item(L"utf-8-bom", "set-buffer-file-coding-system", MenuCheck::kRadio, L"utf-8-bom"),
    command_item(L"utf-16le", "set-buffer-file-coding-system", MenuCheck::kRadio, L"utf-16le"),
    command_item(L"utf-16le-bom", "set-buffer-file-coding-system", MenuCheck::kRadio, L"utf-16le-bom"),
    command_item(L"utf-16be", "set-buffer-file-coding-system", MenuCheck::kRadio, L"utf-16be"),
    command_item(L"utf-16be-bom", "set-buffer-file-coding-system", MenuCheck::kRadio, L"utf-16be-bom"),
    command_item(L"shift_jis", "set-buffer-file-coding-system", MenuCheck::kRadio, L"shift_jis"),
    command_item(L"euc-jp", "set-buffer-file-coding-system", MenuCheck::kRadio, L"euc-jp"),
    command_item(L"iso-2022-jp", "set-buffer-file-coding-system", MenuCheck::kRadio, L"iso-2022-jp"),
};

constexpr MenuItem kLineEndingItems[] = {
    command_item(L"&LF", "set-eol", MenuCheck::kRadio, L"lf"),
    command_item(L"C&RLF", "set-eol", MenuCheck::kRadio, L"crlf"),
    command_item(L"&CR", "set-eol", MenuCheck::kRadio, L"cr"),
};

constexpr MenuItem kFileItems[] = {
    command_item(L"&Open...", "find-file"),
    command_item(L"Open with &Encoding...", "find-file-with-encoding"),
    command_item(L"&Reopen with Encoding...", "revert-buffer-with-coding-system"),
    separator(),
    command_item(L"&Save", "save-buffer"),
    command_item(L"Save &As...", "write-file"),
    separator(),
    submenu_item(L"Encodin&g for Saving", kEncodingItems),
    submenu_item(L"&Line Endings", kLineEndingItems),
    separator(),
    command_item(L"Rea&d Only", "read-only-mode", MenuCheck::kCheck),
    separator(),
    command_item(L"Se&ttings", "find-user-init-file"),
    command_item(L"E&xit", "quit"),
};

// The first kContextItemCount items are the right-click menu (decision B6).
constexpr MenuItem kEditItems[] = {
    command_item(L"&Undo", "undo"),
    command_item(L"&Redo", "redo"),
    separator(),
    command_item(L"Cu&t", "cut"),
    command_item(L"&Copy", "copy"),
    command_item(L"&Paste", "paste"),
    command_item(L"Select &All", "select-all"),
    separator(),
    command_item(L"&Find...", "isearch-forward"),
    command_item(L"R&eplace...", "query-replace"),
    command_item(L"&Go to Line...", "goto-line"),
};
constexpr size_t kContextItemCount = 7;

constexpr MenuItem kWrapItems[] = {
    command_item(L"&None", "set-wrap-mode", MenuCheck::kRadio, L"none"),
    command_item(L"&Window", "set-wrap-mode", MenuCheck::kRadio, L"window"),
    command_item(L"&Fixed", "set-wrap-mode", MenuCheck::kRadio, L"fixed"),
    command_item(L"&Max", "set-wrap-mode", MenuCheck::kRadio, L"max"),
};

constexpr MenuItem kViewItems[] = {
    command_item(L"Line &Numbers", "toggle-line-numbers", MenuCheck::kCheck),
    command_item(L"Line &Ending Marks", "toggle-eol-marks", MenuCheck::kCheck),
    command_item(L"&Whitespace Marks", "toggle-whitespace-marks", MenuCheck::kCheck),
    submenu_item(L"Wr&ap", kWrapItems),
    separator(),
    command_item(L"Zoom &In", "text-scale-increase"),
    command_item(L"Zoom &Out", "text-scale-decrease"),
    command_item(L"&Reset Zoom", "text-scale-reset"),
};

constexpr Menu kMenus[] = {
    {L"&File", kFileItems},
    {L"&Edit", kEditItems},
    {L"&View", kViewItems},
};

// Walks the items depth first; f(item, id) returns true to stop. Returns
// true when stopped.
template <typename F>
bool walk_items(std::span<const MenuItem> items, int* next_id, F&& f) {
  for (const MenuItem& item : items) {
    const int id = (*next_id)++;
    if (f(item, id)) return true;
    if (item.kind == MenuItemKind::kSubmenu && walk_items(item.submenu, next_id, f)) return true;
  }
  return false;
}

template <typename F>
void walk_menus(F&& f) {
  int next_id = 1;
  for (const Menu& menu : kMenus) {
    if (walk_items(menu.items, &next_id, f)) return;
  }
}

}  // namespace

std::span<const Menu> menu_bar_menus() {
  return kMenus;
}

std::span<const MenuItem> context_menu_items() {
  return std::span<const MenuItem>(kEditItems).first(kContextItemCount);
}

int menu_item_id(const MenuItem& item) {
  int found = 0;
  walk_menus([&](const MenuItem& i, int id) {
    if (&i != &item) return false;
    found = id;
    return true;
  });
  return found;
}

const MenuItem* menu_item_for_id(int id) {
  const MenuItem* found = nullptr;
  if (id <= 0) return nullptr;
  walk_menus([&](const MenuItem& i, int item_id) {
    if (item_id != id) return false;
    found = &i;
    return true;
  });
  return found;
}

std::wstring menu_text_without_mnemonic(std::wstring_view text) {
  std::wstring out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == L'&' && i + 1 < text.size()) ++i;
    out += text[i];
  }
  return out;
}

CommandCall menu_item_call(const MenuItem& item) {
  CommandCall call;
  if (item.command != nullptr) call.name = item.command;
  if (item.argument != nullptr) {
    CommandArg arg;
    arg.text = item.argument;
    call.args.push_back(std::move(arg));
  }
  return call;
}

MenuKeyTexts::MenuKeyTexts(const Editor& editor) {
  const Keymap& map = editor.keymap(KeymapKind::kGlobal);
  // A candidate counts when the keymap in effect runs exactly its binding
  // (a later layer may have rebound or unbound the keys).
  auto consider = [&](Choices* best, const KeySequence& keys, const Binding& binding) {
    if (keys.empty() || binding.sequence || binding.calls.size() != 1) return;
    const CommandCall& call = binding.calls.front();
    auto it = best->begin();
    while (it != best->end() && !(it->first == call)) ++it;
    if (it != best->end() && it->second.size() <= keys.size()) return;
    const Keymap::Lookup r = map.lookup(keys);
    if (r.result != Keymap::Result::kCommand || r.binding == nullptr || !(*r.binding == binding)) return;
    if (it != best->end()) {
      it->second = keys;
    } else {
      best->emplace_back(call, keys);
    }
  };
  // The Windows-side slot: settings (file order), then the Windows layer.
  for (const KeySetting& s : editor.config().keys[static_cast<size_t>(KeymapKind::kGlobal)]) {
    if (!s.unbind) consider(&windows_, s.keys, s.binding);
  }
  for (const DefaultBinding& b : compat_layer(KeymapKind::kGlobal)) {
    const std::optional<KeySequence> keys = parse_key_sequence(b.keys);
    if (keys) consider(&windows_, *keys, command_binding(b.command));
  }
  // The Emacs slot: the Emacs default layer.
  for (const DefaultBinding& b : emacs_layer(KeymapKind::kGlobal)) {
    const std::optional<KeySequence> keys = parse_key_sequence(b.keys);
    if (keys) consider(&emacs_, *keys, command_binding(b.command));
  }
}

namespace {

// Commands that do the same as a menu item's command, for the Emacs slot of
// the key text only (no binding changes; user decision 2026-11-15). Two-way.
// select-all and mark-whole-buffer differ only in where the cursor ends.
constexpr std::pair<const char*, const char*> kSameActionCommands[] = {
    {"cut", "kill-region"},
    {"copy", "kill-ring-save"},
    {"paste", "yank"},
    {"select-all", "mark-whole-buffer"},
};

}  // namespace

std::wstring MenuKeyTexts::text_for(const MenuItem& item) const {
  if (item.kind != MenuItemKind::kCommand) return std::wstring();
  const CommandCall call = menu_item_call(item);
  auto find = [](const Choices& best, const CommandCall& wanted) -> const KeySequence* {
    for (const auto& [c, keys] : best) {
      if (c == wanted) return &keys;
    }
    return nullptr;
  };
  const KeySequence* windows_keys = find(windows_, call);
  // The Emacs slot: the shortest key of the command itself or of a
  // same-action command (ties go to the command itself).
  const KeySequence* emacs_keys = find(emacs_, call);
  if (call.args.empty()) {
    for (const auto& [a, b] : kSameActionCommands) {
      const char* other = call.name == a ? b : call.name == b ? a : nullptr;
      if (other == nullptr) continue;
      const KeySequence* k = find(emacs_, CommandCall{other, {}});
      if (k != nullptr && (emacs_keys == nullptr || k->size() < emacs_keys->size())) emacs_keys = k;
    }
  }
  const std::wstring windows = windows_keys != nullptr ? key_sequence_to_string(*windows_keys) : std::wstring();
  const std::wstring emacs = emacs_keys != nullptr ? key_sequence_to_string(*emacs_keys) : std::wstring();
  if (windows.empty()) return emacs;
  if (emacs.empty() || emacs == windows) return windows;
  return windows + L"  " + emacs;
}

bool menu_item_enabled(const Editor& editor, const MenuItem& item) {
  if (editor.minibuffer_active()) return false;
  if (item.kind == MenuItemKind::kSeparator) return false;
  if (item.kind == MenuItemKind::kSubmenu) return true;
  const CommandInfo* info = item.command != nullptr ? find_command(item.command) : nullptr;
  return info != nullptr && info->fn != nullptr;
}

bool menu_item_checked(const Editor& editor, const MenuItem& item) {
  if (item.kind != MenuItemKind::kCommand || item.check == MenuCheck::kNone) return false;
  const std::string_view command = item.command;
  if (command == "toggle-line-numbers") return editor.line_numbers();
  if (command == "toggle-eol-marks") return editor.eol_marks();
  if (command == "toggle-whitespace-marks") return editor.whitespace_marks();
  if (command == "set-wrap-mode") {
    WrapMode mode = WrapMode::kNone;
    return item.argument != nullptr && parse_wrap_mode(item.argument, &mode) && mode == editor.wrap_mode();
  }
  // File menu (M4-9): Encoding for Saving marks the encoding of the next
  // save (set-buffer-file-coding-system, the detected or named encoding of
  // the file), Line Endings the default line end (also of a mixed file),
  // Read Only follows read-only-mode.
  if (command == "set-buffer-file-coding-system") {
    FileFormat format;
    return item.argument != nullptr && parse_encoding(item.argument, &format) &&
           format.codec == editor.buffer().format.codec && format.bom == editor.buffer().format.bom;
  }
  if (command == "set-eol") {
    Eol eol = Eol::kLf;
    return item.argument != nullptr && parse_eol(item.argument, &eol) && eol == editor.buffer().format.eol;
  }
  if (command == "read-only-mode") return editor.buffer().read_only;
  return false;
}

MenuItemState menu_item_state(const Editor& editor, const MenuItem& item, const MenuKeyTexts& keys) {
  MenuItemState s;
  s.enabled = menu_item_enabled(editor, item);
  s.checked = menu_item_checked(editor, item);
  s.key_text = keys.text_for(item);
  return s;
}

}  // namespace geditor
