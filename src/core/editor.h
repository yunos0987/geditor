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
#include "host.h"
#include "key.h"
#include "keymap.h"
#include "minibuffer.h"

namespace geditor {

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
  // command line.
  void request_open(std::wstring_view path);
  // Runs a command by name as if its key had been pressed (for commands
  // without a default key, and the window's close button). Unknown names
  // show a message.
  void execute_command(std::string_view name);

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
  // "-** name  UTF-8 LF  L12 C5".
  std::wstring mode_line() const;
  size_t top_line() const { return top_line_; }
  size_t point_line() const;
  int point_column() const;
  int tab_width() const { return tab_width_; }
  void set_tab_width(int width);
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
  void set_top_line(size_t line) { top_line_ = line; }
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
  void ensure_point_visible();
  void adjust_markers(Buffer& b, size_t offset, size_t inserted, size_t erased);

  std::wstring default_directory() const;
  bool load_file(const std::wstring& path);
  bool save_to(const std::wstring& path);
  void finish_write_file(const std::wstring& input);
  void run_after_save();
  void open_after_choice(const std::wstring& path);

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
  size_t top_line_ = 0;
  int tab_width_ = 4;
  std::wstring message_;

  MinibufferPurpose purpose_ = MinibufferPurpose::kNone;
  std::wstring prompt_;
  History file_history_;

  AfterSave after_save_ = AfterSave::kNone;
  std::wstring after_save_path_;
};

}  // namespace geditor
