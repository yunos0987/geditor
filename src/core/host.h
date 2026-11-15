// Platform services the core needs. The Win32 layer (src/win) implements this
// interface; tests use a fake. Paths are absolute Windows paths with
// backslashes. Every call is synchronous; dialogs are modal.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace geditor {

// kError: the path may exist but cannot be inspected (access denied, network
// failure). The core never treats it as a new file.
enum class FileKind { kNone, kFile, kDirectory, kError };
enum class OpenChoice { kReplace, kNewWindow, kCancel };
enum class SaveChoice { kSave, kDontSave, kCancel };
enum class ClipboardRead { kOk, kNoText, kFailed };

struct DirEntry {
  std::wstring name;  // file name only, without "." and ".."
  bool is_directory = false;
};

// What tells two versions of a file apart without reading it (decision F2),
// and its read-only attribute (decision F6). Through a symbolic link: the
// file it points to.
struct FileInfo {
  bool exists = false;
  unsigned long long size = 0;
  unsigned long long write_time = 0;  // last write, in 100 ns units
  bool read_only = false;

  bool operator==(const FileInfo&) const = default;
};

class Host {
 public:
  virtual ~Host() = default;

  // Files. `error` receives a message for the echo area on failure.
  virtual bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) = 0;
  // Saves `bytes` as `path` the safe way (docs/PLAN.md 4.8): a temporary
  // file in the same folder, flushed, then ReplaceFileW (an existing file)
  // or MoveFileExW (a new one); a symbolic link is resolved and its target
  // replaced; a file with several hard links is written in place. With
  // `backup`, the old content of an existing file is kept as "<file>~"; a
  // failure to make it fails the save. On failure, `error` receives the
  // failed step and the reason ("flushing the temporary file: The disk is
  // full"), and the file and "<file>~" are as before (except for a failure
  // in the middle of an in-place write, which the reason says). On success,
  // `error` may hold a note for the echo area (empty: none).
  virtual bool save_file(const std::wstring& path, std::string_view bytes, bool backup, std::wstring& error) = 0;
  // A missing file gives exists = false and true; false (with the reason in
  // `error`) when the file cannot be inspected.
  virtual bool file_info(const std::wstring& path, FileInfo& info, std::wstring& error) = 0;
  virtual FileKind file_kind(const std::wstring& path) = 0;
  // Lists the entries of `dir` (with or without a trailing backslash).
  virtual bool list_directory(const std::wstring& dir, std::vector<DirEntry>& entries) = 0;
  virtual std::wstring current_directory() = 0;

  // Clipboard text uses "\n" line ends; converting to and from CRLF is the
  // host's job. get_clipboard tells "no text" (kNoText) from "the clipboard
  // cannot be opened or read" (kFailed); set_clipboard returns false when the
  // clipboard cannot be written.
  virtual ClipboardRead get_clipboard(std::wstring& text) = 0;
  virtual bool set_clipboard(const std::wstring& text) = 0;

  // "Open in this window (replace)" / "Open in a new window" / "Cancel".
  virtual OpenChoice ask_open_choice(const std::wstring& path) = 0;
  // "Save" / "Don't save" / "Cancel" for a modified buffer.
  virtual SaveChoice ask_save_changes(const std::wstring& buffer_name) = 0;
  // Yes/no question, such as overwriting an existing file.
  virtual bool ask_yes_no(const std::wstring& question) = 0;

  // Starts another geditor process for `path`; a non-empty `encoding` (one
  // of encoding_names()) is passed as --encoding=NAME (find-file-with-encoding).
  virtual bool launch_new_window(const std::wstring& path, const std::wstring& encoding, std::wstring& error) = 0;
  // Closes the window and ends the process (after the current call returns).
  virtual void quit() = 0;
  // menu-bar-open (F10): enters the menu bar after the current command
  // returns, as the Win32 menu loop would (docs/PLAN.md 4.2).
  virtual void open_menu_bar() = 0;
  // Number of text lines (screen rows) that fit in the window.
  virtual int visible_lines() = 0;
  // Number of whole cells across the text area, line numbers included.
  virtual int visible_columns() = 0;
  // Dots per inch of the window (96 = 100 %), for text-scale limits.
  virtual int dpi() = 0;
};

}  // namespace geditor
