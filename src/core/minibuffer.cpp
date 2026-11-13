#include "minibuffer.h"

#include <algorithm>

#include "path.h"
#include "unicode.h"

namespace geditor {
namespace {

// Completes `name` (the text after `dir_part` in `input`) against `entries`.
FileCompletion complete_entries(std::wstring_view input, const std::wstring& dir_part,
                                const std::wstring& name, std::vector<DirEntry>& entries) {
  FileCompletion result;
  result.text = input;
  std::vector<DirEntry> matches;
  for (DirEntry& e : entries) {
    if (e.name == L"." || e.name == L"..") continue;
    if (fold_starts_with(e.name, name)) matches.push_back(std::move(e));
  }
  if (matches.empty()) {
    result.message = L"[No match]";
    return result;
  }
  std::sort(matches.begin(), matches.end(), [](const DirEntry& a, const DirEntry& b) {
    const int c = fold_compare(a.name, b.name);
    return c != 0 ? c < 0 : a.name < b.name;
  });
  if (matches.size() == 1) {
    const DirEntry& m = matches.front();
    const std::wstring full = dir_part + m.name + (m.is_directory ? L"\\" : L"");
    if (full == input) {
      result.message = L"[Sole completion]";
    } else {
      result.text = full;
    }
    return result;
  }
  // Case-insensitive common prefix of all candidates.
  size_t common = matches.front().name.size();
  for (const DirEntry& m : matches) {
    size_t i = 0;
    const std::wstring& first = matches.front().name;
    while (i < common && i < m.name.size() &&
           to_lower(static_cast<char32_t>(first[i])) == to_lower(static_cast<char32_t>(m.name[i]))) {
      ++i;
    }
    common = i;
  }
  // Never end the completed text between the halves of a surrogate pair.
  if (common > 0 && is_high_surrogate(matches.front().name[common - 1])) --common;
  if (common > name.size()) {
    result.text = dir_part + matches.front().name.substr(0, common);
    return result;
  }
  // The list is capped so that the echo area stays small for big folders.
  std::wstring list = L"{";
  size_t shown = 0;
  for (; shown < matches.size(); ++shown) {
    if (shown >= kMaxListedCandidates || list.size() >= kMaxCandidateListChars) break;
    if (shown > 0) list += L" | ";
    list += matches[shown].name;
    if (matches[shown].is_directory) list += L'\\';
  }
  if (shown < matches.size()) {
    list += L" | ... (" + std::to_wstring(matches.size()) + L" candidates)";
  }
  list += L'}';
  result.message = list;
  return result;
}

}  // namespace

FileCompletion complete_file_name(std::wstring_view input, Host& host) {
  const size_t sep = input.find_last_of(L"\\/");
  const std::wstring dir_part(sep == std::wstring_view::npos ? std::wstring_view() : input.substr(0, sep + 1));
  const std::wstring name(sep == std::wstring_view::npos ? input : input.substr(sep + 1));
  const std::wstring cwd = host.current_directory();
  const std::wstring list_dir = dir_part.empty() ? cwd : normalize_path(dir_part, cwd);

  std::vector<DirEntry> entries;
  if (!host.list_directory(list_dir, entries)) {
    FileCompletion result;
    result.text = input;
    result.message = L"[No match]";
    return result;
  }
  return complete_entries(input, dir_part, name, entries);
}

FileCompletion complete_name(std::wstring_view input, std::vector<std::wstring> names) {
  std::vector<DirEntry> entries;
  entries.reserve(names.size());
  for (std::wstring& n : names) entries.push_back(DirEntry{std::move(n), false});
  return complete_entries(input, std::wstring(), std::wstring(input), entries);
}

void History::add(std::wstring_view entry) {
  if (entry.empty()) return;
  if (!entries_.empty() && entries_.back() == entry) return;
  entries_.emplace_back(entry);
}

bool History::previous(const std::wstring& current, std::wstring* out) {
  if (position_ + 1 >= static_cast<int>(entries_.size())) return false;
  if (position_ == -1) saved_input_ = current;
  ++position_;
  *out = entries_[entries_.size() - 1 - static_cast<size_t>(position_)];
  return true;
}

bool History::next(std::wstring* out) {
  if (position_ < 0) return false;
  --position_;
  *out = position_ < 0 ? saved_input_ : entries_[entries_.size() - 1 - static_cast<size_t>(position_)];
  return true;
}

}  // namespace geditor
