// geditor entry point: command line, startup measurement and the message
// loop. The window, input and drawing live in window.* / input.* /
// renderer.*.
//
// --measure-startup prints the time from process creation to the end of the
// first frame to stderr, then exits (tools/measure_startup.py).
// --measure-input prints the window handle after the first frame and then the
// time from each keyboard message to the end of its frame
// (tools/measure_input.py).
// --headless (tools and checks) keeps the window off the screen, out of the
// foreground and away from the real clipboard (window.h).

#include <windows.h>

#include "command_line.h"
#include "measure.h"
#include "window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  geditor::win::measure_begin();
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const geditor::win::CommandLine command_line =
      geditor::win::parse_command_line(GetCommandLineW());
  if (command_line.measure_startup) geditor::win::enable_stage_marks();
  geditor::win::mark_stage("dpi_and_args");

  // Heap-allocated: no object with a dynamic initializer before wWinMain.
  auto* window = new geditor::win::MainWindow();
  if (!window->create(instance, command_line.measure_startup, command_line.measure_input,
                      command_line.headless)) {
    delete window;
    return 1;
  }
  window->apply_command_line(command_line);
  window->show(show);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!window->pre_translate(msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    window->run_held_keys();
  }
  delete window;
  return static_cast<int>(msg.wParam);
}
