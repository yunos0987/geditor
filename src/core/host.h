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

class Host {
 public:
  virtual ~Host() = default;

  // Files. `error` receives a message for the echo area on failure.
  virtual bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) = 0;
  virtual bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error) = 0;
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

  // Starts another geditor process for `path`.
  virtual bool launch_new_window(const std::wstring& path, std::wstring& error) = 0;
  // Closes the window and ends the process (after the current call returns).
  virtual void quit() = 0;
  // Number of text lines that fit in the window.
  virtual int visible_lines() = 0;
};

}  // namespace geditor
