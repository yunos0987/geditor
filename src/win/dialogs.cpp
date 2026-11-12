#include "dialogs.h"

#include <commctrl.h>

#include "path.h"

namespace geditor::win {
namespace {

constexpr wchar_t kTitle[] = L"geditor";

// Exception codes of the delay-load helper when comctl32 or the function is
// missing (VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND / ...)).
constexpr DWORD kDelayLoadModuleNotFound = 0xC06D007E;
constexpr DWORD kDelayLoadProcNotFound = 0xC06D007F;

int delay_load_filter(DWORD code) {
  return code == kDelayLoadModuleNotFound || code == kDelayLoadProcNotFound
             ? EXCEPTION_EXECUTE_HANDLER
             : EXCEPTION_CONTINUE_SEARCH;
}

// No C++ objects with destructors here: the function uses SEH.
HRESULT call_task_dialog(const TASKDIALOGCONFIG* config, int* button) {
  __try {
    return TaskDialogIndirect(config, button, nullptr, nullptr);
  } __except (delay_load_filter(GetExceptionCode())) {
    return E_FAIL;
  }
}

// Shows a task dialog; returns the pressed button id (IDCANCEL for Esc and
// the close button), or 0 when the dialog could not be shown.
int show_task_dialog(HWND owner, const std::wstring& instruction, const std::wstring& content,
                     const TASKDIALOG_BUTTON* buttons, UINT button_count,
                     TASKDIALOG_COMMON_BUTTON_FLAGS common_buttons, int default_button) {
  TASKDIALOGCONFIG config{};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  config.dwCommonButtons = common_buttons;
  config.pszWindowTitle = kTitle;
  config.pszMainInstruction = instruction.c_str();
  config.pszContent = content.empty() ? nullptr : content.c_str();
  config.pButtons = buttons;
  config.cButtons = button_count;
  config.nDefaultButton = default_button;
  int pressed = 0;
  if (FAILED(call_task_dialog(&config, &pressed))) return 0;
  return pressed;
}

constexpr int kOpenHere = 101;
constexpr int kOpenNewWindow = 102;
constexpr int kSave = 201;
constexpr int kDontSave = 202;

}  // namespace

OpenChoice ask_open_choice(HWND owner, const std::wstring& path) {
  const TASKDIALOG_BUTTON buttons[] = {
      {kOpenHere, L"Open in this window"},
      {kOpenNewWindow, L"Open in a new window"},
      {IDCANCEL, L"Cancel"},
  };
  const std::wstring instruction = L"Open " + file_name_of(path) + L"?";
  switch (show_task_dialog(owner, instruction, path, buttons, 3, 0, kOpenHere)) {
    case kOpenHere: return OpenChoice::kReplace;
    case kOpenNewWindow: return OpenChoice::kNewWindow;
    case IDCANCEL: return OpenChoice::kCancel;
    default: break;
  }
  const std::wstring text = instruction + L"\n" + path +
                            L"\n\nYes: open in this window\nNo: open in a new window";
  switch (MessageBoxW(owner, text.c_str(), kTitle, MB_YESNOCANCEL | MB_ICONQUESTION)) {
    case IDYES: return OpenChoice::kReplace;
    case IDNO: return OpenChoice::kNewWindow;
    default: return OpenChoice::kCancel;
  }
}

SaveChoice ask_save_changes(HWND owner, const std::wstring& buffer_name) {
  const TASKDIALOG_BUTTON buttons[] = {
      {kSave, L"Save"},
      {kDontSave, L"Don't Save"},
      {IDCANCEL, L"Cancel"},
  };
  const std::wstring instruction = L"Save changes to " + buffer_name + L"?";
  switch (show_task_dialog(owner, instruction, std::wstring(), buttons, 3, 0, kSave)) {
    case kSave: return SaveChoice::kSave;
    case kDontSave: return SaveChoice::kDontSave;
    case IDCANCEL: return SaveChoice::kCancel;
    default: break;
  }
  const std::wstring text = instruction + L"\n\nYes: save\nNo: don't save";
  switch (MessageBoxW(owner, text.c_str(), kTitle, MB_YESNOCANCEL | MB_ICONWARNING)) {
    case IDYES: return SaveChoice::kSave;
    case IDNO: return SaveChoice::kDontSave;
    default: return SaveChoice::kCancel;
  }
}

bool ask_yes_no(HWND owner, const std::wstring& question) {
  // Custom buttons: the common buttons would be labelled in the UI language
  // of Windows, while geditor's dialogs are English (docs/PLAN.md 2).
  const TASKDIALOG_BUTTON buttons[] = {
      {IDYES, L"Yes"},
      {IDNO, L"No"},
  };
  switch (show_task_dialog(owner, question, std::wstring(), buttons, 2, 0, IDNO)) {
    case IDYES: return true;
    case IDNO:
    case IDCANCEL:  // Esc or the close button
      return false;
    default: break;
  }
  return MessageBoxW(owner, question.c_str(), kTitle,
                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

}  // namespace geditor::win
