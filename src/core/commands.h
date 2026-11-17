// Command registry: command names (as in docs/PLAN.md 4.4) and their
// implementations.
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "piece_tree.h"

namespace geditor {

class Editor;

// A command reads its arguments (Editor::command_args) and the prefix
// argument (Editor::prefix_count) from the editor; failures go through
// Editor::fail.
using CommandFn = void (*)(Editor&);

// Parameter of a command that takes arguments. A key binding either gives
// every argument or none; with none, the command asks for each argument in
// the minibuffer.
enum class ParamType {
  kText,     // any string
  kInteger,  // a whole number
  kChoice,   // one of `choices`
};

struct Param {
  const wchar_t* prompt;  // minibuffer prompt, such as "Text: "
  ParamType type;
  std::span<const wchar_t* const> choices;  // kChoice
};

struct CommandInfo {
  const char* name;
  CommandFn fn;  // nullptr: registered but not implemented yet (a later milestone)
  std::span<const Param> params = {};
  // universal-argument, digit-argument and negative-argument: the command
  // after them still sees the command before them as the last command.
  bool prefix_argument = false;
};

// The most characters one command (or one run of a command sequence with a
// prefix argument) may insert: larger counts are refused with "Repeat count
// too large" instead of freezing the editor.
inline constexpr size_t kMaxRepeatedInsert = 10'000'000;

std::span<const CommandInfo> all_commands();
const CommandInfo* find_command(std::string_view name);

// The kill commands (kill-line, kill-word, backward-kill-word, kill-region,
// cut, backward-kill-line): a kill right after one of them adds to the
// clipboard text (M6).
bool is_kill_command(std::string_view name);
// The movement commands of the Windows editing keys of PLAN 4.4 (and the
// Emacs keys of the same commands): a shift-translated key runs them with
// shift selection (M6).
bool is_shift_select_command(std::string_view name);
// Commands that set an ordinary (not shift-selected) active mark.
bool is_mark_command(std::string_view name);

// The 13 encoding names of docs/PLAN.md 2 "encodings that can be chosen".
std::span<const wchar_t* const> encoding_names();

// The word at `offset` as forward-word / backward-word see words: [from, to)
// of the word whose characters include the one at `offset`. When that
// character is no word character, [offset, the next character); at the end
// of the text, the empty range there (double-click, decision B5).
void word_bounds_at(const PieceTree& text, size_t offset, size_t* from, size_t* to);

}  // namespace geditor
