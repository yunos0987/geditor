// File flows of the editor: find-file / drag and drop, save-buffer,
// write-file and quit (docs/PLAN.md 2 "open another file").

#include "editor.h"
#include "path.h"

namespace geditor {
namespace {

constexpr wchar_t kRecursiveMinibuffer[] = L"Command attempted to use minibuffer while in minibuffer";

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
    message_ = kRecursiveMinibuffer;
    return;
  }
  start_minibuffer(MinibufferPurpose::kFindFile, L"Find file: ", default_directory());
}

void Editor::request_open(std::wstring_view raw_path) {
  if (minibuffer_active()) abort_minibuffer();
  pending_.clear();
  pending_display_.clear();
  meta_pending_ = false;
  quoted_insert_pending_ = false;

  const std::wstring path = normalize_path(raw_path, host_.current_directory());
  if (!main_.path.empty() && same_path(path, main_.path)) {
    message_ = path + L" is already open in this window";
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
  if (main_.path.empty() && !main_.modified() && main_.text.length() == 0) {
    load_file(path);
    return;
  }
  switch (host_.ask_open_choice(path)) {
    case OpenChoice::kReplace:
      open_after_choice(path);
      return;
    case OpenChoice::kNewWindow: {
      std::wstring error;
      if (host_.launch_new_window(path, error)) {
        message_ = L"Opened " + path + L" in a new window";
      } else {
        message_ = error.empty() ? L"Cannot start a new window for " + path : error;
      }
      return;
    }
    case OpenChoice::kCancel:
      return;
  }
}

void Editor::open_after_choice(const std::wstring& path) {
  if (main_.modified()) {
    switch (host_.ask_save_changes(buffer_name())) {
      case SaveChoice::kSave:
        if (main_.path.empty()) {
          start_minibuffer(MinibufferPurpose::kWriteFile, L"Write file: ", default_directory());
          after_save_ = AfterSave::kOpen;
          after_save_path_ = path;
          return;
        }
        if (!save_to(main_.path)) return;
        break;
      case SaveChoice::kDontSave:
        break;
      case SaveChoice::kCancel:
        return;
    }
  }
  load_file(path);
}

bool Editor::load_file(const std::wstring& path) {
  const FileKind kind = host_.file_kind(path);
  if (kind == FileKind::kError || kind == FileKind::kDirectory) {
    message_ = kind == FileKind::kError ? cannot_access(path) : path + L" is a directory";
    return false;
  }
  if (kind == FileKind::kNone) {
    main_.reset();
    main_.path = path;
    top_line_ = 0;
    goal_column_ = 0;
    message_ = L"(New file)";
    return true;
  }
  std::string bytes;
  std::wstring error;
  if (!host_.read_file(path, bytes, error)) {
    message_ = error.empty() ? L"Cannot read " + path : error;
    return false;
  }
  DecodedText decoded = decode_utf8(bytes);
  bytes = std::string();
  switch (decoded.status) {
    case DecodeStatus::kOk:
      break;
    case DecodeStatus::kInvalidUtf8:
      message_ = path + L": not valid UTF-8 (byte " + std::to_wstring(decoded.error_offset) +
                 L"); not opened";
      return false;
    case DecodeStatus::kMixedEol:
      message_ = path + L": mixed line ends; not opened";
      return false;
    case DecodeStatus::kCrEol:
      message_ = path + L": CR line ends are not supported yet; not opened";
      return false;
    case DecodeStatus::kTooLarge:
      message_ = path + L": file too large; not opened";
      return false;
  }
  main_.reset();
  main_.text = PieceTree(std::move(decoded.text), std::move(decoded.line_feeds));
  main_.format = decoded.format;
  main_.path = path;
  top_line_ = 0;
  goal_column_ = 0;
  message_.clear();
  return true;
}

bool Editor::save_to(const std::wstring& path) {
  // Never write over something that cannot be inspected.
  const FileKind kind = host_.file_kind(path);
  if (kind == FileKind::kError || kind == FileKind::kDirectory) {
    message_ = kind == FileKind::kError ? cannot_access(path) : path + L" is a directory";
    return false;
  }
  const std::string bytes = encode_utf8(main_.text, main_.format);
  std::wstring error;
  if (!host_.write_file(path, bytes, error)) {
    message_ = L"Cannot write " + path + (error.empty() ? std::wstring() : L": " + error);
    return false;
  }
  main_.path = path;
  main_.undo.mark_saved();
  message_ = L"Wrote " + path;
  return true;
}

void Editor::save_buffer() {
  if (!main_.modified()) {
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
    message_ = kRecursiveMinibuffer;
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
    load_file(after_save_path_);
  }
}

bool Editor::confirm_session_end() {
  if (!main_.modified()) return true;
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
      return false;
  }
  return false;
}

void Editor::quit() {
  if (!main_.modified()) {
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
      return;
  }
}

}  // namespace geditor
