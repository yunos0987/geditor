// [C16]: C-g typed in the middle of a key sequence (after ESC, prefix keys or
// a prefix argument) quits, as in Emacs; a real Ctrl+Alt+G key (C-M-g in one
// key event) is the key C-M-g: undefined by default, or its settings binding.

#include <string>

#include "fake_host.h"

namespace {

// Checks that nothing is pending and the next C-f moves exactly one character.
void check_nothing_pending(test::Session& s) {
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
  const size_t before = s.point();
  s.keys(L"C-f");
  CHECK_EQ(s.point(), before + 1);
}

}  // namespace

TEST(quit_esc_c_g) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"ESC C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.point(), size_t{0});
  check_nothing_pending(s);
}

TEST(quit_c_x_c_g) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"C-x C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  check_nothing_pending(s);
}

TEST(quit_c_u_c_g) {
  test::Session s;
  s.load(L"0123456789");
  // C-u is backward-kill-line by default since 2026-11-17: universal-argument
  // bound to it as before.
  s.settings("{\"keys\": {\"C-u\": \"universal-argument\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-u 3 C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  check_nothing_pending(s);
  s.keys(L"C-u C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  check_nothing_pending(s);
}

TEST(quit_m_g_c_g) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"M-g C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  check_nothing_pending(s);
}

TEST(quit_esc_c_g_after_prefix_argument) {
  test::Session s;
  s.load(L"0123456789");
  s.settings("{\"keys\": {\"C-u\": \"universal-argument\"}}");  // as quit_c_u_c_g
  s.keys(L"C-u 3 ESC C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  check_nothing_pending(s);
  s.keys(L"M-2 ESC C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  check_nothing_pending(s);
}

TEST(quit_esc_c_g_whatever_settings_bind_to_c_m_g) {
  test::Session s;
  s.load(L"0123456789");
  s.settings("{\"keys\": {\"C-M-g\": \"end-of-line\", \"C-u\": \"end-of-line\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"ESC C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  CHECK_EQ(s.point(), size_t{0});
  check_nothing_pending(s);
}

TEST(quit_real_ctrl_alt_g_runs_the_binding) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"C-M-g");  // one key event with both modifiers: not a quit
  CHECK_EQ(s.message(), std::wstring(L"C-M-g is undefined"));
  s.settings("{\"keys\": {\"C-M-g\": \"end-of-line\"}}");
  s.keys(L"C-M-g");
  CHECK_EQ(s.point(), size_t{10});
}
