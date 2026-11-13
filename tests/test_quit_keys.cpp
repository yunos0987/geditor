// [C16]: C-g typed in the middle of a key sequence (after ESC, prefix keys or
// a prefix argument) quits, as in Emacs; a real Ctrl+Alt+G key (C-M-g in one
// key event) still runs the C-M-g binding.

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
  s.keys(L"C-M-g");  // one key event with both modifiers
  CHECK_EQ(s.message(), std::wstring(L"goto-line is not implemented yet"));
  s.settings("{\"keys\": {\"C-M-g\": \"end-of-line\"}}");
  s.keys(L"C-M-g");
  CHECK_EQ(s.point(), size_t{10});
}
