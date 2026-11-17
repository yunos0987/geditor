// The main window: message handling, keyboard and IME input, drag and drop,
// and the Host implementation that the core calls back into.
#pragma once

#include <deque>
#include <string>

#include <windows.h>

#include <shellapi.h>

#include "command_line.h"
#include "config_watcher.h"
#include "editor.h"
#include "file_io.h"
#include "host.h"
#include "input.h"
#include "mouse.h"
#include "question.h"
#include "renderer.h"

namespace geditor::win {

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
  // After each message of the message loop: runs the keyboard and mouse
  // messages that quit_requested took from the queue during a keyboard macro
  // run and that no C-g dropped (deferred fix E4, retry R1), in their order.
  void run_held_keys();

  // ---- Host ----
  bool read_file(const std::wstring& path, std::string& bytes, std::wstring& error) override;
  bool save_file(const std::wstring& path, std::string_view bytes, bool backup, std::wstring& error) override;
  bool file_info(const std::wstring& path, FileInfo& info, std::wstring& error) override;
  FileKind file_kind(const std::wstring& path) override;
  bool list_directory(const std::wstring& dir, std::vector<DirEntry>& entries) override;
  std::wstring current_directory() override;
  ClipboardRead get_clipboard(std::wstring& text) override;
  bool set_clipboard(const std::wstring& text) override;
  OpenChoice ask_open_choice(const std::wstring& path) override;
  SaveChoice ask_save_changes(const std::wstring& buffer_name) override;
  bool ask_yes_no(const std::wstring& question) override;
  bool launch_new_window(const std::wstring& path, const std::wstring& encoding, std::wstring& error) override;
  void quit() override;
  void open_menu_bar() override;
  int visible_lines() override;
  int visible_columns() override;
  int dpi() override;
  bool quit_requested() override;

 private:
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam);
  void paint();
  // The modifiers of a key message: its lParam bits (headless) or
  // GetKeyState.
  ModifierState key_message_modifiers(const MSG& msg) const;
  // A Down / Left / Right that a menu loop did not take (posted with a mark
  // in lParam): dropped.
  static bool is_dropped_menu_key(const MSG& msg);
  // A key that goes to TranslateMessage and the system instead of the
  // keymap (an unbound M-<f4>, Shift+F10 for the context menu).
  bool key_goes_to_system(const Key& key) const;
  // pre_translate of a WM_KEYDOWN / WM_SYSKEYDOWN with its modifiers.
  bool handle_key_message(const MSG& msg, const ModifierState& mods);
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

  // ---- Menus (docs/PLAN.md 4.9, decisions B1-B10) ----
  // Shows or removes the menu bar for the menu_bar setting (keeping the top
  // line), or refreshes its texts; a failed creation is reported.
  void apply_menu_bar();
  // menu-bar-open after its command: the Win32 menu loop with the File menu
  // dropped down (decision G7; when the key that ran the command is still
  // held, on its release), or headless the pending menu (decision B7).
  void enter_menu_bar();
  // WM_NEXTMENU: Left / Right at the ends of the menu bar wrap around to its
  // other end with one key press, through the system menu (M3d decision D6).
  void handle_next_menu(WPARAM key, MDINEXTMENU* next);
  // 'GRDW' (M3d [C4]): draws `frames` frames at once and keeps their times
  // (and, in a debug build, the bytes the C runtime allocated meanwhile).
  void run_redraw_benchmark(int frames);
  // WM_INITMENUPOPUP / WM_UNINITMENUPOPUP: which drop-down is open (state
  // for the tools).
  void note_menu_popup(HMENU menu, bool system_menu);
  // WM_CONTEXTMENU: `lparam` -1 for the keyboard (at the cursor), else the
  // screen position of the mouse.
  void open_context_menu(LPARAM lparam);
  // WM_COMMAND of a menu item: runs its command like the key bound to it.
  void run_menu_command(UINT id);
  // Headless: the menu as a pending question; returns the chosen WM_COMMAND
  // id, 0 for Cancel.
  UINT ask_headless_menu(QuestionKind kind, HMENU menu, POINT position);

  // ---- Mouse (decision B5) ----
  // The hit of a client point in physical pixels.
  Renderer::Hit hit_at(POINT client) const;
  void handle_left_down(POINT client, bool shift);
  void handle_left_double_click(POINT client);
  void handle_drag(POINT client);
  void drag_scroll_step();
  void end_drag();
  void handle_wheel(int delta);
  bool set_cursor_shape();
  // The caret's bottom-left corner in client pixels (keyboard context menu).
  POINT caret_point() const;

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
  // An input changed the view: the scroll bar is set after the next frame
  // (paint), so that redrawing the bar does not delay the text (B6).
  bool scroll_bar_after_frame_ = false;
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
  // ---- menus ----
  // One-shot failures ('GFLT' "menu": the next HMENU creation; "popup": the
  // next context menu as if TrackPopupMenu had failed).
  bool fail_next_menu_ = false;
  bool fail_next_popup_ = false;
  // One-shot save failures ('GFLT' "save-..." and "file-info", M4a).
  SaveFault fail_next_save_ = SaveFault::kNone;
  bool fail_next_file_info_ = false;
  unsigned menu_loops_ = 0;      // WM_ENTERMENULOOP seen
  unsigned keymenu_count_ = 0;   // SC_KEYMENU / SC_MOUSEMENU seen
  std::wstring last_keymenu_;    // the last one: "keymenu <lParam>" / "mousemenu"
  // Decision G7: the virtual key of the last key handled through the keymap,
  // and the key whose release enters the menu bar (menu-bar-open ran while
  // it was held; 0: none).
  UINT last_key_vk_ = 0;
  UINT menu_key_ = 0;
  // The menu loop as the tools see it: active, the drop-down open now
  // ("File", "Edit", "View", "System", "Submenu"; empty: none) and every
  // drop-down opened in this loop ("File,Edit,...").
  bool menu_loop_active_ = false;
  std::wstring menu_popup_;
  std::wstring menu_popups_;
  // ---- mouse ----
  bool dragging_ = false;
  POINT drag_point_{};   // the last mouse position of the drag (client pixels)
  int drag_scroll_ = 0;  // -1 / +1 while the drag is above / below the text
  WheelAccumulator wheel_;
  // ---- frames and settings reloads (GSTA, for the tools) ----
  unsigned settings_reloads_ = 0;  // reloads applied after the startup read
  unsigned frames_ = 0;            // frames drawn successfully
  unsigned reloads_drawn_ = 0;     // settings_reloads_ at the last drawn frame
  // ---- 'GRDW' (M3d [C4]) ----
  std::wstring redraw_result_;     // state lines of the last benchmark
  // ---- keyboard macro runs (quit_requested, E4) ----
  struct HeldKey {
    MSG msg{};
    ModifierState mods;
    bool translated = false;  // TranslateMessage done when it was taken (visible keys)
  };
  std::deque<HeldKey> held_keys_;
  // Questions and menus opened so far (with menu_loops_: retry Q2).
  unsigned modal_opens_ = 0;
  // Drops the held input and ends a drag in progress (retry Q3).
  void discard_held_input();
  // Input besides keys and client mouse messages that quit_requested holds:
  // non-client mouse moves, hover / leave, gestures, touch and pointer
  // messages (retry P1).
  static bool is_held_movement(UINT message);
  // Non-client button presses and releases (mouse, and the non-client
  // contacts of pen and touch) that quit_requested takes and drops.
  static bool is_dropped_button(UINT message);
};

}  // namespace geditor::win
