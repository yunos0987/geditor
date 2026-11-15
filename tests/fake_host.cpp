#include "fake_host.h"

#include <algorithm>
#include <cstdio>

#include "key.h"
#include "path.h"
#include "unicode.h"

namespace test {
namespace {

std::wstring key_of(const std::wstring& path) {
  std::wstring k = path;
  while (k.size() > 3 && k.back() == L'\\') k.pop_back();
  for (wchar_t& c : k) c = static_cast<wchar_t>(geditor::to_lower(c));
  return k;
}

}  // namespace

std::string utf8(const std::wstring& s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    size_t units = 0;
    const char32_t c = geditor::decode_at(s, i, &units);
    i += units;
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (c >> 18)));
      out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

FakeHost::FakeHost() {
  add_dir(L"C:\\");
  add_dir(L"C:\\work");
}

void FakeHost::add_dir(const std::wstring& path) {
  Entry e;
  e.is_directory = true;
  files[key_of(path)] = e;
  names[key_of(path)] = path;
}

void FakeHost::add_file(const std::wstring& path, const std::string& bytes) {
  Entry e;
  e.bytes = bytes;
  e.write_time = ++clock;
  const auto old = files.find(key_of(path));
  if (old != files.end()) e.read_only = old->second.read_only;
  files[key_of(path)] = e;
  names[key_of(path)] = path;
}

unsigned long long FakeHost::write_time(const std::wstring& path) const {
  const auto it = files.find(key_of(path));
  return it == files.end() ? 0 : it->second.write_time;
}

void FakeHost::touch_file(const std::wstring& path, const std::string& bytes) {
  add_file(path, bytes);
}

void FakeHost::set_read_only(const std::wstring& path, bool read_only) {
  const auto it = files.find(key_of(path));
  if (it != files.end()) it->second.read_only = read_only;
}

void FakeHost::remove_file(const std::wstring& path) {
  files.erase(key_of(path));
  names.erase(key_of(path));
}

bool FakeHost::has_file(const std::wstring& path) const {
  const auto it = files.find(key_of(path));
  return it != files.end() && !it->second.is_directory;
}

std::string FakeHost::file(const std::wstring& path) const {
  const auto it = files.find(key_of(path));
  return it == files.end() ? std::string() : it->second.bytes;
}

bool FakeHost::read_file(const std::wstring& path, std::string& bytes, std::wstring& error) {
  const auto it = files.find(key_of(path));
  if (unreadable_paths.count(key_of(path)) != 0) {
    error = L"Cannot read " + path + L": Access is denied";
    return false;
  }
  if (it == files.end() || it->second.is_directory) {
    error = L"Cannot read " + path;
    return false;
  }
  bytes = it->second.bytes;
  return true;
}

bool FakeHost::write_file(const std::wstring& path, std::string_view bytes, std::wstring& error) {
  if (fail_writes) {
    error = L"disk full";
    return false;
  }
  const auto parent = files.find(key_of(geditor::directory_of(path)));
  if (parent == files.end() || !parent->second.is_directory) {
    error = L"the folder does not exist";
    return false;
  }
  const auto existing = files.find(key_of(path));
  if (existing != files.end() && existing->second.is_directory) {
    error = L"is a folder";
    return false;
  }
  add_file(path, std::string(bytes));
  written.push_back(path);
  return true;
}

bool FakeHost::save_file(const std::wstring& path, std::string_view bytes, bool backup, std::wstring& error) {
  saves.emplace_back(path, backup);
  const auto existing = files.find(key_of(path));
  const bool exists = existing != files.end() && !existing->second.is_directory;
  if (exists && existing->second.read_only) {
    error = L"Access is denied";
    return false;
  }
  // The steps of PLAN 4.8 in order; a failing step leaves everything as it
  // was (the real host removes its temporary file).
  const SaveStep fail = fail_step;
  fail_step = SaveStep::kNone;
  if (fail == SaveStep::kTempWrite) {
    error = L"writing the temporary file: The disk is full";
    return false;
  }
  if (fail == SaveStep::kFlush) {
    error = L"flushing the temporary file: The disk is full";
    return false;
  }
  if (exists && fail == SaveStep::kReplace) {
    error = L"replacing the file: Access is denied";
    return false;
  }
  if (!exists && fail == SaveStep::kMove) {
    error = L"moving the temporary file: Access is denied";
    return false;
  }
  if (exists && backup && fail == SaveStep::kBackup) {
    error = L"making the backup " + path + L"~: Access is denied";
    return false;
  }
  const std::string old = exists ? existing->second.bytes : std::string();
  if (!write_file(path, bytes, error)) return false;
  if (exists && backup) add_file(path + L"~", old);
  return true;
}

