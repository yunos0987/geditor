// The editor: buffers, key dispatch, minibuffer and file flows. The host feeds
// keys and IME strings in and reads the state back for drawing.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "buffer.h"
#include "host.h"
#include "key.h"
#include "keymap.h"
#include "minibuffer.h"

namespace geditor {

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
  // without a default key; M-x in M2). Unknown names show a message.
  void execute_command(std::string_view name);

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

  // File commands
  void find_file();
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
  void run_command(std::string_view name, bool typing);
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
  Keymap global_map_;
  Keymap minibuffer_map_;

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
