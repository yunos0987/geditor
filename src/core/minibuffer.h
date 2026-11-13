// Minibuffer support: prompt state, file name completion and input history.
// The text being edited lives in an ordinary Buffer owned by the Editor, so
// the global editing commands work inside the minibuffer.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "host.h"

namespace geditor {

// kExtendedCommand: M-x reads a command name. kCommandArgument: an argument
// of a command run without its arguments.
enum class MinibufferPurpose { kNone, kFindFile, kWriteFile, kExtendedCommand, kCommandArgument };

struct FileCompletion {
  std::wstring text;     // new minibuffer contents (unchanged when nothing to add)
  std::wstring message;  // "[No match]", "[Sole completion]" or "{a | b\ | c}"
};

// The candidate list stops after this many names or once it reaches this many
// characters, then ends with "... (N candidates)".
constexpr size_t kMaxListedCandidates = 100;
constexpr size_t kMaxCandidateListChars = 1000;

// Completes the last path component of `input` against the directory listing
// (case-insensitive). A directory completes with a trailing backslash. When
// the input cannot be extended, the candidates are listed in `message`.
FileCompletion complete_file_name(std::wstring_view input, Host& host);

// Completes `input` against `names` (command names for M-x, the choices of a
// command argument) with the same rules as a file name in one directory.
FileCompletion complete_name(std::wstring_view input, std::vector<std::wstring> names);

// Input history, newest last. Navigation positions: -1 is the input being
// edited, 0 the newest entry, size() - 1 the oldest.
class History {
 public:
  void add(std::wstring_view entry);
  const std::vector<std::wstring>& entries() const { return entries_; }

  // Start a navigation session for new input.
  void reset_position() { position_ = -1; }
  int position() const { return position_; }

  // Move to an older / newer element. `current` is the minibuffer text; it is
  // remembered when leaving position -1 and returned when coming back.
  // Return false at the end of the history.
  bool previous(const std::wstring& current, std::wstring* out);
  bool next(std::wstring* out);

 private:
  std::vector<std::wstring> entries_;
  std::wstring saved_input_;
  int position_ = -1;
};

}  // namespace geditor
