#include "test_harness.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace test {
namespace {

TestCase* g_head = nullptr;
TestCase* g_tail = nullptr;
int g_failures_in_current = 0;
bool g_capturing = false;

}  // namespace

Registrar::Registrar(TestCase* tc) {
  // Keep registration order within a translation unit.
  if (g_tail == nullptr) {
    g_head = tc;
  } else {
    g_tail->next = tc;
  }
  g_tail = tc;
}

void report_failure(const char* file, int line, const std::string& message) {
  ++g_failures_in_current;
  if (!g_capturing) std::printf("  %s(%d): %s\n", file, line, message.c_str());
}

int count_failures(TestFn fn) {
  const int saved_failures = g_failures_in_current;
  const bool saved_capturing = g_capturing;
  g_failures_in_current = 0;
  g_capturing = true;
  fn();
  const int captured = g_failures_in_current;
  g_failures_in_current = saved_failures;
  g_capturing = saved_capturing;
  return captured;
}

int run_all(const char* filter) {
  int run = 0;
  int failed = 0;
  for (TestCase* tc = g_head; tc != nullptr; tc = tc->next) {
    if (filter != nullptr && std::strstr(tc->name, filter) == nullptr) continue;
    ++run;
    g_failures_in_current = 0;
    std::printf("[ RUN    ] %s\n", tc->name);
    tc->fn();
    if (g_failures_in_current == 0) {
      std::printf("[     OK ] %s\n", tc->name);
    } else {
      ++failed;
      std::printf("[ FAILED ] %s\n", tc->name);
    }
  }
  std::printf("%d test(s) run, %d failed\n", run, failed);
  if (run == 0) {
    std::printf("no test matched\n");
    return 1;
  }
  return failed == 0 ? 0 : 1;
}

std::string to_display(const std::string& s) {
  return "\"" + s + "\"";
}

std::string to_display(const std::wstring& s) {
  std::string utf8;
  if (!s.empty()) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0, nullptr, nullptr);
    utf8.resize(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), utf8.data(), n,
                        nullptr, nullptr);
  }
  return "L\"" + utf8 + "\"";
}

std::string to_display(const char* s) {
  return s == nullptr ? std::string("(null)") : to_display(std::string(s));
}

std::string to_display(const wchar_t* s) {
  return s == nullptr ? std::string("(null)") : to_display(std::wstring(s));
}

std::string to_display(bool v) {
  return v ? "true" : "false";
}

}  // namespace test

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  return test::run_all(argc > 1 ? argv[1] : nullptr);
}
