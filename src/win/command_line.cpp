#include "command_line.h"

#include "encoding.h"
#include "path.h"

namespace geditor::win {
namespace {

bool is_space(wchar_t c) {
  return c == L' ' || c == L'\t';
}

// Parses the digits of "+LINE"; false when `digits` is not a line number.
bool parse_line_number(std::wstring_view digits, size_t* line) {
  if (digits.empty() || digits.size() > 9) return false;
  size_t value = 0;
  for (const wchar_t d : digits) {
    if (d < L'0' || d > L'9') return false;
    value = value * 10 + static_cast<size_t>(d - L'0');
  }
  *line = value == 0 ? 1 : value;
  return true;
}

}  // namespace

std::vector<std::wstring> split_command_line(std::wstring_view s) {
  std::vector<std::wstring> args;
  size_t i = 0;
  const size_t n = s.size();

  // The program name: no escapes, only quotes.
  std::wstring program;
  if (i < n && s[i] == L'"') {
    ++i;
    while (i < n && s[i] != L'"') program += s[i++];
    if (i < n) ++i;
  } else {
    while (i < n && !is_space(s[i])) program += s[i++];
  }
  args.push_back(std::move(program));

  for (;;) {
    while (i < n && is_space(s[i])) ++i;
    if (i >= n) break;
    std::wstring arg;
    bool in_quotes = false;
    while (i < n) {
      const wchar_t c = s[i];
      if (is_space(c) && !in_quotes) break;
      if (c == L'\\') {
        const size_t start = i;
        while (i < n && s[i] == L'\\') ++i;
        const size_t count = i - start;
        if (i < n && s[i] == L'"') {
          arg.append(count / 2, L'\\');
          if (count % 2 == 1) {
            arg += L'"';
            ++i;
          }
        } else {
          arg.append(count, L'\\');
        }
        continue;
      }
      if (c == L'"') {
        if (in_quotes && i + 1 < n && s[i + 1] == L'"') {
          arg += L'"';
          i += 2;
          continue;
        }
        in_quotes = !in_quotes;
        ++i;
        continue;
      }
      arg += c;
      ++i;
    }
    args.push_back(std::move(arg));
  }
  return args;
}

CommandLine parse_command_line(std::wstring_view command_line) {
  CommandLine result;
  const std::vector<std::wstring> args = split_command_line(command_line);
  bool options_done = false;
  for (size_t i = 1; i < args.size(); ++i) {
    const std::wstring& a = args[i];
    if (!options_done) {
      if (a == L"--") {
        options_done = true;
        continue;
      }
      if (a == L"--measure-startup") {
        result.measure_startup = true;
        continue;
      }
      if (a == L"--measure-input") {
        result.measure_input = true;
        continue;
      }
      if (a == L"--headless") {
        result.headless = true;
        continue;
      }
      if (a.rfind(L"--settings", 0) == 0) {
        constexpr size_t kPrefix = sizeof("--settings=") - 1;
        if (a.size() > kPrefix && a[kPrefix - 1] == L'=') {
          result.settings = a.substr(kPrefix);
        } else {
          result.errors.push_back(L"Invalid option: " + a + L" (use --settings=PATH)");
        }
        continue;
      }
      if (a.rfind(L"--encoding", 0) == 0) {
        constexpr size_t kPrefix = sizeof("--encoding=") - 1;
        geditor::FileFormat format;
        if (a.size() <= kPrefix || a[kPrefix - 1] != L'=') {
          result.errors.push_back(L"Invalid option: " + a + L" (use --encoding=NAME)");
        } else if (!geditor::parse_encoding(std::wstring_view(a).substr(kPrefix), &format)) {
          result.errors.push_back(geditor::unknown_encoding_message(std::wstring_view(a).substr(kPrefix)));
        } else {
          result.encoding = a.substr(kPrefix);
        }
        continue;
      }
      if (a.size() > 1 && a[0] == L'+') {
        if (!parse_line_number(std::wstring_view(a).substr(1), &result.line)) {
          result.errors.push_back(L"Invalid line number: " + a);
        }
        continue;
      }
      if (a.size() > 1 && a[0] == L'-') {
        result.errors.push_back(L"Unknown option: " + a);
        continue;
      }
    }
    if (a.empty()) continue;
    if (result.file.empty()) {
      result.file = a;
    } else {
      result.errors.push_back(L"Only one file can be opened; ignored " + a);
    }
  }
  return result;
}

std::wstring quote_argument(std::wstring_view argument) {
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
    } else {
      out.append(backslashes, L'\\');
    }
    backslashes = 0;
    out += c;
  }
  out.append(backslashes * 2, L'\\');
  out += L'"';
  return out;
}

std::wstring settings_path_for(const CommandLine& command_line, std::wstring_view current_dir,
                               std::wstring_view user_profile) {
  if (!command_line.settings.empty()) return normalize_path(command_line.settings, current_dir);
  if (user_profile.empty()) return std::wstring();
  return with_trailing_separator(user_profile) + L".geditor.json";
}

}  // namespace geditor::win