bool FakeHost::file_info(const std::wstring& path, geditor::FileInfo& info, std::wstring& error) {
  info = geditor::FileInfo();
  if (info_error_paths.count(key_of(path)) != 0) {
    error = L"Access is denied";
    return false;
  }
  const auto it = files.find(key_of(path));
  if (it == files.end() || it->second.is_directory) return true;
  info.exists = true;
  info.size = it->second.bytes.size();
  info.write_time = it->second.write_time;
  info.read_only = it->second.read_only;
  return true;
}

void FakeHost::add_error_path(const std::wstring& path) {
  error_paths.insert(key_of(path));
}

geditor::FileKind FakeHost::file_kind(const std::wstring& path) {
  if (error_paths.count(key_of(path)) != 0) return geditor::FileKind::kError;
  const auto it = files.find(key_of(path));
  if (it == files.end()) return geditor::FileKind::kNone;
  return it->second.is_directory ? geditor::FileKind::kDirectory : geditor::FileKind::kFile;
}

bool FakeHost::list_directory(const std::wstring& dir, std::vector<geditor::DirEntry>& entries) {
  const std::wstring d = key_of(dir);
  const auto it = files.find(d);
  if (it == files.end() || !it->second.is_directory) return false;
  const std::wstring prefix = d.back() == L'\\' ? d : d + L'\\';
  for (const auto& [key, entry] : files) {
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) continue;
    const std::wstring rest = key.substr(prefix.size());
    if (rest.find(L'\\') != std::wstring::npos) continue;
    geditor::DirEntry e;
    e.name = geditor::file_name_of(names[key]);
    e.is_directory = entry.is_directory;
    entries.push_back(e);
  }
  return true;
}

geditor::ClipboardRead FakeHost::get_clipboard(std::wstring& text) {
  if (fail_clipboard_read) return geditor::ClipboardRead::kFailed;
  if (!clipboard_has_text) return geditor::ClipboardRead::kNoText;
  text = clipboard;
  return geditor::ClipboardRead::kOk;
}

bool FakeHost::set_clipboard(const std::wstring& text) {
  if (fail_clipboard) return false;
  clipboard = text;
  clipboard_has_text = true;
  return true;
}

geditor::OpenChoice FakeHost::ask_open_choice(const std::wstring&) {
  ++open_dialogs;
  if (open_answers.empty()) return geditor::OpenChoice::kCancel;
  const geditor::OpenChoice c = open_answers.front();
  open_answers.pop_front();
  return c;
}

geditor::SaveChoice FakeHost::ask_save_changes(const std::wstring&) {
  ++save_dialogs;
  if (save_answers.empty()) return geditor::SaveChoice::kCancel;
  const geditor::SaveChoice c = save_answers.front();
  save_answers.pop_front();
  return c;
}

bool FakeHost::ask_yes_no(const std::wstring& question) {
  ++yes_no_dialogs;
  yes_no_questions.push_back(question);
  if (yes_no_answers.empty()) return false;
  const bool c = yes_no_answers.front();
  yes_no_answers.pop_front();
  return c;
}

bool FakeHost::launch_new_window(const std::wstring& path, const std::wstring& encoding, std::wstring& error) {
  if (fail_launch) {
    error = L"launch failed";
    return false;
  }
  launched.push_back(path);
  launched_encodings.push_back(encoding);
  return true;
}

int FakeHost::visible_lines() {
  if (echo_editor == nullptr) return lines;
  return std::max(1, lines - (echo_editor->echo_line_count() - 1));
}

void Session::settings(const std::string& text) {
  host.add_file(kSettingsPath, text);
  ed.set_settings_path(kSettingsPath);
  ed.reload_settings();
}

void Session::keys(const wchar_t* notation) {
  const std::optional<geditor::KeySequence> seq = geditor::parse_key_sequence(notation);
  CHECK(seq.has_value());
  if (!seq) {
    std::printf("  bad key notation in test: %s\n", to_display(notation).c_str());
    return;
  }
  for (const geditor::Key& k : *seq) ed.handle_key(k);
}

void Session::type(std::wstring_view text) {
  size_t i = 0;
  while (i < text.size()) {
    size_t units = 0;
    const char32_t c = geditor::decode_at(text, i, &units);
    ed.handle_key(geditor::Key{c, 0});
    i += units;
  }
}

void Session::load(const std::wstring& content, const std::wstring& path) {
  host.add_file(path, utf8(content));
  ed.request_open(path);
  CHECK(ed.buffer().path == path);
  CHECK(ed.buffer().text.text() == content);
}

}  // namespace test
