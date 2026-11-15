// The editor: buffers, key dispatch, minibuffer and file flows. The host feeds
// keys and IME strings in and reads the state back for drawing.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "buffer.h"
#include "commands.h"
#include "config.h"
#include "display.h"
#include "host.h"
#include "key.h"
#include "keymap.h"
#include "minibuffer.h"

namespace geditor {

struct MenuItem;

// Shown when a command needs the minibuffer while it is already active.
inline constexpr wchar_t kRecursiveMinibufferMessage[] =
    L"Command attempted to use minibuffer while in minibuffer";

// The prefix argument (docs/PLAN.md 4.4, decision D6): C-u = 4, C-u C-u =
// 16, C-u with digits, C-u - = -1, M-0..M-9, M--.
struct PrefixArg {
  bool active = false;
  bool digits = false;    // digits were typed
  bool negative = false;  // "-" was typed
  int value = 0;          // the typed digits
  int multiplier = 1;     // 4 for each C-u (without digits)

  // The numeric value; 1 when not active.
  int count() const;
};

class Editor {
 public:
  explicit Editor(Host& host);
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;

  // ---- Input from the host ----
  void handle_key(const Key& key);
  // Text committed by the IME: inserted as one undo group ("\r\n" and "\r"
  // become "\n").
  void insert_text(std::wstring_view text);
  // Opens `path` (absolute or relative to the current directory) following
  // the "open another file" rules: used by find-file, drag and drop and the
  // command line. `encoding` (one of encoding_names(); empty: detect) reads
  // the file in that encoding (find-file-with-encoding, --encoding) and is
  // passed to a new window; an unknown name is refused with a message.
  void request_open(std::wstring_view path, std::wstring_view encoding = {});
  // Runs a command by name as if its key had been pressed (for commands
  // without a default key, and the window's close button). Unknown names
  // show a message.
  void execute_command(std::string_view name);
  // Runs the command of a menu item (docs/PLAN.md 4.9) through the same
  // dispatch as a key bound to it: one undo group, the prefix argument typed
  // before it, "not implemented yet". Pending prefix keys are dropped. A
  // disabled item (decision B2) runs nothing and returns false.
  bool execute_menu_item(const MenuItem& item);

  // ---- Mouse (decision B5) ----
  // The offset that a click at screen row `row` of the window (0: the top
  // row; rows below the last one: the end of the buffer) and cell column
  // `column` of the text area (the horizontal scroll included; negative: the
  // line-number gutter, the start of the row) points at: the character whose
  // cells contain the column, the end of the line right of a line's last row,
  // and the last character right of any other row (WrapLayout::offset_at).
  size_t offset_at_screen(long long row, int column) const;
  // The offset of a click (press, double-click, Shift+click) at `row`:
  // like offset_at_screen with `row` clamped to the rows of the window
  // [0, visible_lines() - 1]. The blank strip below the last whole row (a
  // client height that is no multiple of the line height) counts as the
  // last row, so that a click never scrolls the view.
  size_t offset_at_click(long long row, int column);
  // Left press at `offset`: point moves there and the mark is deactivated;
  // with `shift` the region is extended instead, from the active mark or else
  // from the old point. Like a key, it ends a typing undo group and pending
  // prefix keys. Ignored (false) while the minibuffer is active.
  bool mouse_press(size_t offset, bool shift);
  // Dragging after mouse_press: the mark at the press position (or the
  // extended region's mark), point at `offset`, the region active once point
  // left the press position. False when no press is held.
  bool mouse_drag(size_t offset);
  void mouse_release() { mouse_pressed_ = false; }
  bool mouse_pressed() const { return mouse_pressed_; }
  // Double-click: selects the word at `offset` (the forward-word /
  // backward-word definition), or the one character there when it is no word
  // character. Ignored (false) while the minibuffer is active.
  bool mouse_double_click(size_t offset);

