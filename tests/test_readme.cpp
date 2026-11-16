// [M6-3] README.md lists every registered command once and nothing else
// ("## コマンド一覧"), and its default key tables ("## 既定のキー") hold
// exactly the default bindings of src/core/keymap.cpp, keymap by keymap.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "commands.h"
#include "fake_host.h"
#include "key.h"
#include "keymap.h"
#include "unicode.h"

namespace {

std::wstring read_readme() {
  std::ifstream in(GEDITOR_SOURCE_DIR "/README.md", std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return geditor::widen(bytes);
}

std::vector<std::wstring> lines_of(const std::wstring& text) {
  std::vector<std::wstring> lines;
  std::wistringstream in(text);
  std::wstring line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

// Heading level of a Markdown heading line (0: not a heading).
int heading_level(const std::wstring& line) {
  size_t n = 0;
  while (n < line.size() && line[n] == L'#') ++n;
  return n > 0 && n < line.size() && line[n] == L' ' ? static_cast<int>(n) : 0;
}

// The lines under the heading line `heading` up to the next heading of the
// same or a higher level (none: the heading is missing).
std::optional<std::vector<std::wstring>> section(const std::vector<std::wstring>& lines, std::wstring_view heading) {
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i] != heading) continue;
    const int level = heading_level(lines[i]);
    std::vector<std::wstring> out;
    for (size_t j = i + 1; j < lines.size(); ++j) {
      const int l = heading_level(lines[j]);
      if (l > 0 && l <= level) break;
      out.push_back(lines[j]);
    }
    return out;
  }
  return std::nullopt;
}

// The cells of a table row ("| a | b |" -> {"a", "b"}); empty for any other
// line.
std::vector<std::wstring> cells_of(const std::wstring& line) {
  std::vector<std::wstring> cells;
  if (line.empty() || line[0] != L'|') return cells;
  size_t start = 1;
  for (;;) {
    const size_t bar = line.find(L'|', start);
    if (bar == std::wstring::npos) break;
    cells.push_back(line.substr(start, bar - start));
    start = bar + 1;
  }
  return cells;
}

// The `code spans` of a cell.
std::vector<std::wstring> code_spans(const std::wstring& cell) {
  std::vector<std::wstring> spans;
  size_t i = 0;
  for (;;) {
    const size_t open = cell.find(L'`', i);
    if (open == std::wstring::npos) break;
    const size_t close = cell.find(L'`', open + 1);
    if (close == std::wstring::npos) break;
    spans.push_back(cell.substr(open + 1, close - open - 1));
    i = close + 1;
  }
  return spans;
}

// A key sequence in the notation of key_sequence_to_string, with "ESC <key>"
// written as "M-<key>" (README shows ESC ESC ESC; the keymap binds M-ESC
// ESC). Empty when the notation does not parse.
std::wstring normalized_keys(const std::wstring& notation) {
  const std::optional<geditor::KeySequence> keys = geditor::parse_key_sequence(notation);
  if (!keys) return std::wstring();
  geditor::KeySequence out;
  for (size_t i = 0; i < keys->size(); ++i) {
    geditor::Key k = (*keys)[i];
    if (k == geditor::Key{geditor::kKeyEscape, 0} && i + 1 < keys->size()) {
      k = (*keys)[++i];
      k.mods |= geditor::kModMeta;
    }
    out.push_back(k);
  }
  return geditor::key_sequence_to_string(out);
}

using Pairs = std::multiset<std::pair<std::wstring, std::wstring>>;

// The (keys, command) rows of a key table section: the first cell holds one
// or more key sequences, the second the command.
Pairs readme_bindings(const std::vector<std::wstring>& lines) {
  Pairs pairs;
  for (const std::wstring& line : lines) {
    const std::vector<std::wstring> cells = cells_of(line);
    if (cells.size() < 2) continue;
    const std::vector<std::wstring> keys = code_spans(cells[0]);
    const std::vector<std::wstring> commands = code_spans(cells[1]);
    if (keys.empty()) continue;  // the header and the separator row
    const std::wstring command = commands.size() == 1 ? commands[0] : L"(not one command: " + cells[1] + L")";
    for (const std::wstring& k : keys) {
      const std::wstring n = normalized_keys(k);
      pairs.emplace(n.empty() ? L"(bad key " + k + L")" : n, command);
    }
  }
  return pairs;
}

