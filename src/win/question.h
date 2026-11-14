// The questions of the Host: "open in this window / a new window",
// "save changes" and yes/no. Their text and buttons are built here, once,
// and used both by the task dialogs (dialogs.cpp) and by the headless
// question channel (window.cpp, --headless), so that a tool reads exactly
// the text and labels that the dialog would show (English, docs/PLAN.md 2
// "screen language"; custom buttons, because the common buttons of a task
// dialog are labelled in the UI language of Windows).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include <commctrl.h>

#include "host.h"
#include "menu_bar.h"
#include "path.h"

namespace geditor::win {

// kMenuBar / kContextMenu: a menu that would open in a visible window
// (menu-bar-open, WM_CONTEXTMENU), pending in the headless channel instead
// (decision B7); its buttons are the enabled command items.
enum class QuestionKind { kOpenChoice, kSaveChanges, kYesNo, kMenuBar, kContextMenu };

struct QuestionButton {
  int id;
  std::wstring label;
};

struct Question {
  QuestionKind kind = QuestionKind::kYesNo;
  std::wstring instruction;  // the main instruction (first line)
  std::wstring content;      // the line below it; empty: none
  std::vector<QuestionButton> buttons;
  int default_button = 0;
  // Menus: every item as read back from the built HMENU, and where the menu
  // would open (client pixels).
  std::vector<MenuEntry> menu;
  POINT menu_position{};
  // The menu bar: the submenu that a visible window shows dropped down when
  // menu-bar-open enters the bar (decision G7: the first one, File).
  std::wstring menu_open;
};

// Button ids (IDCANCEL / IDYES / IDNO for the standard meanings).
inline constexpr int kButtonOpenHere = 101;
inline constexpr int kButtonOpenNewWindow = 102;
inline constexpr int kButtonSave = 201;
inline constexpr int kButtonDontSave = 202;

inline Question open_choice_question(const std::wstring& path) {
  Question q;
  q.kind = QuestionKind::kOpenChoice;
  q.instruction = L"Open " + file_name_of(path) + L"?";
  q.content = path;
  q.buttons = {{kButtonOpenHere, L"Open in this window"},
               {kButtonOpenNewWindow, L"Open in a new window"},
               {IDCANCEL, L"Cancel"}};
  q.default_button = kButtonOpenHere;
  return q;
}

inline Question save_changes_question(const std::wstring& buffer_name) {
  Question q;
  q.kind = QuestionKind::kSaveChanges;
  q.instruction = L"Save changes to " + buffer_name + L"?";
  q.buttons = {{kButtonSave, L"Save"}, {kButtonDontSave, L"Don't Save"}, {IDCANCEL, L"Cancel"}};
  q.default_button = kButtonSave;
  return q;
}

inline Question yes_no_question(const std::wstring& question) {
  Question q;
  q.kind = QuestionKind::kYesNo;
  q.instruction = question;
  q.buttons = {{IDYES, L"Yes"}, {IDNO, L"No"}};
  q.default_button = IDNO;
  return q;
}

// A menu as a question: the buttons are the enabled command items, labelled
// with their path ("Edit > Undo", or "Undo" in the right-click menu), and
// "Cancel" (IDCANCEL, as Esc in a menu). Ids are the WM_COMMAND ids.
inline Question menu_question(QuestionKind kind, std::vector<MenuEntry> entries, POINT position) {
  Question q;
  q.kind = kind;
  q.instruction = kind == QuestionKind::kMenuBar ? L"Menu bar" : L"Context menu";
  for (const MenuEntry& e : entries) {
    if (!e.separator && !e.submenu && e.enabled && e.id != 0) {
      q.buttons.push_back(QuestionButton{static_cast<int>(e.id), e.path});
    }
  }
  q.buttons.push_back(QuestionButton{IDCANCEL, L"Cancel"});
  q.default_button = IDCANCEL;
  if (kind == QuestionKind::kMenuBar) {
    for (const MenuEntry& e : entries) {
      if (e.submenu && e.path.find(L" > ") == std::wstring::npos) {
        q.menu_open = e.path;
        break;
      }
    }
  }
  q.menu = std::move(entries);
  q.menu_position = position;
  return q;
}

inline const wchar_t* question_kind_name(QuestionKind kind) {
  switch (kind) {
    case QuestionKind::kOpenChoice: return L"open_choice";
    case QuestionKind::kSaveChanges: return L"save_changes";
    case QuestionKind::kYesNo: return L"yes_no";
    case QuestionKind::kMenuBar: return L"menu_bar";
    case QuestionKind::kContextMenu: return L"context_menu";
  }
  return L"unknown";
}

// The id of the button labelled exactly `label`; 0 when there is none.
inline int question_button_id(const Question& q, std::wstring_view label) {
  for (const QuestionButton& b : q.buttons) {
    if (label == b.label) return b.id;
  }
  return 0;
}

// The answer of a pressed button; nullopt when `id` is no answer (0: the
// dialog could not be shown). IDCANCEL also stands for Esc and the close
// button of the task dialog.
inline std::optional<OpenChoice> open_choice_for(int id) {
  switch (id) {
    case kButtonOpenHere: return OpenChoice::kReplace;
    case kButtonOpenNewWindow: return OpenChoice::kNewWindow;
    case IDCANCEL: return OpenChoice::kCancel;
    default: return std::nullopt;
  }
}

inline std::optional<SaveChoice> save_choice_for(int id) {
  switch (id) {
    case kButtonSave: return SaveChoice::kSave;
    case kButtonDontSave: return SaveChoice::kDontSave;
    case IDCANCEL: return SaveChoice::kCancel;
    default: return std::nullopt;
  }
}

inline std::optional<bool> yes_no_for(int id) {
  switch (id) {
    case IDYES: return true;
    case IDNO:
    case IDCANCEL: return false;
    default: return std::nullopt;
  }
}

// The custom buttons of the task dialog (the labels point into `q`).
inline std::vector<TASKDIALOG_BUTTON> task_dialog_buttons(const Question& q) {
  std::vector<TASKDIALOG_BUTTON> buttons;
  buttons.reserve(q.buttons.size());
  for (const QuestionButton& b : q.buttons) buttons.push_back(TASKDIALOG_BUTTON{b.id, b.label.c_str()});
  return buttons;
}

// The task dialog of `q` with `buttons` from task_dialog_buttons(q); the
// strings point into `q` and `title`.
inline TASKDIALOGCONFIG task_dialog_config(const Question& q, const std::vector<TASKDIALOG_BUTTON>& buttons,
                                           HWND owner, const wchar_t* title) {
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  config.dwCommonButtons = 0;
  config.pszWindowTitle = title;
  config.pszMainInstruction = q.instruction.c_str();
  config.pszContent = q.content.empty() ? nullptr : q.content.c_str();
  config.pButtons = buttons.data();
  config.cButtons = static_cast<UINT>(buttons.size());
  config.nDefaultButton = q.default_button;
  return config;
}

}  // namespace geditor::win
