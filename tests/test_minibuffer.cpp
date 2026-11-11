// Minibuffer: file name completion, history, abort and editing keys.

#include "fake_host.h"

using test::Session;

namespace {

void add_sample_files(test::FakeHost& host) {
  host.add_file(L"C:\\work\\alpha.txt", "a");
  host.add_file(L"C:\\work\\report1.txt", "1");
  host.add_file(L"C:\\work\\report2.txt", "2");
  host.add_file(L"C:\\work\\README.md", "r");
  host.add_dir(L"C:\\work\\src");
  host.add_file(L"C:\\work\\src\\main.cpp", "m");
}

// Starts find-file and replaces the initial text with `input`.
void prompt_with(Session& s, const wchar_t* input) {
  s.keys(L"C-x C-f");
  s.keys(L"C-a C-k");
  s.type(input);
}

}  // namespace

TEST(minibuffer_completes_unique_prefix) {
  Session s;
  add_sample_files(s.host);
  prompt_with(s, L"C:\\work\\al");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\alpha.txt"));
  s.keys(L"TAB");
  CHECK_EQ(s.message(), std::wstring(L"[Sole completion]"));
}

TEST(minibuffer_completes_common_prefix_of_several) {
  Session s;
  add_sample_files(s.host);
  prompt_with(s, L"C:\\work\\rep");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\report"));
}

TEST(minibuffer_lists_candidates_when_tab_cannot_extend) {
  Session s;
  add_sample_files(s.host);
  prompt_with(s, L"C:\\work\\report");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\report"));
  CHECK_EQ(s.message(), std::wstring(L"{report1.txt | report2.txt}"));
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"Find file: C:\\work\\report {report1.txt | report2.txt}"));
}

TEST(minibuffer_completes_directory_with_trailing_backslash) {
  Session s;
  add_sample_files(s.host);
  prompt_with(s, L"C:\\work\\sr");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\src\\"));
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\src\\main.cpp"));
}

TEST(minibuffer_completion_is_case_insensitive) {
  Session s;
  add_sample_files(s.host);
  prompt_with(s, L"C:\\work\\ALP");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\alpha.txt"));
  s.keys(L"C-a C-k");
  s.type(L"C:\\work\\rea");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\README.md"));
}

TEST(minibuffer_completion_without_match) {
  Session s;
  add_sample_files(s.host);
  prompt_with(s, L"C:\\work\\zzz");
  s.keys(L"TAB");
  CHECK_EQ(s.message(), std::wstring(L"[No match]"));
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\zzz"));
}

TEST(minibuffer_history_m_p_and_m_n) {
  Session s;
  add_sample_files(s.host);
  s.host.open_answers.push_back(geditor::OpenChoice::kCancel);
  prompt_with(s, L"C:\\work\\alpha.txt");
  s.keys(L"RET");
  prompt_with(s, L"C:\\work\\report1.txt");
  s.keys(L"RET");  // replaces nothing: dialog cancelled by default
  s.keys(L"C-x C-f");
  s.type(L"typed");
  s.keys(L"M-p");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\report1.txt"));
  s.keys(L"M-p");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\alpha.txt"));
  s.keys(L"M-p");
  CHECK_EQ(s.message(), std::wstring(L"Beginning of history; no preceding item"));
  s.keys(L"M-n");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\report1.txt"));
  s.keys(L"M-n");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\typed"));
  s.keys(L"M-n");
  CHECK_EQ(s.message(), std::wstring(L"End of history; no default available"));
}

TEST(minibuffer_c_g_aborts) {
  Session s;
  s.load(L"keep");
  s.keys(L"C-x C-f");
  CHECK(s.ed.minibuffer_active());
  s.type(L"abc");
  s.keys(L"C-g");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.text(), std::wstring(L"keep"));
}

TEST(minibuffer_editing_keys) {
  Session s;
  s.load(L"main buffer");
  s.keys(L"C-x C-f");
  s.keys(L"C-a C-k");
  s.type(L"abcdef");
  const geditor::Buffer& mb = s.ed.minibuffer_buffer();
  s.keys(L"C-a");
  CHECK_EQ(mb.point, size_t{0});
  s.keys(L"C-f C-f");
  CHECK_EQ(mb.point, size_t{2});
  s.keys(L"C-b");
  CHECK_EQ(mb.point, size_t{1});
  s.keys(L"C-e");
  CHECK_EQ(mb.point, size_t{6});
  s.keys(L"DEL");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"abcde"));
  s.keys(L"C-a C-f C-f C-k");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"ab"));
  s.keys(L"C-d");  // at the end
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"ab"));
  CHECK_EQ(s.text(), std::wstring(L"main buffer"));  // main buffer untouched
  CHECK_EQ(s.point(), size_t{0});
}