  // ---- Settings (docs/PLAN.md 2 "settings errors", 4.3, 4.5) ----
  // The settings file; empty when there is none.
  void set_settings_path(std::wstring path) { settings_path_ = std::move(path); }
  const std::wstring& settings_path() const { return settings_path_; }
  // Reads the settings file through the host and applies it (startup and
  // every change). No file: the built-in defaults, with "Settings file
  // removed; using defaults" when it existed at the last read. A file that
  // cannot be read keeps the previous settings and shows the reason. Pending
  // prefix keys and a pending prefix argument are cancelled; the minibuffer
  // stays.
  void reload_settings();
  // Applies settings text (UTF-8 JSONC). A syntax error keeps the previous
  // settings; semantic errors drop only their items. Every error is shown in
  // the echo area with its line and column. Returns false on a syntax error.
  bool apply_settings_text(std::string_view utf8);
  const Config& config() const { return config_; }
  // Errors of the last settings text, sorted by position.
  const std::vector<Diagnostic>& settings_errors() const { return settings_errors_; }
  const Keymap& keymap(KeymapKind kind) const { return maps_[static_cast<int>(kind)]; }

  // ---- State for drawing ----
  const Buffer& buffer() const { return main_; }
  // The main buffer differs from its file: edited (buffer().modified()), or
  // its encoding for saving was changed (set-buffer-file-coding-system).
  bool modified() const { return main_.modified() || coding_changed_; }
  // The encoding part of the mode line: "UTF-8", "Shift_JIS!" ("!": bytes
  // that could not be read, PLAN 2 "bytes that cannot be read").
  std::wstring encoding_text() const;
  bool minibuffer_active() const { return purpose_ != MinibufferPurpose::kNone; }
  const std::wstring& minibuffer_prompt() const { return prompt_; }
  const Buffer& minibuffer_buffer() const { return mini_; }
  std::wstring minibuffer_text() const { return mini_.text.text(); }
  // Message in the echo area ("" when none).
  const std::wstring& message() const { return message_; }
  // Echo area line: the minibuffer prompt and input (with the message in
  // braces after it), or the message.
  std::wstring echo_area_text() const;
  // True when the message has several lines (settings errors).
  bool message_is_multi_line() const { return message_.find(L'\n') != std::wstring::npos; }
  // Lines of the message, at most `max_lines`: settings errors use several
  // lines ("\n" in the message), everything else one. When fewer lines fit
  // than there are, the last shown line is "... and N more", N counting every
  // hidden error.
  std::vector<std::wstring> message_lines(int max_lines = 10) const;
  // Rows of the echo area when at most `max_rows` fit (the window keeps the
  // mode line and one text line): 1, or the lines of a multi-line message
  // plus the minibuffer line when it is active. The next key returns it to 1.
  int echo_line_count(int max_rows = 10) const;
  // Pending prefix keys for the host to echo, such as "C-x-" ("" when none).
  std::wstring pending_keys_text() const;
  // True when `key`, typed now, would run a command or continue a prefix
  // (after shift translation). The host passes unbound system keys such as
  // M-<f4> on to Windows.
  bool key_is_bound(const Key& key) const;
  // "-** name  UTF-8 LF  L12 C5  wrap:max".
  std::wstring mode_line() const;
  // The line-end part of the mode line (decision F8): "LF", "CRLF", "CR",
  // or "Mixed(<default>)" while some line end differs from the default.
  std::wstring eol_text() const;
  // Line ends of the main buffer that differ from its default line end.
  size_t other_line_ends() const;
  size_t top_line() const { return top_.line; }
  size_t point_line() const;
  // Column of point from the start of its logical line (the mode line's C).
  int point_column() const;
  int tab_width() const { return tab_width_; }
  void set_tab_width(int width);

