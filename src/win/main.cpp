// geditor entry point.
//
// M0 skeleton: opens a window, draws a fixed text with Direct2D/DirectWrite,
// and supports --measure-startup (prints the time from process creation to
// the end of the first frame to stderr, then exits).

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <cstdio>
#include <cwchar>

namespace {

constexpr wchar_t kWindowClass[] = L"geditor.main";
constexpr wchar_t kTitle[] = L"geditor";
constexpr wchar_t kSampleText[] = L"geditor - M0 skeleton";
constexpr float kFontSizePt = 12.0f;
constexpr float kMarginDip = 8.0f;

// All members are trivially constant-initialized: no dynamic initializers run
// before wWinMain.
struct App {
  HWND hwnd;
  ID2D1Factory* d2d_factory;
  IDWriteFactory* dwrite_factory;
  IDWriteTextFormat* text_format;
  ID2D1HwndRenderTarget* target;
  ID2D1SolidColorBrush* text_brush;
  bool measure_startup;
  bool first_frame_done;
  double entry_ms;  // ms from process creation to the start of wWinMain
  LARGE_INTEGER entry_counter;
};

App g_app{};

// Startup stage marks (ms since wWinMain), printed by --measure-startup.
struct StageMark {
  const char* name;
  double ms;
};
constexpr int kMaxStages = 12;
StageMark g_stages[kMaxStages]{};
int g_stage_count = 0;

double ms_since_entry();

void mark_stage(const char* name) {
  if (!g_app.measure_startup || g_stage_count >= kMaxStages) return;
  g_stages[g_stage_count++] = StageMark{name, ms_since_entry()};
}

template <typename T>
void safe_release(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

D2D1_COLOR_F rgb(UINT32 value) {
  return D2D1::ColorF(value);
}

// Returns true when `flag` appears as a whole token after the program name.
// Quoted arguments are not interpreted yet (file arguments arrive in M1).
bool has_flag(const wchar_t* command_line, const wchar_t* flag) {
  const wchar_t* p = command_line;
  // Skip the program name, which may be quoted.
  if (*p == L'"') {
    ++p;
    while (*p != L'\0' && *p != L'"') ++p;
    if (*p == L'"') ++p;
  } else {
    while (*p != L'\0' && *p != L' ' && *p != L'\t') ++p;
  }
  const size_t flag_len = wcslen(flag);
  while (*p != L'\0') {
    while (*p == L' ' || *p == L'\t') ++p;
    const wchar_t* start = p;
    while (*p != L'\0' && *p != L' ' && *p != L'\t') ++p;
    const size_t len = static_cast<size_t>(p - start);
    if (len == flag_len && wcsncmp(start, flag, len) == 0) return true;
  }
  return false;
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
  LARGE_INTEGER now{}, freq{};
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  return static_cast<double>(now.QuadPart - g_app.entry_counter.QuadPart) * 1000.0 /
         static_cast<double>(freq.QuadPart);
}

void write_stderr(const char* text) {
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (err == nullptr || err == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(err, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
}

bool create_device_independent_resources() {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_app.d2d_factory))) {
    return false;
  }
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&g_app.dwrite_factory)))) {
    return false;
  }
  const float size_dip = kFontSizePt * 96.0f / 72.0f;
  if (FAILED(g_app.dwrite_factory->CreateTextFormat(
          L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, size_dip, L"en-us", &g_app.text_format))) {
    return false;
  }
  return true;
}

bool ensure_render_target(HWND hwnd) {
  if (g_app.target != nullptr) return true;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
  // Software rendering: creating a GPU render target costs about 80ms at
  // startup on the development PC (D3D device and driver load), while the
  // software target costs about 25ms. Revisit with the M1-5 frame-time check.
  const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), dpi, dpi);
  const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                        static_cast<UINT32>(rc.bottom - rc.top)),
      D2D1_PRESENT_OPTIONS_IMMEDIATELY);
  if (FAILED(g_app.d2d_factory->CreateHwndRenderTarget(props, hwnd_props, &g_app.target))) {
    return false;
  }
  if (FAILED(g_app.target->CreateSolidColorBrush(rgb(0xd4d4d4), &g_app.text_brush))) {
    safe_release(g_app.target);
    return false;
  }
  return true;
}

