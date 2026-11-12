// Starting another geditor process ("Open in a new window").
#pragma once

#include <string>

namespace geditor::win {

// Path of the running executable.
std::wstring own_executable_path();

// Starts `exe` with `file` as its quoted argument. On failure `error` gets a
// message for the echo area.
bool launch_editor(const std::wstring& exe, const std::wstring& file, std::wstring& error);

}  // namespace geditor::win