  // ---- View (docs/PLAN.md 4.5 - 4.7) ----
  // The view settings in effect: the settings file's values, or the values
  // View commands set for this window (decision E6).
  DisplayOptions display_options() const;
  WrapMode wrap_mode() const;
  bool line_numbers() const;
  bool eol_marks() const;
  bool whitespace_marks() const;
  // Cells of the line-number gutter (decision E2).
  int gutter_columns() const;
  // Cells across the text area right of the gutter (at least 1).
  int text_columns() const;
  // Wrap width in cells for the window (0: no wrapping).
  int wrap_width() const;
  // The wrap layout of the main buffer, configured for the current window
  // width, wrap mode and tab width.
  const WrapLayout& layout() const;
  // The first screen row of the window.
  RowPos top() const { return top_; }
  void set_top(RowPos top) { top_ = top; }
  // The screen row of point and its cell column within that row.
  struct PointView {
    RowPos pos;
    int column = 0;
  };
  PointView point_view() const;
  // Builds screen row `pos` for the visible columns [left, left + columns).
  void display_row(RowPos pos, int left, int columns, DisplayRow* out) const;
  // The screen row after `pos`; false at the end of the buffer.
  bool next_row(RowPos* pos) const;
  // Scrolls the window so that point is on screen (by screen rows).
  void ensure_point_visible();
  // Vertical scroll bar (logical-line approximation).
  ScrollBarState scroll_bar();
  // Scroll bar actions: by `rows` screen rows (negative: up), or to put
  // logical line `line` at the top. The view stops where the last row is on
  // the bottom row; point moves onto the screen when it would leave it.
  void scroll_by_rows(long long rows);
  void scroll_to_line(size_t line);
  // text-scale steps of this window (decision E5) and the resulting font
  // pixel height at `dpi`.
  int text_scale() const { return text_scale_; }
  double font_pixels(double dpi) const;
  // View commands.
  void toggle_line_numbers();
  void toggle_eol_marks();
  void toggle_whitespace_marks();
  void set_wrap_mode(WrapMode mode);
  // +1 / -1 steps, or 0 to reset.
  void text_scale_adjust(int steps);
  // Moves the cursor of the main buffer (for example on a mouse click). Like
  // any other command, it ends a typing undo group and the goal column.
  void set_point(size_t offset);

  // ---- Used by commands ----
  Host& host() { return host_; }
  Buffer& main_buffer() { return main_; }
  Buffer& minibuffer() { return mini_; }
  // The buffer that keys edit: the minibuffer while it is active.
  Buffer& current() { return minibuffer_active() ? mini_ : main_; }
  void set_message(std::wstring text) { message_ = std::move(text); }
  // The running command failed: shows `text`; a command sequence stops here.
  void fail(std::wstring text) {
    message_ = std::move(text);
    failed_ = true;
  }
  // The prefix argument of the running command (1 without one) and whether
  // one was given.
  int prefix_count() const { return command_prefix_.count(); }
  bool has_prefix_arg() const { return command_prefix_.active; }
  // Arguments of the running command (from its binding, M-x or the
  // minibuffer).
  const std::vector<CommandArg>& command_args() const { return command_args_; }
  const std::string& last_command() const { return last_command_; }
  const std::string& this_command() const { return this_command_; }
  Key last_key() const { return last_key_; }
  int goal_column() const { return goal_column_; }
  void set_goal_column(int column) { goal_column_ = column; }
  void set_top_line(size_t line) { top_ = RowPos{line, 0}; }
  int visible_lines();
  // Position of recenter-top-bottom in its center -> top -> bottom cycle.
  int& recenter_cycle() { return recenter_cycle_; }

  // Shows "Buffer is read-only" and returns false for a read-only buffer.
  bool check_writable(Buffer& b);
  // Edits through these record undo and move point and mark like markers.
  // insert() inserts at point and leaves point after the text; insert_at()
  // leaves a point equal to `offset` before the text. Text inserted into the
  // single-line minibuffer is cut at its first '\n'.
  bool insert(Buffer& b, std::wstring_view text);
  bool insert_at(Buffer& b, size_t offset, std::wstring_view text);
  bool erase(Buffer& b, size_t from, size_t to);
  // Replaces [from, to) with `text` as one edit. The mark keeps its logical
  // place: before the range it stays, after it it shifts by the length
  // change, inside it keeps its offset (clamped to the new text). Point is
  // left for the caller to set.
  bool replace(Buffer& b, size_t from, size_t to, std::wstring_view text);
  // Undo/redo with mark adjustment; false when there is nothing to do.
  bool undo(Buffer& b);
  bool redo(Buffer& b);

  void start_quoted_insert() { quoted_insert_pending_ = true; }

  // Minibuffer
  void start_minibuffer(MinibufferPurpose purpose, std::wstring prompt, std::wstring_view initial);
  void abort_minibuffer();
  void exit_minibuffer();
  void complete_minibuffer();
  void minibuffer_history_previous();
  void minibuffer_history_next();
  void set_minibuffer_text(std::wstring_view text);

