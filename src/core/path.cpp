#include "path.h"

#include <vector>

#include "unicode.h"

namespace geditor {
namespace {

bool is_sep(wchar_t c) {
  return c == L'\\' || c == L'/';
}

bool has_drive(std::wstring_view p) {
  return p.size() >= 2 && p[1] == L':' &&
         ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z'));
}

}  // namespace

bool is_absolute_path(std::wstring_view p) {
  if (has_drive(p)) return p.size() >= 3 && is_sep(p[2]);
  return p.size() >= 2 && is_sep(p[0]) && is_sep(p[1]);
}

std::wstring normalize_path(std::wstring_view path, std::wstring_view base_dir) {
  std::wstring full;
  if (is_absolute_path(path)) {
    full = path;
  } else if (!path.empty() && is_sep(path[0])) {
    // Root of the base directory's drive.
    full = std::wstring(base_dir.substr(0, has_drive(base_dir) ? 2 : 0)) + std::wstring(path);
  } else if (has_drive(path)) {
    // "C:foo" (drive-relative) is treated as "C:\foo".
    full = std::wstring(path.substr(0, 2)) + L"\\" + std::wstring(path.substr(2));
  } else {
    full = with_trailing_separator(base_dir) + std::wstring(path);
  }

  std::wstring root;
  size_t pos = 0;
  if (has_drive(full)) {
    root = full.substr(0, 2);
    root[0] = static_cast<wchar_t>(to_upper(root[0]));
    root += L'\\';
    pos = 2;
  } else if (full.size() >= 2 && is_sep(full[0]) && is_sep(full[1])) {
    root = L"\\\\";
    pos = 2;
  }
  std::vector<std::wstring> parts;
  while (pos < full.size()) {
    while (pos < full.size() && is_sep(full[pos])) ++pos;
    size_t end = pos;
    while (end < full.size() && !is_sep(full[end])) ++end;
    if (end > pos) {
      const std::wstring part = full.substr(pos, end - pos);
      if (part == L"..") {
        if (!parts.empty()) parts.pop_back();
      } else if (part != L".") {
        parts.push_back(part);
      }
    }
    pos = end;
  }
  std::wstring out = root;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += L'\\';
    out += parts[i];
  }
  return out;
}

std::wstring directory_of(std::wstring_view path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring_view::npos) return std::wstring();
  std::wstring dir(path.substr(0, pos + 1));
  for (wchar_t& c : dir) {
    if (c == L'/') c = L'\\';
  }
  return dir;
}

std::wstring file_name_of(std::wstring_view path) {
  const size_t pos = path.find_last_of(L"\\/");
  return std::wstring(pos == std::wstring_view::npos ? path : path.substr(pos + 1));
}

std::wstring with_trailing_separator(std::wstring_view dir) {
  std::wstring s(dir);
  if (s.empty() || !is_sep(s.back())) s += L'\\';
  return s;
}

bool same_path(std::wstring_view a, std::wstring_view b) {
  return fold_equal(a, b);
}

}  // namespace geditor
