// Command line of geditor.exe: [+LINE] [file] (docs/PLAN.md 2 "startup
// arguments"; --encoding arrives in M4), plus the measurement flags.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace geditor::win {

struct CommandLine {
  std::wstring file;           // empty when no file is given
  size_t line = 0;             // 1-based line from +LINE; 0 when not given
  bool measure_startup = false;  // --measure-startup
  bool measure_input = false;    // --measure-input (tools/measure_input.py)
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

}  // namespace geditor::win