  // Prefix argument commands (C-u, M-<digit>, M--).
  void universal_argument();
  void digit_argument();
  void negative_argument();
  // M-x: reads a command name (Tab completes all registered names, M-p/M-n
  // history) and runs it with the prefix argument typed before M-x.
  void execute_extended_command();

  // set-eol (decision F7): every line end of the main buffer becomes `eol`,
  // which also becomes the default, as one undo group. Nothing changes (and
  // the buffer stays unmodified) when both already are `eol`.
  void set_eol(Eol eol);
  // read-only-mode (C-x C-q, decision F6).
  void toggle_read_only();
  // Encodings (M4b, PLAN 2 "encodings that can be named"); `name` is one of
  // encoding_names() (another name fails with a message).
  // set-buffer-file-coding-system: the encoding of the next save; the text
  // stays as it is and the buffer becomes modified (as in Emacs). Refused in
  // read-only-mode, like set-eol.
  void set_buffer_file_coding_system(std::wstring_view name);
  // revert-buffer-with-coding-system: reads the file again from disk in
  // `name`, after a yes/no question when the buffer is modified. The undo
  // history is dropped; point goes to the same line number. Nothing happens
  // (with a message) without a file name or when the file is not on disk.
  void revert_buffer_with_coding_system(std::wstring_view name);
  // find-file-with-encoding: request_open(path, name).
  void find_file_with_encoding(std::wstring_view path, std::wstring_view name);

  // File commands
  void find_file();
  // Opens the settings file like find-file; a missing file opens as a new
  // buffer holding the commented template (written only when saved).
  void find_user_init_file();
  void save_buffer();
  void write_file();
  void quit();
  // Windows is ending the session (log off, shutdown): asks to save a
  // modified buffer like quit. Returns true when the session may end (saved,
  // "Don't Save" or unmodified); false on Cancel, a failed save, or an
  // untitled buffer whose file name is now asked in the minibuffer.
  bool confirm_session_end();
  std::wstring buffer_name() const;

 private:
  enum class AfterSave { kNone, kQuit, kOpen };

  Keymap::Lookup lookup(const KeySequence& keys) const;
  // Runs a key's binding with the pending prefix argument; then a command
  // chosen in the minibuffer (M-x), if any. False when a command failed.
  bool dispatch(const Binding& binding, bool typing);
  bool run_binding(const Binding& binding, bool typing);
  // Runs one call. `own_group`: the call is its own undo group (false inside
  // a command sequence, which is one group).
  bool run_call(const CommandCall& call, const PrefixArg& prefix, bool typing, bool own_group);
  // Takes the prefix argument being typed for the next command.
  PrefixArg take_prefix();
  // Drops pending prefix keys, ESC and the prefix argument being typed.
  void cancel_pending_input();
  void start_reading_arguments(const CommandInfo& info, const PrefixArg& prefix);
  void finish_command_name(const std::wstring& input);
  void finish_argument(const std::wstring& input);
  History& history_for(MinibufferPurpose purpose);
  void rebuild_keymaps(const Config& config, std::vector<Diagnostic>* errors);
  void show_settings_errors();
  void finish_command(Buffer& b);
  void note_non_command();
  void handle_quoted_insert(const Key& key);
  void adjust_markers(Buffer& b, size_t offset, size_t inserted, size_t erased);
  // After config_ changed: the tab width, and View command values that the
  // file's new values end (decision E6).
  void apply_view_settings();
  // Tells the wrap layout about a change of the main buffer at `offset`.
  void note_text_edit(const Buffer& b, size_t offset, size_t inserted, size_t erased);
  // The first screen row that shows the last row on the bottom row.
  RowPos max_top();
  // Moves point onto the screen rows [top_, top_ + visible) when it is off.
  void keep_point_on_screen();
  // `offset` within the main buffer, on the cluster boundary at or before it
  // (decision G3).
  size_t clamp_main_offset(size_t offset) const;
  // Starts a mouse action as a command of its own: clears the message and
  // pending input and ends the typing undo group.
  void begin_mouse_command();

