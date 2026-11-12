// In-memory Host for tests: a fake file system, scripted dialog answers and a
// clipboard, with a record of every dialog and window request.
#pragma once

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "editor.h"
#include "host.h"
#include "test_harness.h"

namespace test {

class FakeHost : public geditor::Host {
 public:
  struct Entry {
    bool is_directory = false;
    std::string bytes;
  };

  // Keys are lower-cased paths; `names` keeps the original spelling.
  std::map<std::wstring, Entry> files;
  std::map<std::wstring, std::wstring> names;
  std::wstring cwd = L"C:\\work";
  std::wstring clipboard;
  bool clipboard_has_text = false;
  int lines = 10;

  std::deque<geditor::OpenChoice> open_answers;
  std::deque<geditor::SaveChoice> save_answers;
  std::deque<bool> yes_no_answers;
  bool fail_writes = false;
  bool fail_launch = false;
  bool fail_clipboard = false;
  bool fail_clipboard_read = false;  // get_clipboard reports kFailed
  // Paths (any case) for which file_kind reports FileKind::kError.
  std::set<std::wstring> error_paths;

  int open_dialogs = 0;
  int save_dialogs = 0;
  int yes_no_dialogs = 0;
  std::vector<std::wstring> launched;
  std::vector<std::wstring> written;
  int quit_calls = 0;

  FakeHost();

  void add_dir(const std::wstring& path);
  void add_file(const std::wstring& path, const std::string& bytes);
  bool has_file(const std::wstring& path) const;
  std::string file(const std::wstring& path) const;

  bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) override;
  bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error) override;
  geditor::FileKind file_kind(const std::wstring& path) override;
  bool list_directory(const std::wstring& dir, std::vector<geditor::DirEntry>& entries) override;
  std::wstring current_directory() override { return cwd; }
  geditor::ClipboardRead get_clipboard(std::wstring& text) override;
  bool set_clipboard(const std::wstring& text) override;
  void add_error_path(const std::wstring& path);
  geditor::OpenChoice ask_open_choice(const std::wstring& path) override;
  geditor::SaveChoice ask_save_changes(const std::wstring& buffer_name) override;
  bool ask_yes_no(const std::wstring& question) override;
  bool launch_new_window(const std::wstring& path, std::wstring& error) override;
  void quit() override { ++quit_calls; }
  int visible_lines() override { return lines; }
};

// A host plus an editor, with helpers to feed keys.
struct Session {
  FakeHost host;
  geditor::Editor ed{host};

  // Feeds a key sequence written in key notation, such as "C-x C-f".
  void keys(const wchar_t* notation);
  // Feeds each character of `text` as an unmodified key.
  void type(std::wstring_view text);
  // Opens a file with `content` (UTF-8 of the text) at C:\work\t.txt (or
  // `path`) so that the buffer starts with no undo history.
  void load(const std::wstring& content, const std::wstring& path = L"C:\\work\\t.txt");
  void set_point(size_t offset) { ed.set_point(offset); }

  std::wstring text() const { return ed.buffer().text.text(); }
  size_t point() const { return ed.buffer().point; }
  std::wstring message() const { return ed.message(); }
  bool modified() const { return ed.buffer().modified(); }
};

std::string utf8(const std::wstring& s);

}  // namespace test