Pairs default_bindings(std::span<const geditor::DefaultBinding> table) {
  Pairs pairs;
  for (const geditor::DefaultBinding& b : table) pairs.emplace(normalized_keys(b.keys), geditor::widen(b.command));
  return pairs;
}

// Prints what only one side has; true when both are the same.
bool same_pairs(const Pairs& readme, const Pairs& defaults, const char* what) {
  std::vector<std::pair<std::wstring, std::wstring>> only_readme;
  std::vector<std::pair<std::wstring, std::wstring>> only_defaults;
  std::set_difference(readme.begin(), readme.end(), defaults.begin(), defaults.end(),
                      std::back_inserter(only_readme));
  std::set_difference(defaults.begin(), defaults.end(), readme.begin(), readme.end(),
                      std::back_inserter(only_defaults));
  for (const auto& p : only_readme) {
    std::printf("  %s: README has %s -> %s, keymap.cpp does not\n", what, test::to_display(p.first).c_str(),
                test::to_display(p.second).c_str());
  }
  for (const auto& p : only_defaults) {
    std::printf("  %s: keymap.cpp has %s -> %s, README does not\n", what, test::to_display(p.first).c_str(),
                test::to_display(p.second).c_str());
  }
  return only_readme.empty() && only_defaults.empty();
}

}  // namespace

TEST(readme_command_list_matches_the_registry) {
  const std::vector<std::wstring> lines = lines_of(read_readme());
  const std::optional<std::vector<std::wstring>> list = section(lines, L"## コマンド一覧");
  CHECK(list.has_value());
  if (!list) return;
  std::multiset<std::wstring> readme;
  for (const std::wstring& line : *list) {
    const std::vector<std::wstring> cells = cells_of(line);
    if (cells.empty()) continue;
    const std::vector<std::wstring> names = code_spans(cells[0]);
    if (names.size() == 1) readme.insert(names[0]);
  }
  std::multiset<std::wstring> registered;
  for (const geditor::CommandInfo& c : geditor::all_commands()) registered.insert(geditor::widen(c.name));
  for (const std::wstring& name : readme) {
    if (registered.count(name) == 0) std::printf("  README lists %s, which is not registered\n", test::to_display(name).c_str());
    if (readme.count(name) > 1) std::printf("  README lists %s more than once\n", test::to_display(name).c_str());
  }
  for (const std::wstring& name : registered) {
    if (readme.count(name) == 0) std::printf("  README lacks %s\n", test::to_display(name).c_str());
  }
  CHECK(readme == registered);
}

TEST(readme_key_tables_match_the_default_keymaps) {
  const std::vector<std::wstring> lines = lines_of(read_readme());
  const std::optional<std::vector<std::wstring>> keys = section(lines, L"## 既定のキー");
  CHECK(keys.has_value());
  if (!keys) return;
  struct Table {
    const wchar_t* heading;
    std::span<const geditor::DefaultBinding> defaults;
    const char* what;
  };
  const Table tables[] = {
      {L"### 通常のキー", geditor::default_global_bindings(), "global"},
      {L"### Windows 互換キー", geditor::default_compat_bindings(), "Windows keys"},
      {L"### ミニバッファの中", geditor::default_minibuffer_bindings(), "minibuffer"},
      {L"### isearch の中", geditor::default_isearch_bindings(), "isearch"},
      {L"### 問い合わせ置換の中", geditor::default_query_replace_bindings(), "query-replace"},
  };
  for (const Table& t : tables) {
    const std::optional<std::vector<std::wstring>> rows = section(*keys, t.heading);
    if (!rows) std::printf("  README has no heading %s\n", test::to_display(t.heading).c_str());
    CHECK(rows.has_value());
    if (!rows) continue;
    CHECK(same_pairs(readme_bindings(*rows), default_bindings(t.defaults), t.what));
  }
}
