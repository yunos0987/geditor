// Timing for --measure-startup and --measure-input (stderr output read by
// tools/measure_startup.py and tools/measure_input.py).
#pragma once

namespace geditor::win {

// Call first in wWinMain.
void measure_begin();
// Makes mark_stage record (only --measure-startup prints the marks).
void enable_stage_marks();

double ms_since_process_creation();
double ms_since_entry();
// ms from process creation to the start of wWinMain.
double entry_ms();

// Startup stage marks (ms since wWinMain), printed by report_startup.
void mark_stage(const char* name);

void write_stderr(const char* text);
// "startup_ms=... entry_ms=... app_ms=..." and the stage marks.
void report_startup();

}  // namespace geditor::win
