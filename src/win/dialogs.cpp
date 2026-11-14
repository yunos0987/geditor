#include "dialogs.h"

#include <commctrl.h>

#include <optional>
#include <vector>

#include "question.h"

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

// Shows the task dialog of `question` (question.h builds its text and
// buttons); returns the pressed button id (IDCANCEL for Esc and the close
// button), or 0 when the dialog could not be shown.
int show_task_dialog(HWND owner, const Question& question) {
  const std::vector<TASKDIALOG_BUTTON> buttons = task_dialog_buttons(question);
  const TASKDIALOGCONFIG config = task_dialog_config(question, buttons, owner, kTitle);
  int pressed = 0;
  if (FAILED(call_task_dialog(&config, &pressed))) return 0;
  return pressed;
}

}  // namespace

OpenChoice ask_open_choice(HWND owner, const std::wstring& path) {
  const Question question = open_choice_question(path);
  if (const std::optional<OpenChoice> choice = open_choice_for(show_task_dialog(owner, question))) {
    return *choice;
  }
  const std::wstring& instruction = question.instruction;
  const std::wstring text = instruction + L"\n" + path +
                            L"\n\nYes: open in this window\nNo: open in a new window";
  switch (MessageBoxW(owner, text.c_str(), kTitle, MB_YESNOCANCEL | MB_ICONQUESTION)) {
    case IDYES: return OpenChoice::kReplace;
    case IDNO: return OpenChoice::kNewWindow;
    default: return OpenChoice::kCancel;
  }
}

SaveChoice ask_save_changes(HWND owner, const std::wstring& buffer_name) {
  const Question question = save_changes_question(buffer_name);
  if (const std::optional<SaveChoice> choice = save_choice_for(show_task_dialog(owner, question))) {
    return *choice;
  }
  const std::wstring& instruction = question.instruction;
  const std::wstring text = instruction + L"\n\nYes: save\nNo: don't save";
  switch (MessageBoxW(owner, text.c_str(), kTitle, MB_YESNOCANCEL | MB_ICONWARNING)) {
    case IDYES: return SaveChoice::kSave;
    case IDNO: return SaveChoice::kDontSave;
    default: return SaveChoice::kCancel;
  }
}

bool ask_yes_no(HWND owner, const std::wstring& question) {
  // Custom buttons (question.h): the common buttons would be labelled in the
  // UI language of Windows, while geditor's dialogs are English.
  if (const std::optional<bool> yes = yes_no_for(show_task_dialog(owner, yes_no_question(question)))) {
    return *yes;  // IDCANCEL (Esc or the close button) is "No"
  }
  return MessageBoxW(owner, question.c_str(), kTitle,
                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

}  // namespace geditor::win
