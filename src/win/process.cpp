#include "process.h"

#include <windows.h>

#include "command_line.h"
#include "file_io.h"

namespace geditor::win {

std::wstring own_executable_path() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (n == 0) return std::wstring();
    if (n < path.size()) {
      path.resize(n);
      return path;
    }
    path.resize(path.size() * 2);  // truncated: try a larger buffer
  }
}

bool launch_editor(const std::wstring& exe, const std::wstring& file, std::wstring& error) {
  if (exe.empty()) {
    error = L"Cannot start a new window: the program path is unknown";
    return false;
  }
  std::wstring command_line = quote_argument(exe) + L" " + quote_argument(file);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  if (!CreateProcessW(exe.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &startup, &info)) {
    error = L"Cannot start a new window: " + error_text(GetLastError());
    return false;
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return true;
}

}  // namespace geditor::win
