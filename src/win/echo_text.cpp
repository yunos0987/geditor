#include "echo_text.h"

#include <algorithm>
#include <vector>

#include "grapheme.h"

namespace geditor::win {
namespace {

constexpr wchar_t kEllipsis[] = L"\u2026";

struct Unit {
  size_t start;  // UTF-16 offset
  size_t length;
  int cells;
};

// The grapheme clusters of `s` (the core's segmentation and widths; TAB is
// 1 cell here): the echo line is never cut inside a cluster.
std::vector<Unit> split_units(std::wstring_view s) {
  std::vector<Unit> units;
  for_each_cluster(s, [&](const Cluster& c) {
    units.push_back(Unit{c.offset, c.length, c.width});
    return true;
  });
  return units;
}

// The longest prefix of `s` that fits in `cells`.
std::wstring head_fitting(std::wstring_view s, int cells) {
  int used = 0;
  size_t end = 0;
  for (const Unit& u : split_units(s)) {
    if (used + u.cells > cells) break;
    used += u.cells;
    end = u.start + u.length;
  }
  return std::wstring(s.substr(0, end));
}

// The longest suffix of `s` that fits in `cells`.
std::wstring tail_fitting(std::wstring_view s, int cells) {
  const std::vector<Unit> units = split_units(s);
  int used = 0;
  size_t start = s.size();
  for (size_t i = units.size(); i > 0; --i) {
    const Unit& u = units[i - 1];
    if (used + u.cells > cells) break;
    used += u.cells;
    start = u.start;
  }
  return std::wstring(s.substr(start));
}

// Start of the first absolute path in `s` ("X:\" or "\\"), or npos.
size_t path_start(std::wstring_view s) {
  for (size_t i = 0; i + 2 < s.size(); ++i) {
    const wchar_t c = s[i];
    const bool letter = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    if (letter && s[i + 1] == L':' && s[i + 2] == L'\\' && (i == 0 || s[i - 1] == L' ')) return i;
    if (c == L'\\' && s[i + 1] == L'\\' && (i == 0 || s[i - 1] == L' ')) return i;
  }
  return std::wstring_view::npos;
}

}  // namespace

int text_cells(std::wstring_view text) {
  int cells = 0;
  for (const Unit& u : split_units(text)) cells += u.cells;
  return cells;
}

std::wstring fit_message(std::wstring_view message, int columns) {
  if (columns <= 0) return std::wstring();
  if (text_cells(message) <= columns) return std::wstring(message);
  if (columns == 1) return kEllipsis;

  // Inside a path: keep "<text before>X:\" and "\<last component and rest>".
  const size_t start = path_start(message);
  if (start != std::wstring_view::npos) {
    const size_t first_sep = message.find(L'\\', start + 2);
    const size_t last_sep = message.rfind(L'\\');
    if (first_sep != std::wstring_view::npos && last_sep > first_sep) {
      std::wstring_view head = message.substr(0, first_sep + 1);
      std::wstring_view tail = message.substr(last_sep);
      const int head_cells = text_cells(head);
      const int tail_cells = text_cells(tail);
      if (head_cells + 1 + tail_cells <= columns) {
        // Show as much of the path's start as the room allows.
        const int room = columns - 1 - tail_cells;
        return head_fitting(message.substr(0, last_sep), room) + kEllipsis + std::wstring(tail);
      }
    }
  }
  // Generic: a third of the room for the start, the rest for the end.
  const int head_cells = (columns - 1) / 3;
  const int tail_cells = columns - 1 - head_cells;
  return head_fitting(message, head_cells) + kEllipsis + tail_fitting(message, tail_cells);
}

MinibufferLine fit_minibuffer(std::wstring_view prompt, std::wstring_view input, size_t cursor,
                              std::wstring_view message, int columns) {
  MinibufferLine line;
  if (cursor > input.size()) cursor = input.size();
  const int prompt_cells = text_cells(prompt);
  // One cell stays free for the cursor after the last character.
  int room = columns - prompt_cells - 1;
  std::wstring_view shown_prompt = prompt;
  std::wstring prompt_text(prompt);
  if (room < 8) {
    // A prompt wider than the line: shorten it too (rare).
    prompt_text = fit_message(prompt, columns / 2);
    shown_prompt = prompt_text;
    room = columns - text_cells(shown_prompt) - 1;
  }
  const int shown_prompt_cells = text_cells(shown_prompt);

  const std::vector<Unit> units = split_units(input);
  int total = 0;
  for (const Unit& u : units) total += u.cells;
  size_t first = 0;  // index in units of the first shown unit
  bool ellipsis = false;
  if (total > room) {
    // Units before the cursor that fit after the ellipsis, leaving room for
    // the character under the cursor (or the cursor bar at the end).
    size_t cursor_unit = 0;
    while (cursor_unit < units.size() && units[cursor_unit].start < cursor) ++cursor_unit;
    const int at_cursor = cursor_unit < units.size() ? std::max(1, units[cursor_unit].cells) : 1;
    const int budget = room + 1 - 1 - at_cursor;
    int used = 0;
    first = cursor_unit;
    while (first > 0 && used + units[first - 1].cells <= budget) {
      used += units[first - 1].cells;
      --first;
    }
    ellipsis = first > 0;
  }
  line.text = std::wstring(shown_prompt);
  int column = shown_prompt_cells;
  if (ellipsis) {
    line.text += kEllipsis;
    ++column;
  }
  line.cursor_column = -1;
  line.input_start = line.text.size();
  line.input_column = column;
  const int limit = shown_prompt_cells + room + 1;  // = columns
  for (size_t i = first; i < units.size(); ++i) {
    const Unit& u = units[i];
    if (u.start == cursor) line.cursor_column = column;
    // Characters after the cursor are cut at the right edge; when the whole
    // input fits, the last cell stays free for the cursor at the end.
    if (column + u.cells > (u.start >= cursor && total > room ? limit : limit - 1)) break;
    line.text.append(input.substr(u.start, u.length));
    column += u.cells;
  }
  if (line.cursor_column < 0) line.cursor_column = column;

  if (!message.empty()) {
    const int free = columns - text_cells(line.text) - 1;
    if (free >= 4) line.text += L" " + fit_message(message, free);
  }
  return line;
}

bool is_invisible_format_char(char32_t c) {
  return (c >= 0x200B && c <= 0x200F) || c == 0x2060 || (c >= 0xFE00 && c <= 0xFE0F) ||
         c == 0xFEFF;
}

int max_echo_rows(int total_rows) {
  return std::max(1, total_rows - 2);
}

std::wstring dropped_files_note(unsigned count) {
  if (count <= 1) return std::wstring();
  return L"Opened only the first of " + std::to_wstring(count) + L" dropped files";
}

}  // namespace geditor::win
