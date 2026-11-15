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
    unsigned long long write_time = 0;  // a tick of `clock` at the last write
    bool read_only = false;
  };

  // The safe-save steps of Host::save_file that a test can make fail once
  // (decision F4). The fake changes nothing when a step fails.
  enum class SaveStep { kNone, kTempWrite, kFlush, kReplace, kMove, kBackup };
  SaveStep fail_step = SaveStep::kNone;
  // Paths (any case) whose file_info fails.
  std::set<std::wstring> info_error_paths;
  // Every save_file call: the path and whether a backup was asked for.
  std::vector<std::pair<std::wstring, bool>> saves;
  unsigned long long clock = 1;

  // Keys are lower-cased paths; `names` keeps the original spelling.
  std::map<std::wstring, Entry> files;
  std::map<std::wstring, std::wstring> names;
  std::wstring cwd = L"C:\\work";
  std::wstring clipboard;
  bool clipboard_has_text = false;
  int lines = 10;
  // Cells across the text area: with the default 3-cell gutter the window
  // width is 80, so the default wrap (max, 80) wraps at 80 cells.
  int columns = 84;
  int display_dpi = 96;

  std::deque<geditor::OpenChoice> open_answers;
  std::deque<geditor::SaveChoice> save_answers;
  std::deque<bool> yes_no_answers;
  bool fail_writes = false;
  bool fail_launch = false;
  bool fail_clipboard = false;
  bool fail_clipboard_read = false;  // get_clipboard reports kFailed
  // Paths (any case) for which file_kind reports FileKind::kError.
  std::set<std::wstring> error_paths;
  // Paths (any case) that exist but cannot be read ("Access is denied").
  std::set<std::wstring> unreadable_paths;
  // When set, visible_lines() shrinks by the extra echo area rows of this
  // editor, as the window does (decision D10).
  const geditor::Editor* echo_editor = nullptr;

  int open_dialogs = 0;
  int save_dialogs = 0;
  int yes_no_dialogs = 0;
  std::vector<std::wstring> yes_no_questions;  // the text of each yes/no question
  std::vector<std::wstring> launched;
  std::vector<std::wstring> launched_encodings;  // the encoding of each launched window ("": none)
  std::vector<std::wstring> written;
  int quit_calls = 0;

  FakeHost();

  void add_dir(const std::wstring& path);
  void add_file(const std::wstring& path, const std::string& bytes);
  bool has_file(const std::wstring& path) const;
  std::string file(const std::wstring& path) const;
  // The write time of a file (0 when there is none).
  unsigned long long write_time(const std::wstring& path) const;

  // Changes a file as another program would: new bytes and a new write time.
  void touch_file(const std::wstring& path, const std::string& bytes);
  void set_read_only(const std::wstring& path, bool read_only);
  void remove_file(const std::wstring& path);

  bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) override;
  // Writes `bytes` as `path` (fail_writes: "disk full"); save_file uses it.
  bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error);
  bool save_file(const std::wstring& path, std::string_view bytes, bool backup, std::wstring& error) override;
  bool file_info(const std::wstring& path, geditor::FileInfo& info, std::wstring& error) override;
  geditor::FileKind file_kind(const std::wstring& path) override;
  bool list_directory(const std::wstring& dir, std::vector<geditor::DirEntry>& entries) override;
  std::wstring current_directory() override { return cwd; }
  geditor::ClipboardRead get_clipboard(std::wstring& text) override;
  bool set_clipboard(const std::wstring& text) override;
  void add_error_path(const std::wstring& path);
  geditor::OpenChoice ask_open_choice(const std::wstring& path) override;
  geditor::SaveChoice ask_save_changes(const std::wstring& buffer_name) override;
  bool ask_yes_no(const std::wstring& question) override;
  bool launch_new_window(const std::wstring& path, const std::wstring& encoding, std::wstring& error) override;
  void quit() override { ++quit_calls; }
  void open_menu_bar() override { ++menu_bar_opens; }
  int menu_bar_opens = 0;
  int visible_lines() override;
  int visible_columns() override { return columns; }
  int dpi() override { return display_dpi; }
};

// The settings file of Session::settings.
inline constexpr wchar_t kSettingsPath[] = L"C:\\work\\.geditor.json";

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
  // Writes `text` (UTF-8) to kSettingsPath, makes it the settings file and
  // reloads it, as after saving the file.
  void settings(const std::string& text);

  std::wstring text() const { return ed.buffer().text.text(); }
  size_t point() const { return ed.buffer().point; }
  std::wstring message() const { return ed.message(); }
  bool modified() const { return ed.modified(); }
};

std::string utf8(const std::wstring& s);

}  // namespace test
