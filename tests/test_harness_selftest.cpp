// Tests of the harness itself: a broken harness would let every test pass.

#include "test_harness.h"

#include <string>

namespace {

void failing_check() {
  CHECK(1 + 1 == 3);
}

void failing_check_eq() {
  CHECK_EQ(2, 3);
}

void failing_string_check_eq() {
  CHECK_EQ(std::wstring(L"a"), std::wstring(L"b"));
}

void two_failures() {
  CHECK(false);
  CHECK_EQ(1, 0);
}

void passing_checks() {
  CHECK(true);
  CHECK_EQ(7, 7);
  CHECK_EQ(std::string("x"), std::string("x"));
}

}  // namespace

TEST(harness_counts_failing_check) {
  CHECK_EQ(test::count_failures(&failing_check), 1);
}

TEST(harness_counts_failing_check_eq) {
  CHECK_EQ(test::count_failures(&failing_check_eq), 1);
  CHECK_EQ(test::count_failures(&failing_string_check_eq), 1);
}

TEST(harness_counts_every_failure) {
  CHECK_EQ(test::count_failures(&two_failures), 2);
}

TEST(harness_passing_checks_report_nothing) {
  CHECK_EQ(test::count_failures(&passing_checks), 0);
}

TEST(harness_display_of_values) {
  CHECK_EQ(test::to_display(42), std::string("42"));
  CHECK_EQ(test::to_display(true), std::string("true"));
  CHECK_EQ(test::to_display(std::wstring(L"あ")), std::string("L\"\xE3\x81\x82\""));
}
