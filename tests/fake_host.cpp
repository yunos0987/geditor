#include "fake_host.h"

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
  files[key_of(path)] = e;
  names[key_of(path)] = path;
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

bool FakeHost::ask_yes_no(const std::wstring&) {
  ++yes_no_dialogs;
  if (yes_no_answers.empty()) return false;
  const bool c = yes_no_answers.front();
  yes_no_answers.pop_front();
  return c;
}

bool FakeHost::launch_new_window(const std::wstring& path, std::wstring& error) {
  if (fail_launch) {
    error = L"launch failed";
    return false;
  }
  launched.push_back(path);
  return true;
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
