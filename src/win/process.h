// Starting another geditor process ("Open in a new window").
#pragma once

#include <string>

namespace geditor::win {

// Path of the running executable.
std::wstring own_executable_path();

// The command line of a new window: `exe`, then --headless when `headless`,
// then --settings=`settings` when not empty (new windows keep the settings
// file and the headless mode of this one), then `file`, each quoted.
std::wstring editor_command_line(const std::wstring& exe, const std::wstring& file,
                                 const std::wstring& settings, bool headless = false);

// Starts `exe` with editor_command_line. On failure `error` gets a message
// for the echo area.
bool launch_editor(const std::wstring& exe, const std::wstring& file, const std::wstring& settings,
                   std::wstring& error, bool headless = false);

}  // namespace geditor::win
