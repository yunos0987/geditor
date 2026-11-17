// File system access for the Host: read, write, inspect, list, and the safe
// save of docs/PLAN.md 4.8 (M4a).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "host.h"

namespace geditor::win {

// FileKind for a GetFileAttributesExW failure: only "not found" errors mean
// kNone (a new file); every other error (access denied, sharing violation,
// network failure, bad name) is kError.
FileKind file_kind_from_error(unsigned long error);

// The path to pass to the file APIs: an absolute path that is too long for
// the classic MAX_PATH limit gets the "\\?\" (or "\\?\UNC\") prefix, so that
// files deeper than 260 characters open without longPathAware. Shorter,
// relative and already prefixed paths are returned unchanged.
std::wstring extended_length_path(const std::wstring& path);

// Short English text for a Win32 error code ("Access is denied").
std::wstring error_text(unsigned long error);
// The reason of a failed ReplaceFileW (the "replacing the file" step of a
// save). Errors 1175 - 1177 (ERROR_UNABLE_TO_REMOVE_REPLACED,
// ERROR_UNABLE_TO_MOVE_REPLACEMENT, ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
// without REPLACEFILE_IGNORE_MERGE_ERRORS also the errors of attributes or
// permissions that cannot be carried over) are explained in plain words with
// the advice to save under another name (deferred fix F2); any other error
// is error_text.
std::wstring replace_error_text(unsigned long error);

// What tells two versions of a file apart without reading it: existence,
// size and last-write time, and the read-only attribute (all zero when the
// file is missing or cannot be inspected).
struct FileState {
  bool exists = false;
  unsigned long long size = 0;
  unsigned long long write_time = 0;
  bool read_only = false;

  bool operator==(const FileState&) const = default;
};
FileState file_state(const std::wstring& path);
// Host::file_info: the state of the file that `path` names, through a
// symbolic link (the file it points to). A missing file: exists = false and
// true. Any other failure: false with the reason in `error`.
bool read_file_state(const std::wstring& path, FileState& state, std::wstring& error);

bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error);
// Writes in place (the headless state file and tests; files of the user go
// through save_file). `error` receives the reason only ("Access is denied").
bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error);

// The step of save_file that a check makes fail once (decision F4,
// 'GFLT' "save-..."): the step reports a failure as if Windows had
// returned an error there, and everything after it runs as for a real one.
enum class SaveFault {
  kNone,
  kInspect,     // reading the attributes of the file
  kLink,        // resolving the link (the file is treated as a link)
  kTempCreate,  // creating the temporary file
  kTempWrite,   // writing the temporary file
  kFlush,       // FlushFileBuffers of the temporary file
  kReplace,     // ReplaceFileW
  kMove,        // MoveFileExW of a new file
  kBackup,      // keeping the old content as "<file>~"
  kInPlace,     // writing a hard-linked file in place
};
// The fault of a 'GFLT' name ("save-inspect", "save-link",
// "save-temp-create", "save-temp-write", "save-flush", "save-replace",
// "save-move", "save-backup", "save-in-place"); `*known` is false (and the
// result kNone) for any other name.
SaveFault save_fault_from_name(std::wstring_view name, bool* known);

// Host::save_file (docs/PLAN.md 4.8, decisions F3 - F5):
//  - an existing file: a temporary file ".<name>.<pid>.<n>.geditor-tmp" in
//    the same folder is written and flushed, then ReplaceFileW swaps it in
//    (merge errors are not ignored: attributes or an ACL that cannot be
//    carried over fail the save, M4a fix F4); the old file goes to
//    a second temporary name ("...geditor-old") and, with `backup`, is then
//    renamed to "<file>~" (so that "<file>~" changes only when the save
//    succeeded; when that rename fails, the old file is put back and the save
//    fails), else it is deleted;
//  - temporary names: <name> is cut so that the whole name fits the longest
//    name of the volume (GetVolumeInformationW; 255 when it cannot be read),
//    M4a fix F3. When "<file>~" itself would be longer than that, the file
//    is saved without a backup and the note says so;
//  - a new file: the temporary file is moved to `path` (MoveFileExW);
//  - a symbolic link: the same for the file it points to (its final path);
//    "<file>~" goes next to that file;
//  - a file with two or more hard links: with `backup` a copy is made as
//    "<file>~" first, then the file is written in place, so that every name
//    sees the new content. A failure in the middle of that write leaves the
//    file partly written; the error says so and names the backup;
//  - a file with the read-only attribute: fails ("Access is denied").
// On failure `error` is "<step>: <reason>" (the backup step names the
// backup: "making the backup <file>~: <reason>", M4a fix F5), every temporary
// file is removed, and the file and "<file>~" are unchanged (except the
// in-place case). On success `error` may hold a note (no backup because of
// the name length; a temporary file that could not be removed).
bool save_file(const std::wstring& path, std::string_view bytes, bool backup, std::wstring& error,
               SaveFault fault = SaveFault::kNone);
FileKind file_kind(const std::wstring& path);
bool list_directory(const std::wstring& dir, std::vector<DirEntry>& entries);
std::wstring current_directory();

}  // namespace geditor::win
