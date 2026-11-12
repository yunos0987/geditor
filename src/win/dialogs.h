// Modal dialogs of the Host (English text, docs/PLAN.md 2 "screen language").
// They use TaskDialogIndirect (comctl32 v6, delay-loaded) and fall back to
// MessageBoxW when comctl32 cannot be loaded.
#pragma once

#include <string>

#include <windows.h>

#include "host.h"

namespace geditor::win {

// "Open in this window" / "Open in a new window" / "Cancel".
OpenChoice ask_open_choice(HWND owner, const std::wstring& path);
// "Save" / "Don't Save" / "Cancel".
SaveChoice ask_save_changes(HWND owner, const std::wstring& buffer_name);
// "Yes" / "No".
bool ask_yes_no(HWND owner, const std::wstring& question);

}  // namespace geditor::win
