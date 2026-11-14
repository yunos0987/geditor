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

struct Question;

class MainWindow final : public Host {
 public:
  MainWindow();
  ~MainWindow() override;
  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  // Creates the renderer and the (hidden) window. `headless` (--headless):
  // the window is placed outside every monitor as a tool window that is
  // never activated, the clipboard is a private store and questions are
  // answered through WM_COPYDATA (headless.h).
  bool create(HINSTANCE instance, bool measure_startup, bool measure_input, bool headless);
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
  int visible_columns() override;
  int dpi() override;

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
  // Rebuilds the font when the settings, text-scale or DPI ask for another
  // one (always when `force`); reports missing families once per value of
  // font.family / font.fallback (decision E1, missing_fonts_) and a failed
  // creation (the previous font stays).
  void apply_font(bool force);
  void handle_dpi_changed(UINT dpi, const RECT& suggested);
  // WM_VSCROLL: `position` is the 16-bit thumb position of the message.
  void handle_vscroll(int code, unsigned position);
  void update_scroll_bar();
  void place_ime();
  void start_input_measure();
  // After the first frame: watch the settings file (failures are shown once).
  void start_settings_watcher();
  // Adds `text` to the echo area message after the current one.
  void append_message(const std::wstring& text);
  // Draws the current frame into a 32-bit BMP file (WM_COPYDATA request).
  bool capture_to_bmp(const std::wstring& path);
  // The WM_COPYDATA requests of the tools (headless.h); 1 when done, 0 when
  // rejected (reject_request says why).
  LRESULT handle_copy_data(const COPYDATASTRUCT* data);
  // Counts a request that was not understood and reports it to the
  // debugger; the state ('GSTA') shows the count and the last reason.
  void reject_request(const std::wstring& reason);
  // Writes the state lines (headless.h) to `path` as UTF-16LE.
  bool write_state(const std::wstring& path);
  // Headless: asks `question` without a window and waits, in a loop like the
  // modal loop of a task dialog, until a tool presses one of its buttons
  // ('GANS'). Returns the id of that button (IDCANCEL when the loop ends
  // because the program quits).
  int ask_headless(const Question& question);

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
  int last_rows_ = 0;     // text rows of the last non-empty client area
  int last_columns_ = 0;  // cells across the last non-empty client area
  // A frame that could not be drawn (no render target): retried by a timer
  // and reported in the echo area once drawing works again.
  HRESULT draw_error_ = S_OK;
  // The scroll bar as last set.
  SCROLLINFO scroll_shown_{};
  bool scroll_set_ = false;
  // Which missing font names were already reported (decision E1).
  MissingFontReport missing_fonts_;
  // ---- --headless ----
  bool headless_ = false;
  // The private clipboard: the text as the real clipboard would hold it
  // (CRLF line ends); the real clipboard is never opened.
  std::wstring clipboard_store_;
  bool clipboard_has_text_ = false;
  // One-shot failures of the private clipboard ('GFLT').
  bool fail_clipboard_read_ = false;
  bool fail_clipboard_write_ = false;
  // The question waiting for an answer, and the pressed button (0: none).
  const Question* question_ = nullptr;
  int question_answer_ = 0;
  unsigned rejected_requests_ = 0;
  std::wstring last_rejected_;
};

}  // namespace geditor::win
