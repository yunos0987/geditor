// Command line of geditor.exe: [--encoding=NAME] [--settings=PATH] [+LINE]
// [file] (docs/PLAN.md 2 "startup arguments"), plus the measurement flags
// and --headless.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace geditor::win {

struct CommandLine {
  std::wstring file;           // empty when no file is given
  size_t line = 0;             // 1-based line from +LINE; 0 when not given
  std::wstring settings;       // PATH of --settings=PATH as written; empty when not given
  // NAME of --encoding=NAME (one of encoding_names(): the file is read in it
  // instead of detecting); empty when not given or not one of them (an
  // unknown name is reported in `errors`).
  std::wstring encoding;
  bool measure_startup = false;  // --measure-startup
  bool measure_input = false;    // --measure-input (tools/measure_input.py)
  // --headless: an off-screen window that never takes the foreground, a
  // private clipboard and dialogs answered through WM_COPYDATA, for the
  // tools and checks (docs/PLAN.md 2 "headless"; not shown to users).
  bool headless = false;
  std::vector<std::wstring> errors;  // messages for the echo area
};

// Splits a command line with the rules of the Microsoft C runtime: the
// program name ends at the closing quote or at white space; in arguments,
// quotes group, 2n backslashes before a quote give n backslashes, 2n+1 give n
// and a literal quote, and "" inside quotes gives a quote. The result starts
// with the program name.
std::vector<std::wstring> split_command_line(std::wstring_view command_line);

// Parses the whole command line (program name first, as GetCommandLineW).
CommandLine parse_command_line(std::wstring_view command_line);

// Quotes one argument so that split_command_line returns it unchanged.
std::wstring quote_argument(std::wstring_view argument);

// The settings file: --settings=PATH (a relative PATH is resolved against
// `current_dir`), else "<user_profile>\.geditor.json", else "" (no profile).
std::wstring settings_path_for(const CommandLine& command_line, std::wstring_view current_dir,
                               std::wstring_view user_profile);

}  // namespace geditor::win