void discard_render_target() {
  safe_release(g_app.text_brush);
  safe_release(g_app.target);
}

void paint(HWND hwnd) {
  PAINTSTRUCT ps;
  BeginPaint(hwnd, &ps);
  mark_stage("paint_begin");
  // S_OK when the frame reached the window; a failure code otherwise.
  HRESULT result = E_FAIL;
  if (ensure_render_target(hwnd)) {
    mark_stage("render_target");
    g_app.target->BeginDraw();
    g_app.target->Clear(rgb(0x1e1e1e));
    const D2D1_SIZE_F size = g_app.target->GetSize();
    g_app.target->DrawText(kSampleText, static_cast<UINT32>(wcslen(kSampleText)),
                           g_app.text_format,
                           D2D1::RectF(kMarginDip, kMarginDip, size.width, size.height),
                           g_app.text_brush);
    mark_stage("draw_calls");
    result = g_app.target->EndDraw();
    mark_stage("end_draw");
    if (result == D2DERR_RECREATE_TARGET) {
      // Device lost: drop the target and paint again with a new one.
      discard_render_target();
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }
  EndPaint(hwnd, &ps);

  if (g_app.measure_startup && !g_app.first_frame_done) {
    if (result == D2DERR_RECREATE_TARGET) return;  // measured on the repainted frame
    g_app.first_frame_done = true;
    if (FAILED(result)) {
      // Do not report a time for a frame that was never drawn.
      char line[64];
      std::snprintf(line, sizeof(line), "startup_error=draw_failed hr=0x%08lx\n",
                    static_cast<unsigned long>(result));
      write_stderr(line);
      PostQuitMessage(2);
    } else {
      // startup_ms: process creation -> first frame (includes the loader and
      // anything that holds the process before wWinMain, such as antivirus).
      // entry_ms: process creation -> wWinMain. app_ms: wWinMain -> first frame.
      char line[128];
      std::snprintf(line, sizeof(line), "startup_ms=%.3f entry_ms=%.3f app_ms=%.3f\n",
                    ms_since_process_creation(), g_app.entry_ms, ms_since_entry());
      write_stderr(line);
      for (int i = 0; i < g_stage_count; ++i) {
        std::snprintf(line, sizeof(line), "stage %s=%.3f\n", g_stages[i].name, g_stages[i].ms);
        write_stderr(line);
      }
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
  }
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_PAINT:
      paint(hwnd);
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Direct2D clears the whole client area.
    case WM_SIZE:
      if (g_app.target != nullptr) {
        g_app.target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      }
      return 0;
    case WM_DPICHANGED: {
      const float dpi = static_cast<float>(HIWORD(wparam));
      if (g_app.target != nullptr) g_app.target->SetDpi(dpi, dpi);
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  QueryPerformanceCounter(&g_app.entry_counter);
  g_app.entry_ms = ms_since_process_creation();
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  g_app.measure_startup = has_flag(GetCommandLineW(), L"--measure-startup");
  mark_stage("dpi_and_args");

  if (!create_device_independent_resources()) {
    MessageBoxW(nullptr, L"Failed to initialize Direct2D/DirectWrite.", kTitle, MB_ICONERROR);
    return 1;
  }
  mark_stage("factories");

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
  wc.lpszClassName = kWindowClass;
  if (RegisterClassExW(&wc) == 0) return 1;

  g_app.hwnd = CreateWindowExW(0, kWindowClass, kTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr,
                               instance, nullptr);
  if (g_app.hwnd == nullptr) return 1;
  mark_stage("create_window");

  ShowWindow(g_app.hwnd, show);
  mark_stage("show_window");
  UpdateWindow(g_app.hwnd);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  discard_render_target();
  safe_release(g_app.text_format);
  safe_release(g_app.dwrite_factory);
  safe_release(g_app.d2d_factory);
  return static_cast<int>(msg.wParam);
}
