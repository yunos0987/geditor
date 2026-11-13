// The main window: message handling, keyboard and IME input, drag and drop,
// and the Host implementation that the core calls back into.
#pragma once

#include <string>

#include <windows.h>

#include <shellapi.h>

#include "command_line.h"
#include "config_watcher.h"
#include "editor.h"
#include "file_io.h"
#include "host.h"
#include "input.h"
#include "renderer.h"

namespace geditor::win {

class MainWindow final : public Host {
 public:
  MainWindow();
  ~MainWindow() override;
  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  // Creates the renderer and the (hidden) window.
  bool create(HINSTANCE instance, bool measure_startup, bool measure_input);
  // Reads the settings file (--settings=PATH or %USERPROFILE%\.geditor.json),
  // opens the file of the command line, goes to +LINE and shows argument and
  // settings errors in the echo area.
  void apply_command_line(const CommandLine& command_line);
  void show(int show_command);
  // Keyboard messages are translated here, before TranslateMessage, so that a
  // handled key produces no WM_CHAR / WM_SYSCHAR. True when consumed.
  bool pre_translate(const MSG& msg);

  // ---- Host ----
  bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) override;
  bool write_file(const std::wstring& path, std::string_view bytes, std::wstring& error) override;
  FileKind file_kind(const std::wstring& path) override;
  bool list_directory(const std::wstring& dir, std::vector<DirEntry>& entries) override;
  std::wstring current_directory() override;
  ClipboardRead get_clipboard(std::wstring& text) override;
  bool set_clipboard(const std::wstring& text) override;
  OpenChoice ask_open_choice(const std::wstring& path) override;
  SaveChoice ask_save_changes(const std::wstring& buffer_name) override;
  bool ask_yes_no(const std::wstring& question) override;
  bool launch_new_window(const std::wstring& path, std::wstring& error) override;
  void quit() override;
  int visible_lines() override;

 private:
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam);
  void paint();
  void handle_key(const Key& key);
  void handle_char(wchar_t unit);
  void handle_ime_composition(LPARAM lparam);
  void handle_drop(HDROP drop);
  // After any input: title, prefix-key echo timer, repaint.
  void after_input();
  void update_title();
  void keep_point_visible();
  void place_ime();
  void start_input_measure();
  // After the first frame: watch the settings file (failures are shown once).
  void start_settings_watcher();
  // Adds `text` to the echo area message after the current one.
  void append_message(const std::wstring& text);
  // Draws the current frame into a 32-bit BMP file (WM_COPYDATA request).
  bool capture_to_bmp(const std::wstring& path);

  HWND hwnd_ = nullptr;
  Renderer renderer_;
  Editor editor_;
  ViewState view_;
  CharAssembler chars_;
  ConfigWatcher settings_watcher_;
  std::wstring settings_argument_;  // --settings, passed on to new windows
  FileState settings_state_;        // the settings file as read at startup
  std::wstring title_;
  bool measure_startup_ = false;
  bool measure_input_ = false;
  bool first_frame_done_ = false;
  bool input_pending_ = false;
  LARGE_INTEGER input_start_{};
  double open_ms_ = -1;
  int last_rows_ = 0;  // text rows of the last non-empty client area
};

}  // namespace geditor::win
