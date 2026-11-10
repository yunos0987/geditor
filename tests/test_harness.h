// Minimal self-registering test harness (no external dependency).
//
//   TEST(name) { CHECK(expr); CHECK_EQ(actual, expected); }
//
// The runner (test_main.cpp) executes every registered test, or only those
// whose name contains the substring given as the first command-line argument.
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

namespace test {

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
  TestCase* next;
};

struct Registrar {
  explicit Registrar(TestCase* tc);
};

void report_failure(const char* file, int line, const std::string& message);

// Runs `fn` with failure reporting captured; returns the number of failed
// checks inside it. Used to test the harness itself.
int count_failures(TestFn fn);

int run_all(const char* filter);

std::string to_display(const std::string& s);
std::string to_display(const std::wstring& s);
std::string to_display(const char* s);
std::string to_display(const wchar_t* s);
std::string to_display(bool v);

template <typename T>
std::string to_display(const T& v) {
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<std::underlying_type_t<T>>(v));
  } else if constexpr (std::is_arithmetic_v<T>) {
    return std::to_string(v);
  } else {
    static_assert(std::is_arithmetic_v<T>, "add a to_display overload for this type");
    return {};
  }
}

}  // namespace test

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST(name)                                                               \
  static void name();                                                            \
  static ::test::TestCase TEST_CONCAT(name, _case){#name, &name, nullptr};       \
  static ::test::Registrar TEST_CONCAT(name, _registrar){&TEST_CONCAT(name, _case)}; \
  static void name()

#define CHECK(expr)                                                     \
  do {                                                                  \
    if (!(expr)) ::test::report_failure(__FILE__, __LINE__, "CHECK(" #expr ")"); \
  } while (0)

#define CHECK_EQ(actual, expected)                                                  \
  do {                                                                              \
    const auto& test_actual_ = (actual);                                            \
    const auto& test_expected_ = (expected);                                        \
    if (!(test_actual_ == test_expected_)) {                                        \
      ::test::report_failure(__FILE__, __LINE__,                                    \
                             "CHECK_EQ(" #actual ", " #expected ")\n    actual:   " + \
                                 ::test::to_display(test_actual_) +                 \
                                 "\n    expected: " + ::test::to_display(test_expected_)); \
    }                                                                               \
  } while (0)
