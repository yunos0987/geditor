// File flows of the editor: find-file / drag and drop, save-buffer,
// write-file and quit (docs/PLAN.md 2 "open another file").

#include <cwchar>
#include <iterator>

#include "config.h"
#include "editor.h"
#include "path.h"
#include "unicode.h"

namespace geditor {
namespace {

// A path written with a trailing separator names a directory.
bool names_directory(std::wstring_view path) {
  return !path.empty() && (path.back() == L'\\' || path.back() == L'/');
}

std::wstring cannot_access(const std::wstring& path) {
  return L"Cannot access " + path;
}

}  // namespace

std::wstring Editor::default_directory() const {
  if (!main_.path.empty()) return directory_of(main_.path);
  return with_trailing_separator(host_.current_directory());
}

void Editor::find_file() {
  if (minibuffer_active()) {
    fail(kRecursiveMinibufferMessage);
    return;
  }
  start_minibuffer(MinibufferPurpose::kFindFile, L"Find file: ", default_directory());
}

void Editor::find_user_init_file() {
  if (settings_path_.empty()) {
    fail(L"No settings file: the user profile folder is unknown");
    return;
  }
  request_open(settings_path_);
}

void Editor::find_file_with_encoding(std::wstring_view path, std::wstring_view name) {
  request_open(path, name);
}

void Editor::request_open(std::wstring_view raw_path, std::wstring_view raw_encoding) {
  if (minibuffer_active()) abort_minibuffer();
  cancel_pending_input();
  quoted_insert_pending_ = false;
  end_macro_repeat();  // a dropped file is input too (retry R4)

  const std::wstring encoding(raw_encoding);
  FileFormat named;
  if (!encoding.empty() && !parse_encoding(encoding, &named)) {
    message_ = unknown_encoding_message(encoding);
    return;
  }
  const std::wstring path = normalize_path(raw_path, host_.current_directory());
  if (!main_.path.empty() && same_path(path, main_.path)) {
    message_ = path + L" is already open in this window";
    if (!encoding.empty()) message_ += L" (revert-buffer-with-coding-system reads it again as " + encoding + L")";
    return;
  }
  const FileKind kind = host_.file_kind(path);
  if (kind == FileKind::kError) {
    message_ = cannot_access(path);
    return;
  }
  if (kind == FileKind::kDirectory) {
    message_ = path + L" is a directory";
    return;
  }
  if (names_directory(raw_path)) {
    message_ = L"No such directory: " + path;
    return;
  }
  if (main_.path.empty() && !modified() && main_.text.length() == 0) {
    load_file(path, encoding);
    return;
  }
  switch (host_.ask_open_choice(path)) {
    case OpenChoice::kReplace:
      open_after_choice(path, encoding);
      return;
    case OpenChoice::kNewWindow: {
      std::wstring error;
      if (host_.launch_new_window(path, encoding, error)) {
        message_ = L"Opened " + path + L" in a new window";
      } else {
        message_ = error.empty() ? L"Cannot start a new window for " + path : error;
      }
      return;
    }
    case OpenChoice::kCancel:
      declined();  // stops a keyboard macro run (retry R3)
      return;
  }
}

void Editor::open_after_choice(const std::wstring& path, const std::wstring& encoding) {
  if (modified()) {
    switch (host_.ask_save_changes(buffer_name())) {
      case SaveChoice::kSave:
        if (main_.path.empty()) {
          start_minibuffer(MinibufferPurpose::kWriteFile, L"Write file: ", default_directory());
          after_save_ = AfterSave::kOpen;
          after_save_path_ = path;
          after_save_encoding_ = encoding;
          return;
        }
        if (!save_to(main_.path)) return;
        break;
      case SaveChoice::kDontSave:
        break;
      case SaveChoice::kCancel:
        declined();
        return;
    }
  }
  load_file(path, encoding);
}

bool Editor::load_file(const std::wstring& path, std::wstring_view encoding) {
  FileFormat named;
  const bool has_named = !encoding.empty() && parse_encoding(encoding, &named);
  if (!encoding.empty() && !has_named) {
    message_ = unknown_encoding_message(encoding);
    return false;
  }
  const FileKind kind = host_.file_kind(path);
  if (kind == FileKind::kError || kind == FileKind::kDirectory) {
    message_ = kind == FileKind::kError ? cannot_access(path) : path + L" is a directory";
    return false;
  }
  if (kind == FileKind::kNone) {
    main_.reset();
    main_.path = path;
    main_.format = has_named ? named : new_file_format();
    main_.format.eol = new_file_eol();
    coding_changed_ = false;
    encoding_named_ = has_named;
    disk_known_ = true;
    disk_ = FileInfo();
    top_ = RowPos();
    goal_column_ = 0;
    if (!settings_path_.empty() && same_path(path, settings_path_)) {
      // A new settings file starts with the commented example. Inserting it
      // is an edit: the buffer is modified and nothing is written yet.
      insert_at(main_, 0, settings_template());
    }
    message_ = L"(New file)";
    return true;
  }
  // The time, size and attributes before the bytes: a change in between is
  // seen as a change at the next save (decision F2).
  FileInfo info;
  std::wstring info_error;
  const bool info_known = host_.file_info(path, info, info_error);
  std::string bytes;
  std::wstring error;
  if (!host_.read_file(path, bytes, error)) {
    message_ = error.empty() ? L"Cannot read " + path : error;
    return false;
  }
  DecodedText decoded = has_named ? decode_as(bytes, named) : decode_detected(bytes);
  bytes = std::string();
  if (decoded.status == DecodeStatus::kTooLarge) {
    message_ = path + L": file too large; not opened";
    return false;
  }
  main_.reset();
  // A file without line ends uses new_file.eol (decision F1).
  const Eol eol = default_eol_of(decoded.eol_counts, new_file_eol());
  main_.text = make_piece_tree(decoded);
  main_.format = decoded.format;
  main_.format.eol = eol;
  main_.path = path;
  coding_changed_ = false;
  encoding_named_ = false;
  undecodable_byte_ = decoded.error_offset;
  undecodable_line_ = decoded.error_line;
  read_encoding_ = encoding_name(decoded.format);
  undecodable_replaced_ = decoded.replaced || decoded.detection_failed;
  top_ = RowPos();
  goal_column_ = 0;
  message_.clear();
  if (decoded.format.undecodable) {
    // PLAN 2 "bytes that cannot be read": the first one, by byte offset and
    // line; the buffer shows "!" until it is saved or read again.
    const std::wstring where = L"(first at byte offset " + std::to_wstring(decoded.error_offset) + L", line " +
                               std::to_wstring(decoded.error_line + 1) + L")";
    const std::wstring name = encoding_name(decoded.format);
    if (decoded.detection_failed) {
      message_ = path + L": not valid UTF-8 or Shift_JIS; read as UTF-8 with U+FFFD for the bytes that are not " +
                 where;
    } else if (decoded.replaced) {
      message_ = path + L": bytes that are not valid " + name + L" were read as U+FFFD " + where;
    } else {
      message_ = path + L": " + name + L" does not give the same bytes back " + where;
    }
  }
  disk_known_ = info_known;
  disk_ = info_known ? info : FileInfo();
  // A file with the read-only attribute opens in read-only-mode (F6).
  main_.read_only = info_known && info.read_only;
  if (!info_known) {
    if (!message_.empty()) message_ += L"; ";
    message_ += L"Cannot read the time and attributes of " + path + L": " + info_error +
                L" (changes on disk will not be noticed)";
  }
  return true;
}

FileFormat Editor::new_file_format() const {
  FileFormat format;
  if (!parse_encoding(config_.new_file_encoding, &format)) format = FileFormat();
  return format;
}

void Editor::set_buffer_file_coding_system(std::wstring_view name) {
  FileFormat named;
  if (!parse_encoding(name, &named)) {
    fail(unknown_encoding_message(name));
    return;
  }
  if (!check_writable(main_)) return;
  const std::wstring display = encoding_name(named);
  if (named.codec == main_.format.codec && named.bom == main_.format.bom) {
    message_ = L"The encoding for saving is already " + display;
    return;
  }
  // Only the next save changes: the text stays, the buffer is modified (as
  // in Emacs) until it is saved. "!" stays: the bytes that could not be
  // read are still lost.
  main_.format.codec = named.codec;
  main_.format.bom = named.bom;
  coding_changed_ = true;
  encoding_named_ = true;
  message_ = L"Encoding for saving: " + display;
}

void Editor::set_new_buffer_encoding(std::wstring_view name) {
  FileFormat named;
  if (!parse_encoding(name, &named)) {
    message_ = unknown_encoding_message(name);
    return;
  }
  // As a named new file (load_file): the buffer stays unmodified, and
  // new_file.encoding does not replace the encoding (encoding_named_).
  main_.format.codec = named.codec;
  main_.format.bom = named.bom;
  encoding_named_ = true;
}

void Editor::revert_buffer_with_coding_system(std::wstring_view name) {
  FileFormat named;
  if (!parse_encoding(name, &named)) {
    fail(unknown_encoding_message(name));
    return;
  }
  if (main_.path.empty()) {
    fail(L"Buffer is not visiting a file");
    return;
  }
  const std::wstring path = main_.path;
  const FileKind kind = host_.file_kind(path);
  if (kind == FileKind::kNone) {
    fail(L"File " + path + L" is not on disk; nothing to read again");
    return;
  }
  if (kind == FileKind::kError || kind == FileKind::kDirectory) {
    fail(kind == FileKind::kError ? cannot_access(path) : path + L" is a directory");
    return;
  }
  const std::wstring display = encoding_name(named);
  if (modified() &&
      !host_.ask_yes_no(L"Discard the changes to " + buffer_name() + L" and read it again as " + display + L"?")) {
    message_ = L"Cancelled";
    declined();
    return;
  }
  const size_t line = point_line();
  const size_t top = top_.line;
  const bool was_read_only = main_.read_only;
  if (!load_file(path, name)) return;
  // B3: read-only-mode turned on with C-x C-q stays on (load_file turns it
  // on for a file with the read-only attribute).
  if (was_read_only) main_.read_only = true;
  // Undo history is gone (load_file); point keeps its line number.
  const PieceTree& text = main_.text;
  main_.point = text.line_start(std::min(line, text.line_count() - 1));
  top_ = RowPos{std::min(top, text.line_count() - 1), 0};
  ensure_point_visible();
  if (message_.empty()) message_ = L"Read " + path + L" again as " + display;
}

Eol Editor::new_file_eol() const {
  Eol eol = Eol::kLf;
  parse_eol(config_.new_file_eol, &eol);
  return eol;
}

bool Editor::saved_before(const std::wstring& path) const {
  for (const std::wstring& p : saved_paths_) {
    if (same_path(p, path)) return true;
  }
  return false;
}

bool Editor::save_to(const std::wstring& path) {
  // Never write over something that cannot be inspected.
  const FileKind kind = host_.file_kind(path);
  if (kind == FileKind::kError || kind == FileKind::kDirectory) {
    fail(kind == FileKind::kError ? cannot_access(path) : path + L" is a directory");
    return false;
  }
  FileInfo now;
  std::wstring error;
  if (!host_.file_info(path, now, error)) {
    fail(L"Cannot write " + path + L": reading the file time: " + error);
    return false;
  }
  // PLAN 2 "bytes that cannot be read" (M4b, revised in M6 as B4 / B5): a
  // buffer read with bytes that could not be read (or not given back) asks
  // before it is saved in any encoding, naming the encoding the file was
  // read with. Yes goes on as any save: a U+FFFD (or another character)
  // that the encoding cannot write still stops it below.
  const std::wstring encoding = encoding_name(main_.format);
  if (main_.format.undecodable) {
    const std::wstring where = L"first at byte offset " + std::to_wstring(undecodable_byte_) + L", line " +
                               std::to_wstring(undecodable_line_ + 1);
    const std::wstring read_as = read_encoding_.empty() ? encoding : read_encoding_;
    const std::wstring what = undecodable_replaced_
                                  ? L"bytes that are not valid " + read_as + L" (" + where + L"; read as U+FFFD)"
                                  : L"bytes that " + read_as + L" does not give back the same (" + where + L")";
    // The file that was read (the buffer's), also when saving under another
    // name (write-file; retry R6).
    const std::wstring read_file = main_.path.empty() ? path : main_.path;
    if (!host_.ask_yes_no(file_name_of(read_file) + L" was read as " + read_as + L" with " + what +
                          L"; their original bytes will be lost. Save as " + encoding + L" anyway?")) {
      fail(L"Not saved");
      return false;
    }
  }
  // M4a fix F1: an LF line end right after a CR line end is saved as CR, and
  // the buffer gets the same line ends (not an undo step), so that no line
  // is lost when the file is read again. A save that stops puts the pieces
  // back.
  PieceList kept;
  const size_t cr_kept = cr_kept_line_ends(main_.text, &kept);
  PieceList original;
  if (cr_kept > 0) {
    original = main_.text.pieces();
    main_.text.assign_pieces(kept);
  }
  auto restore = [&] {
    if (cr_kept > 0) main_.text.assign_pieces(original);
  };
  // PLAN 2 "characters a non-Unicode encoding cannot write": the save stops
  // at the first one (best fit included); point goes there.
  EncodedText encoded = encode_text(main_.text, main_.format);
  if (!encoded.ok) {
    restore();
    if (encoded.too_large) {
      fail(L"Cannot save as " + encoding + L": the text is too large for " + encoding);
      return false;
    }
    if (encoded.conversion_failed) {
      fail(L"Cannot save as " + encoding + L": the conversion to " + encoding + L" failed");
      return false;
    }
    size_t units = 1;
    const char32_t c = decode_at(main_.text.substr(encoded.bad_offset, 2), 0, &units);
    main_.point = clamp_main_offset(encoded.bad_offset);
    main_.mark_active = false;
    ensure_point_visible();
    wchar_t code[16];
    std::swprintf(code, std::size(code), L"U+%04X", static_cast<unsigned>(c));
    fail(L"Cannot save as " + encoding + L": " + code + L" at line " + std::to_wstring(point_line() + 1) +
         L", column " + std::to_wstring(point_column()) + L" cannot be written in " + encoding +
         L"; choose another encoding with set-buffer-file-coding-system");
    return false;
  }
  // Decision F2: the file of this buffer changed on disk since it was read
  // or saved. A file deleted on disk is saved as new without asking.
  const bool same_file = !main_.path.empty() && same_path(path, main_.path);
  if (same_file && disk_known_ && now.exists &&
      (!disk_.exists || now.size != disk_.size || now.write_time != disk_.write_time)) {
    if (!host_.ask_yes_no(file_name_of(path) + L" has changed on disk since it was read; save anyway?")) {
      restore();
      fail(L"Not saved");
      return false;
    }
  }
  // Decision F3: "<file>~" on the first save of an existing file.
  const bool backup = config_.backup && now.exists && !saved_before(path);
  error.clear();
  if (!host_.save_file(path, encoded.bytes, backup, error)) {
    restore();
    fail(L"Cannot write " + path + (error.empty() ? std::wstring() : L": " + error));
    return false;
  }
  encoded.bytes = std::string();
  const std::wstring note = error;
  if (!saved_before(path)) saved_paths_.push_back(path);
  main_.path = path;
  main_.undo.mark_saved();
  // The file now holds this text in this encoding.
  coding_changed_ = false;
  main_.format.undecodable = false;
  message_ = L"Wrote " + path;
  if (!note.empty()) message_ += L" (" + note + L")";
  if (cr_kept > 0) {
    message_ += L"; saved " + std::to_wstring(cr_kept) + (cr_kept == 1 ? L" LF line end" : L" LF line ends") +
                L" after a CR as CR to keep the lines";
  }
  // The new state on disk for the next check.
  FileInfo after;
  std::wstring after_error;
  disk_known_ = host_.file_info(path, after, after_error);
  disk_ = disk_known_ ? after : FileInfo();
  if (!disk_known_) {
    message_ += L"; cannot read its time: " + after_error + L" (changes on disk will not be noticed)";
  }
  return true;
}

void Editor::save_buffer() {
  if (!modified()) {
    message_ = L"(No changes need to be saved)";
    return;
  }
  if (main_.path.empty()) {
    write_file();
    return;
  }
  save_to(main_.path);
}

void Editor::write_file() {
  if (minibuffer_active()) {
    fail(kRecursiveMinibufferMessage);
    return;
  }
  start_minibuffer(MinibufferPurpose::kWriteFile, L"Write file: ", default_directory());
  after_save_ = AfterSave::kNone;
}

void Editor::finish_write_file(const std::wstring& input) {
  std::wstring path = normalize_path(input, host_.current_directory());
  FileKind kind = host_.file_kind(path);
  if (kind != FileKind::kDirectory && kind != FileKind::kError && names_directory(input)) {
    message_ = L"No such directory: " + path;
    after_save_ = AfterSave::kNone;
    return;
  }
  if (kind == FileKind::kDirectory && !main_.path.empty()) {
    path = with_trailing_separator(path) + file_name_of(main_.path);
    kind = host_.file_kind(path);
  }
  if (kind == FileKind::kError) {
    message_ = cannot_access(path);
    after_save_ = AfterSave::kNone;
    return;
  }
  if (kind == FileKind::kDirectory) {
    message_ = path + L" is a directory";
    after_save_ = AfterSave::kNone;
    return;
  }
  if (kind == FileKind::kFile && !same_path(path, main_.path)) {
    if (!host_.ask_yes_no(L"File " + path + L" exists; overwrite?")) {
      message_ = L"Cancelled";
      after_save_ = AfterSave::kNone;
      declined();
      return;
    }
  }
  if (!save_to(path)) {
    after_save_ = AfterSave::kNone;
    return;
  }
  run_after_save();
}

void Editor::run_after_save() {
  const AfterSave action = after_save_;
  after_save_ = AfterSave::kNone;
  if (action == AfterSave::kQuit) {
    host_.quit();
  } else if (action == AfterSave::kOpen) {
    load_file(after_save_path_, after_save_encoding_);
  }
}

bool Editor::confirm_session_end() {
  if (!modified()) return true;
  switch (host_.ask_save_changes(buffer_name())) {
    case SaveChoice::kSave:
      if (main_.path.empty()) {
        // The file name is read in the minibuffer, which cannot finish while
        // Windows waits: keep the session and let the user save, then quit.
        if (minibuffer_active()) abort_minibuffer();
        start_minibuffer(MinibufferPurpose::kWriteFile, L"Write file: ", default_directory());
        after_save_ = AfterSave::kQuit;
        return false;
      }
      return save_to(main_.path);
    case SaveChoice::kDontSave:
      return true;
    case SaveChoice::kCancel:
      declined();
      return false;
  }
  return false;
}

void Editor::quit() {
  if (!modified()) {
    host_.quit();
    return;
  }
  switch (host_.ask_save_changes(buffer_name())) {
    case SaveChoice::kSave:
      if (main_.path.empty()) {
        if (minibuffer_active()) abort_minibuffer();
        start_minibuffer(MinibufferPurpose::kWriteFile, L"Write file: ", default_directory());
        after_save_ = AfterSave::kQuit;
        return;
      }
      if (save_to(main_.path)) host_.quit();
      return;
    case SaveChoice::kDontSave:
      host_.quit();
      return;
    case SaveChoice::kCancel:
      declined();
      return;
  }
}

}  // namespace geditor