  std::wstring default_directory() const;
  // Reads `path` into the main buffer: in `encoding` (a name of
  // encoding_names()), or detected when it is empty. False (the buffer
  // unchanged) when it cannot be read.
  bool load_file(const std::wstring& path, std::wstring_view encoding = {});
  bool save_to(const std::wstring& path);
  // The line end of new buffers and of files without any (new_file.eol).
  Eol new_file_eol() const;
  // The encoding of new buffers (new_file.encoding).
  FileFormat new_file_format() const;
  // The first argument of find-file-with-encoding is a file name: the
  // minibuffer starts in default_directory() and completes file names.
  bool reads_file_name() const;
  // True when `path` was saved by this process before (decision F3: only the
  // first save of a file makes "<file>~").
  bool saved_before(const std::wstring& path) const;
  void finish_write_file(const std::wstring& input);
  void run_after_save();
  void open_after_choice(const std::wstring& path, const std::wstring& encoding);

  Host& host_;
  Buffer main_;
  Buffer mini_;
  // global, minibuffer, isearch, query_replace (KeymapKind order).
  Keymap maps_[kKeymapCount];

  // Settings
  std::wstring settings_path_;
  bool settings_file_seen_ = false;  // the file existed at the last read
  Config config_;
  std::vector<Diagnostic> settings_errors_;
  bool message_is_settings_ = false;  // message_ shows settings errors

  // Prefix argument: the one being typed, and the one of the running command.
  PrefixArg prefix_;
  std::wstring prefix_display_;  // "C-u 1 2"
  PrefixArg command_prefix_;
  std::wstring command_prefix_display_;
  std::wstring key_display_;  // the keys of the running command
  std::vector<CommandArg> command_args_;
  bool failed_ = false;

  // M-x and arguments read in the minibuffer.
  PrefixArg minibuffer_prefix_;  // prefix argument for the command being read
  CommandCall reading_call_;     // command whose arguments are being read
  std::optional<std::pair<CommandCall, PrefixArg>> deferred_call_;
  History command_history_;
  History argument_history_;

  KeySequence pending_;
  std::wstring pending_display_;
  bool meta_pending_ = false;
  bool quoted_insert_pending_ = false;
  Key last_key_;
  std::string last_command_;
  std::string this_command_;
  int goal_column_ = 0;
  int recenter_cycle_ = 0;
  RowPos top_;
  int tab_width_ = 4;
  std::wstring message_;

  // View state of this window (decisions E5, E6).
  mutable WrapLayout wrap_;
  ViewSetting<bool> line_numbers_view_;
  ViewSetting<bool> eol_marks_view_;
  // On/off over the file's whitespace flags.
  ViewSetting<WhitespaceFlags> whitespace_view_;
  ViewSetting<WrapMode> wrap_view_;
  int text_scale_ = 0;

  // Mouse (decision B5): the press being held and where the region starts.
  bool mouse_pressed_ = false;
  bool mouse_dragged_ = false;
  size_t mouse_anchor_ = 0;

  MinibufferPurpose purpose_ = MinibufferPurpose::kNone;
  std::wstring prompt_;
  History file_history_;

  AfterSave after_save_ = AfterSave::kNone;
  std::wstring after_save_path_;
  std::wstring after_save_encoding_;  // the encoding of after_save_path_ ("": detect)

  // Encodings (M4b). `coding_changed_`: set-buffer-file-coding-system since
  // the file was read or saved. `encoding_named_`: the buffer's encoding was
  // chosen (--encoding, find-file-with-encoding of a new file, or
  // set-buffer-file-coding-system), so new_file.encoding does not replace
  // it. `undecodable_byte_` / `undecodable_line_`: where the first problem
  // of an undecodable file is (format.undecodable), for the save message.
  bool coding_changed_ = false;
  bool encoding_named_ = false;
  size_t undecodable_byte_ = 0;
  size_t undecodable_line_ = 0;

  // The file of the main buffer as it was when read or last saved (decision
  // F2); `disk_known_` is false when that could not be read.
  bool disk_known_ = false;
  FileInfo disk_;
  // Files saved by this process (decision F3).
  std::vector<std::wstring> saved_paths_;
};

}  // namespace geditor
