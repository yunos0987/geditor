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
#include "search.h"

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

  // ---- Search and replace (M5) ----
  bool isearch_active() const { return isearch_.active; }
  bool query_replace_active() const { return replace_.active; }
  // The minibuffer, isearch or query-replace reads the keys (PLAN 2 "menu
  // bar": every menu item is greyed meanwhile).
  bool input_mode_active() const { return minibuffer_active() || isearch_active() || query_replace_active(); }
  // The isearch pattern being typed ("" outside isearch).
  const std::wstring& isearch_pattern() const { return isearch_.now.pattern; }
  // The matches to highlight in the `rows` screen rows from top(): every
  // non-empty match ("search_match") and the current one
  // ("search_current"), edges on cluster boundaries. Empty outside isearch
  // and query-replace, and while the isearch pattern is empty, invalid or
  // failing (failing: no current match).
  struct SearchHighlights {
    std::vector<SearchMatch> matches;
    bool has_current = false;
    SearchMatch current;
  };
  SearchHighlights search_highlights(int rows) const;
  // The most matches search_highlights returns.
  static constexpr size_t kMaxHighlights = 10000;

  // isearch (PLAN 4.4 "during isearch"). Starting fails while the
  // minibuffer, isearch or query-replace is active.
  void isearch_start(bool forward, bool regexp);
  // C-s / C-r: the next / previous match; the other direction first turns
  // the search around on the current match. An empty pattern takes the last
  // search string. A failing search wraps around the buffer.
  void isearch_repeat(bool forward);
  // DEL: back to the state before the last input (a character or a repeat).
  void isearch_delete_char();
  // RET: ends isearch at the match; the mark goes where the search started.
  void isearch_exit();
  // C-g: ends isearch with point and the view back where it started.
  void isearch_abort();
  void isearch_toggle_regexp();
  void isearch_toggle_case_fold();
  // M-%: ends isearch and reads the replacement of the isearch pattern.
  void isearch_query_replace();
  // query-replace FROM TO from point to the end of the buffer (PLAN 4.4
  // "during query-replace"). The replacements of one run are one undo unit.
  void query_replace(bool regexp, std::wstring_view from, std::wstring_view to);
  // y / SPC (`and_exit`: "."), n / DEL, !, q / RET.
  void query_replace_act(bool and_exit);
  void query_replace_skip();
  void query_replace_all();
  void query_replace_exit();
  // goto-line: point to the start of line `line` (1-based; clamped to the
  // first and last line), after pushing the mark as Emacs does.
  void goto_line(int64_t line);
  // Ends isearch (as RET) or query-replace (as q): before the host opens a
  // dropped file or ends the session, and before execute_command.
  void end_search_modes();

  // ---- Keyboard macros (M6, PLAN 4.4 "macro") ----
  // A macro is the key events (keys and IME strings) typed between
  // kmacro-start-macro and kmacro-end-macro, replayed through handle_key and
  // insert_text. Mouse actions and menu commands are not recorded. It lives
  // until the process ends.
  bool macro_recording() const { return macro_recording_; }
  bool macro_executing() const { return macro_executing_; }
  // Events of the last macro (0: none).
  size_t macro_size() const { return macro_.size(); }
  void kmacro_start_macro();
  void kmacro_end_macro();
  // Ends a recording (and runs nothing), else runs the last macro the
  // prefix argument's number of times as one undo unit, after this key's
  // command; then `e` runs it again. A run asks Host::quit_requested every
  // macro poll interval and stops with "Quit" when it says so (retry R2).
  void kmacro_end_and_call_macro();
  // Tests only: how often a macro run asks Host::quit_requested (0: before
  // every event; default 50 ms).
  void set_macro_poll_ms_for_testing(int ms) { macro_poll_ms_ = ms; }

  // ---- Encoding of a new buffer (B2) ----
  // --encoding=NAME without a file: the untitled buffer is saved in `name`
  // (one of encoding_names(); another name shows "Unknown encoding").
  void set_new_buffer_encoding(std::wstring_view name);

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
  // Vertical scroll bar (PLAN 4.7): logical lines when the text has more
  // logical lines than the window has rows (or everything fits); otherwise
  // (few long lines that do not fit, B6) character offsets.
  ScrollBarState scroll_bar();
  // Scroll bar actions: by `rows` screen rows (negative: up), or to put
  // logical line `line` at the top. The view stops where the last row is on
  // the bottom row; point moves onto the screen when it would leave it.
  void scroll_by_rows(long long rows);
  void scroll_to_line(size_t line);
  // The thumb dragged to (or SB_TOP / SB_BOTTOM at) `pos` in the units of
  // scroll_bar() now: a logical line, or a character offset (in its units)
  // whose screen row goes to the top (the thumb at the bottom, pos + page
  // past max: the last screen).
  void scroll_to_bar_position(size_t pos);
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
  // The running command failed: shows `text`; a command sequence stops here
  // (and a keyboard macro run).
  void fail(std::wstring text) {
    message_ = std::move(text);
    failed_ = true;
    ++errors_;
  }
  // The user answered a question No or Cancel: like a failure, this stops a
  // keyboard macro run and its repeats (M6 retry R3); the message stays.
  void declined() { ++errors_; }
  // Kills (M6, PLAN 2 "kill and the clipboard"): `text` was killed; right
  // after another kill it joins the text of the kills in a row (before it
  // when `prepend`). At the top level the joined text is written to the
  // clipboard now, and false (with "Cannot write to the clipboard") means
  // the text must not be deleted. Inside a keyboard macro run or a command
  // sequence the write waits (retry R1: one write per run of kills, not one
  // per kill): it happens before the next command that is no kill and at the
  // end of the run or sequence (flush_kills). Then this returns true.
  bool kill_add(std::wstring_view text, bool prepend);
  // Writes the waiting kill text to the clipboard; false (with the message)
  // when that fails. True when nothing waits.
  bool flush_kills();
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

  // The keymaps of the input mode now (the minibuffer, query-replace and
  // isearch read their own keymap; the first two fall back to the global
  // keymap without the Windows compatible keys).
  Keymap::Lookup lookup(const KeySequence& keys) const;
  void handle_key_impl(const Key& key);
  // After each input: query-replace follows edits and moves made by other
  // commands; the echo area shows the prompt of isearch or query-replace
  // (with the message of the input in brackets).
  void after_input();
  // The echo area prompt of isearch / query-replace.
  std::wstring isearch_prompt() const;
  std::wstring query_replace_prompt() const;

  // isearch state. A step is what DEL goes back to.
  struct IsearchStep {
    std::wstring pattern;
    bool forward = true;
    bool regexp = false;
    int case_override = -1;  // -1: search.case_fold; 0: ignore case; 1: case-sensitive (M-c)
    bool has_match = false;  // `match` is the current match (also while failing: the last one)
    SearchMatch match;
    bool failing = false;
    bool wrapped = false;
    bool invalid = false;  // the regexp does not compile (the message is shown)
    size_t origin = 0;     // where the search runs from without a match
    size_t point = 0;
  };
  struct IsearchState {
    bool active = false;
    IsearchStep now;
    std::vector<IsearchStep> history;
    size_t start_point = 0;
    RowPos start_top;
  };
  using LineReplacement = SearchPattern::LineReplacement;
  struct ReplaceState {
    bool active = false;
    bool regexp = false;
    std::wstring from;
    std::wstring to;
    size_t start_point = 0;
    size_t undo_groups = 0;  // undo groups before the first replacement
    size_t count = 0;        // replacements made
    SearchMatch match;       // the current match
    // What the last query-replace action left, to notice other commands.
    uint64_t revision = 0;
    size_t point = 0;
  };
  // Compiles the isearch pattern of `isearch_.now` into matcher_; false for
  // an invalid regexp (now.invalid set and its message shown).
  bool isearch_prepare();
  // Searches for isearch_.now: `repeat` for C-s / C-r, else from the current
  // match (the pattern or a toggle changed).
  void isearch_search(bool repeat);
  void isearch_add_text(std::wstring_view text);
  void isearch_push();
  // Ends isearch: the search string is kept for the next empty C-s; `abort`
  // puts point back.
  void isearch_end(bool abort);
  // The current match of query-replace: the next one at or after `from`.
  // Finishes query-replace when there is none. `show`: scroll it into view
  // now (query_replace_all leaves that to the end of its command).
  void query_replace_next(size_t from, bool not_empty_at_from, bool show = true);
  void query_replace_finish(std::wstring message);
  bool query_replace_replace_current();
  // The match `m` widened to cluster boundaries.
  SearchMatch snap_match(const SearchMatch& m) const;
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
  // Shift selection (PLAN 4.4) before a movement command in `b`: a Shift key
  // (`shifted`) activates a temporary mark at point unless a region is
  // active; a key without Shift removes a temporary mark.
  void shift_select(Buffer& b, bool shifted);

  // Keyboard macros (M6).
  struct MacroEvent {
    bool is_text = false;  // an IME string (`text`), else `key`
    Key key;
    std::wstring text;
  };
  // A key or IME string typed while recording: added to the recording; C-g
  // ends the recording (the last macro stays).
  void record_macro_key(const Key& key);
  void record_macro_text(std::wstring_view text);
  // Input other than a key (a click, the wheel, the scroll bar, a menu
  // command, a dropped file) ends the `e` repeat (retry R4).
  void end_macro_repeat() {
    if (!macro_executing_) macro_repeat_ = false;
  }
  // True when the next key starts a key sequence typed outside the
  // minibuffer: the recording drops the keys from there that end it.
  bool macro_sequence_starts() const;
  // Runs the macro that kmacro-end-and-call-macro asked for (after the
  // command that asked, from the input entry points).
  void run_pending_macro();

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
  // The global keymap without the Windows compatible keys: the fallback of
  // the minibuffer and query-replace, and the keymap of a key that ends
  // isearch (M5: the C-S- keys do not run inside these modes).
  Keymap global_in_modes_;
  // The key sequence being typed started in an input mode (it ended
  // isearch): it is looked up in global_in_modes_.
  bool sequence_in_mode_ = false;

  // Search and replace (M5).
  mutable SearchPattern matcher_;
  IsearchState isearch_;
  ReplaceState replace_;
  // The last isearch strings (literal, regexp), for C-s on an empty pattern.
  std::wstring last_search_[2];

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
  // Failures so far (fail(), undefined keys, failing isearch): a keyboard
  // macro run stops when it grows.
  uint64_t errors_ = 0;
  // The key that runs the next command was shift-translated (S-<left> runs
  // backward-char): shift selection.
  bool shift_select_ = false;
  // The kills in a row: kill_front_rev_ holds the text added before (in
  // reverse order of units), kill_back_ the text from the first kill on, so
  // that adding either way costs the added text only (retry R1).
  std::wstring kill_back_;
  std::wstring kill_front_rev_;
  bool kill_pending_ = false;  // the joined text is not on the clipboard yet
  int kill_defer_ = 0;         // > 0: inside a macro run or command sequence
  // The joined text of the kills in a row.
  std::wstring kill_joined() const;
  void clear_kills();

  // Keyboard macros (M6).
  bool macro_recording_ = false;
  bool macro_executing_ = false;
  bool macro_repeat_ = false;   // `e` runs the macro again
  int macro_runs_ = 0;          // runs asked for by kmacro-end-and-call-macro
  int macro_poll_ms_ = 50;      // how often a run asks Host::quit_requested
  size_t macro_sequence_start_ = 0;
  std::vector<MacroEvent> macro_recorded_;
  std::vector<MacroEvent> macro_;
  // B4 / B5: the encoding the file was read with, and whether the bytes that
  // did not read became U+FFFD (else they read but are not given back).
  std::wstring read_encoding_;
  bool undecodable_replaced_ = false;

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
