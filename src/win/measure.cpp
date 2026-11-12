#include "measure.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace geditor::win {
namespace {

struct StageMark {
  const char* name;
  double ms;
};
constexpr int kMaxStages = 16;

// All members are trivially constant-initialized: no dynamic initializers run
// before wWinMain.
struct MeasureState {
  bool stages_enabled;
  double entry_ms;
  LARGE_INTEGER entry_counter;
  LARGE_INTEGER frequency;
  StageMark stages[kMaxStages];
  int stage_count;
};

MeasureState g_measure{};

}  // namespace

void measure_begin() {
  QueryPerformanceCounter(&g_measure.entry_counter);
  QueryPerformanceFrequency(&g_measure.frequency);
  g_measure.entry_ms = ms_since_process_creation();
}

void enable_stage_marks() {
  g_measure.stages_enabled = true;
}

double ms_since_process_creation() {
  FILETIME creation{}, exit_time{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user)) {
    return -1.0;
  }
  FILETIME now{};
  GetSystemTimePreciseAsFileTime(&now);
  ULARGE_INTEGER a{}, b{};
  a.LowPart = creation.dwLowDateTime;
  a.HighPart = creation.dwHighDateTime;
  b.LowPart = now.dwLowDateTime;
  b.HighPart = now.dwHighDateTime;
  return static_cast<double>(b.QuadPart - a.QuadPart) / 10000.0;  // 100ns -> ms
}

double ms_since_entry() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart - g_measure.entry_counter.QuadPart) * 1000.0 /
         static_cast<double>(g_measure.frequency.QuadPart);
}

double entry_ms() {
  return g_measure.entry_ms;
}

void mark_stage(const char* name) {
  if (!g_measure.stages_enabled || g_measure.stage_count >= kMaxStages) return;
  g_measure.stages[g_measure.stage_count++] = StageMark{name, ms_since_entry()};
}

void write_stderr(const char* text) {
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (err == nullptr || err == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(err, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

void report_startup() {
  // startup_ms: process creation -> first frame (includes the loader and
  // anything that holds the process before wWinMain, such as antivirus).
  // entry_ms: process creation -> wWinMain. app_ms: wWinMain -> first frame.
  char line[128];
  std::snprintf(line, sizeof(line), "startup_ms=%.3f entry_ms=%.3f app_ms=%.3f\n",
                ms_since_process_creation(), g_measure.entry_ms, ms_since_entry());
  write_stderr(line);
  for (int i = 0; i < g_measure.stage_count; ++i) {
    std::snprintf(line, sizeof(line), "stage %s=%.3f\n", g_measure.stages[i].name,
                  g_measure.stages[i].ms);
    write_stderr(line);
  }
}

}  // namespace geditor::win
