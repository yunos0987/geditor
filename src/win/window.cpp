#include "window.h"

#include <imm.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "clipboard.h"
#include "dialogs.h"
#include "echo_text.h"
#include "file_io.h"
#include "ime.h"
#include "measure.h"
#include "process.h"
#include "unicode.h"

namespace geditor::win {
namespace {

constexpr wchar_t kWindowClass[] = L"geditor.main";
constexpr wchar_t kAppName[] = L"geditor";
constexpr wchar_t kFontFamily[] = L"Consolas";
constexpr float kFontSizePt = 12.0f;
constexpr UINT kMsgDestroy = WM_APP + 1;     // Host::quit, after the current call
// From the settings watcher: wParam 0 = the file changed; 1 = watching
// stopped (lParam: new std::wstring with the reason).
constexpr UINT kMsgSettingsChanged = WM_APP + 2;
constexpr UINT_PTR kPendingKeysTimer = 1;   // echo prefix keys after 1 second
constexpr UINT kPendingKeysDelayMs = 1000;
constexpr DWORD kToUnicodeNoStateChange = 0x4;  // Windows 10 1607 and later
constexpr ULONG_PTR kCopyDataCapture = 0x50414347;  // 'GCAP': WM_COPYDATA screenshot

// LayoutCharFn for the current keyboard layout: the character of `vk` with
// only Shift down, without touching the dead-key state.
char32_t layout_char(void*, unsigned vk, unsigned scan_code, bool shift) {
  BYTE state[256] = {};
  if (shift) {
    state[VK_SHIFT] = 0x80;
    state[VK_LSHIFT] = 0x80;
  }
  wchar_t buffer[4] = {};
  const int n = ToUnicodeEx(vk, scan_code, state, buffer, 4, kToUnicodeNoStateChange,
                            GetKeyboardLayout(0));
  if (n == 1 && !is_high_surrogate(buffer[0]) && !is_low_surrogate(buffer[0])) return buffer[0];
  if (n == 2 && is_high_surrogate(buffer[0]) && is_low_surrogate(buffer[1])) {
    return combine_surrogates(buffer[0], buffer[1]);
  }
  return 0;
}

std::wstring environment_variable(const wchar_t* name) {
  const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
  if (n == 0) return std::wstring();
  std::wstring value(n, L'\0');
  const DWORD written = GetEnvironmentVariableW(name, value.data(), n);
  value.resize(written < n ? written : 0);
  return value;
}

double ms_between(const LARGE_INTEGER& from, const LARGE_INTEGER& to) {
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  return static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 /
         static_cast<double>(frequency.QuadPart);
}

}  // namespace

MainWindow::MainWindow() : editor_(*this) {}

MainWindow::~MainWindow() = default;

bool MainWindow::create(HINSTANCE instance, bool measure_startup, bool measure_input) {
  measure_startup_ = measure_startup;
  measure_input_ = measure_input;
  if (!renderer_.init(kFontFamily, kFontSizePt)) {
    MessageBoxW(nullptr, L"Failed to initialize Direct2D/DirectWrite.", kAppName, MB_ICONERROR);
    return false;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
  wc.lpszClassName = kWindowClass;
  if (RegisterClassExW(&wc) == 0) return false;

  // WS_EX_ACCEPTFILES instead of DragAcceptFiles: shell32 stays unloaded
  // until a file is dropped.
  hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr,
                          nullptr, instance, this);
  if (hwnd_ == nullptr) return false;
  mark_stage("create_window");
  update_title();
  return true;
}

void MainWindow::apply_command_line(const CommandLine& command_line) {
  // The settings path is known before the file opens: a missing settings
  // file opened from the command line gets its template.
  const std::wstring settings_path = settings_path_for(
      command_line, win::current_directory(), environment_variable(L"USERPROFILE"));
  if (!command_line.settings.empty()) settings_argument_ = settings_path;
  editor_.set_settings_path(settings_path);
  if (!command_line.file.empty()) {
    LARGE_INTEGER start{}, end{};
    QueryPerformanceCounter(&start);
    editor_.request_open(command_line.file);
    QueryPerformanceCounter(&end);
    open_ms_ = ms_between(start, end);
  }
  if (command_line.line > 0) {
    const PieceTree& text = editor_.buffer().text;
    const size_t line = std::min(command_line.line - 1, text.line_count() - 1);
    editor_.set_point(text.line_start(line));
  }
  // Read synchronously before the first frame (PLAN 4.1); errors replace the
  // message of opening the file. The state is taken before the read: a
  // change made before the watcher starts is caught by comparing it then.
  if (!settings_path.empty()) settings_state_ = file_state(settings_path);
  editor_.reload_settings();
  mark_stage("settings");
  if (!command_line.errors.empty()) {
    std::wstring message;
    for (const std::wstring& e : command_line.errors) {
      if (!message.empty()) message += L"; ";
      message += e;
    }
    if (!editor_.message().empty()) message = editor_.message() + L"; " + message;
    editor_.set_message(std::move(message));
  }
  update_title();
}

void MainWindow::show(int show_command) {
  ShowWindow(hwnd_, show_command);
  mark_stage("show_window");
  UpdateWindow(hwnd_);
}

bool MainWindow::pre_translate(const MSG& msg) {
  if (msg.hwnd != hwnd_ || (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)) {
    return false;
  }
  ModifierState mods;
  mods.ctrl = GetKeyState(VK_CONTROL) < 0;
  mods.alt = GetKeyState(VK_MENU) < 0;
  mods.shift = GetKeyState(VK_SHIFT) < 0;
  const unsigned scan_code = static_cast<unsigned>((msg.lParam >> 16) & 0x1FF);
  const std::optional<Key> key =
      translate_key_down(static_cast<unsigned>(msg.wParam), scan_code, mods, &layout_char, nullptr);
  if (!key) return false;
  if (passes_to_system_when_unbound(*key) && !editor_.key_is_bound(*key)) return false;
  start_input_measure();
  chars_.reset();
  handle_key(*key);
  return true;
}

// ---- Host ----

bool MainWindow::read_file(const std::wstring& path, std::string& bytes, std::wstring& error) {
  return win::read_file(path, bytes, error);
}

bool MainWindow::write_file(const std::wstring& path, std::string_view bytes,
                            std::wstring& error) {
  return win::write_file(path, bytes, error);
}

FileKind MainWindow::file_kind(const std::wstring& path) {
  return win::file_kind(path);
}

bool MainWindow::list_directory(const std::wstring& dir, std::vector<DirEntry>& entries) {
  return win::list_directory(dir, entries);
}

std::wstring MainWindow::current_directory() {
  return win::current_directory();
}

ClipboardRead MainWindow::get_clipboard(std::wstring& text) {
  return read_clipboard(hwnd_, text);
}

bool MainWindow::set_clipboard(const std::wstring& text) {
  return write_clipboard(hwnd_, text);
}

OpenChoice MainWindow::ask_open_choice(const std::wstring& path) {
  return win::ask_open_choice(hwnd_, path);
}

SaveChoice MainWindow::ask_save_changes(const std::wstring& buffer_name) {
  return win::ask_save_changes(hwnd_, buffer_name);
}

bool MainWindow::ask_yes_no(const std::wstring& question) {
  return win::ask_yes_no(hwnd_, question);
}

bool MainWindow::launch_new_window(const std::wstring& path, std::wstring& error) {
  return launch_editor(own_executable_path(), path, settings_argument_, error);
}

void MainWindow::quit() {
  PostMessageW(hwnd_, kMsgDestroy, 0, 0);
}

int MainWindow::visible_lines() {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  // A minimised window has an empty client area: keep the rows of the last
  // real size so that the core does not scroll the view to one row.
  if (IsIconic(hwnd_) || rc.bottom <= rc.top || rc.right <= rc.left) {
    if (last_rows_ > 0) return last_rows_;
  }
  const float scale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
  // An expanded echo area (settings errors) leaves fewer text rows.
  const float height = static_cast<float>(rc.bottom - rc.top) / scale;
  const int rows = renderer_.text_rows(height, renderer_.echo_rows(height, editor_));
  if (!IsIconic(hwnd_) && rc.bottom > rc.top && rc.right > rc.left) last_rows_ = rows;
  return rows;
}

// ---- Messages ----

LRESULT CALLBACK MainWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* self = static_cast<MainWindow*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, msg, wparam, lparam);
  return self->handle_message(msg, wparam, lparam);
}

LRESULT MainWindow::handle_message(UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_PAINT:
      paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Direct2D clears the whole client area.
    case WM_PRINTCLIENT:
      renderer_.paint_to_dc(hwnd_, reinterpret_cast<HDC>(wparam), editor_, view_);
      return 0;
    case WM_COPYDATA: {
      // Screenshot request of tools/app_driver.py: the UTF-16 path of a BMP
      // file to write the current frame to. Works on a locked desktop, where
      // the screen and PrintWindow show nothing of the window.
      const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
      if (data != nullptr && data->dwData == kCopyDataCapture && data->lpData != nullptr &&
          data->cbData % sizeof(wchar_t) == 0) {
        const std::wstring path(static_cast<const wchar_t*>(data->lpData),
                                data->cbData / sizeof(wchar_t));
        return capture_to_bmp(path) ? 1 : 0;
      }
      break;
    }
    case WM_SIZE:
      // Minimising (SIZE_MINIMIZED, 0 x 0 client) must not move the view.
      if (wparam == SIZE_MINIMIZED || LOWORD(lparam) == 0 || HIWORD(lparam) == 0) return 0;
      renderer_.resize(LOWORD(lparam), HIWORD(lparam));
      keep_point_visible();
      return 0;
    case WM_DPICHANGED: {
      const float dpi = static_cast<float>(HIWORD(wparam));
      renderer_.set_dpi(dpi);
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_CHAR:
    case WM_IME_CHAR:
      // WM_IME_CHAR comes from IMEs that send characters one by one; the
      // committed string of a composition arrives in WM_IME_COMPOSITION.
      start_input_measure();
      handle_char(static_cast<wchar_t>(wparam));
      return 0;
    case WM_SYSCHAR:
      // Alt+Space opens the system menu; other Alt characters are dropped
      // (the handled ones never get here), so no beep and no menu.
      if (wparam == L' ') break;
      return 0;
    case WM_SYSCOMMAND:
      // Alt alone: SC_KEYMENU with lParam 0 would enter the (absent) menu bar.
      if ((wparam & 0xFFF0) == SC_KEYMENU && lparam == 0) return 0;
      break;
    case WM_IME_STARTCOMPOSITION:
      place_ime();
      break;
    case WM_IME_COMPOSITION:
      handle_ime_composition(lparam);
      lparam &= ~static_cast<LPARAM>(GCS_RESULTSTR | GCS_RESULTREADSTR | GCS_RESULTCLAUSE |
                                     GCS_RESULTREADCLAUSE);
      if (lparam == 0) return 0;
      break;  // the IME draws the remaining composition string
    case WM_TIMER:
      if (wparam == kPendingKeysTimer) {
        KillTimer(hwnd_, kPendingKeysTimer);
        view_.show_pending_keys = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      break;
    case WM_DROPFILES:
      handle_drop(reinterpret_cast<HDROP>(wparam));
      return 0;
    case WM_CLOSE:
      if (measure_startup_ || measure_input_) {
        DestroyWindow(hwnd_);
        return 0;
      }
      // Asks to save a modified buffer; Host::quit closes the window.
      editor_.execute_command("quit");
      after_input();
      return 0;
    case WM_QUERYENDSESSION: {
      // Log off / shutdown: same save question as quit. Cancel, a failed
      // save or an untitled buffer waiting for a file name block the end.
      const bool may_end = editor_.confirm_session_end();
      after_input();
      if (may_end) {
        ShutdownBlockReasonDestroy(hwnd_);
        return TRUE;
      }
      const std::wstring reason = L"Unsaved changes in " + editor_.buffer_name();
      ShutdownBlockReasonCreate(hwnd_, reason.c_str());
      return FALSE;
    }
    case WM_ENDSESSION:
      if (wparam) {
        DestroyWindow(hwnd_);  // the session ends; the buffer was handled above
      } else {
        ShutdownBlockReasonDestroy(hwnd_);
      }
      return 0;
    case kMsgSettingsChanged:
      if (wparam == 1) {
        std::unique_ptr<std::wstring> reason(reinterpret_cast<std::wstring*>(lparam));
        append_message(L"Stopped watching the settings file: " + (reason ? *reason : std::wstring()));
      } else {
        editor_.reload_settings();
      }
      after_input();
      return 0;
    case kMsgDestroy:
      DestroyWindow(hwnd_);
      return 0;
    case WM_DESTROY:
      settings_watcher_.stop();
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

void MainWindow::paint() {
  PAINTSTRUCT ps;
  BeginPaint(hwnd_, &ps);
  mark_stage("paint_begin");
  const HRESULT result = renderer_.paint(hwnd_, editor_, view_);
  EndPaint(hwnd_, &ps);
  if (result == D2DERR_RECREATE_TARGET) {
    // Device lost: drop the target and paint again with a new one (measured
    // on the repainted frame).
    renderer_.discard_target();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (!first_frame_done_) {
    first_frame_done_ = true;
    if (measure_startup_) {
      if (FAILED(result)) {
        // Do not report a time for a frame that was never drawn.
        char line[64];
        std::snprintf(line, sizeof(line), "startup_error=draw_failed hr=0x%08lx\n",
                      static_cast<unsigned long>(result));
        write_stderr(line);
        PostQuitMessage(2);
      } else {
        report_startup();
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      }
      return;
    }
    if (measure_input_) {
      char line[96];
      std::snprintf(line, sizeof(line), "hwnd=0x%llx open_ms=%.3f\n",
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd_)), open_ms_);
      write_stderr(line);
    } else {
      start_settings_watcher();
    }
  }
  if (measure_input_ && input_pending_ && SUCCEEDED(result)) {
    input_pending_ = false;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    char line[64];
    std::snprintf(line, sizeof(line), "input_ms=%.3f\n", ms_between(input_start_, now));
    write_stderr(line);
  }
}

bool MainWindow::capture_to_bmp(const std::wstring& path) {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const LONG width = rc.right - rc.left;
  const LONG height = rc.bottom - rc.top;
  if (width <= 0 || height <= 0) return false;
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(header);
  header.biWidth = width;
  header.biHeight = -height;  // top-down
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO*>(&header),
                                    DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  bool ok = false;
  if (dc != nullptr && bitmap != nullptr) {
    HGDIOBJ old = SelectObject(dc, bitmap);
    ok = SUCCEEDED(renderer_.paint_to_dc(hwnd_, dc, editor_, view_));
    GdiFlush();
    SelectObject(dc, old);
  }
  if (ok) {
    const DWORD image_bytes = static_cast<DWORD>(width) * static_cast<DWORD>(height) * 4;
    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;  // "BM"
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + image_bytes;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
      DWORD written = 0;
      ok = WriteFile(file, &file_header, sizeof(file_header), &written, nullptr) &&
           WriteFile(file, &header, sizeof(header), &written, nullptr) &&
           WriteFile(file, bits, image_bytes, &written, nullptr) && written == image_bytes;
      CloseHandle(file);
    }
  }
  if (bitmap != nullptr) DeleteObject(bitmap);
  if (dc != nullptr) DeleteDC(dc);
  return ok;
}

void MainWindow::start_settings_watcher() {
  if (editor_.settings_path().empty()) return;
  std::wstring error;
  if (settings_watcher_.start(hwnd_, kMsgSettingsChanged, editor_.settings_path(), error)) {
    // The file may have changed between the startup read and now; the
    // watcher only sees later changes.
    if (file_state(editor_.settings_path()) != settings_state_) {
      editor_.reload_settings();
      after_input();
    }
    return;
  }
  // Decision D12: shown once; editing goes on without live reload.
  append_message(L"Cannot watch the settings file: " + error);
  after_input();
}

void MainWindow::append_message(const std::wstring& text) {
  const std::wstring& current = editor_.message();
  editor_.set_message(current.empty() ? text : current + L"; " + text);
}

void MainWindow::start_input_measure() {
  if (!measure_input_ || input_pending_) return;
  input_pending_ = true;
  QueryPerformanceCounter(&input_start_);
}

void MainWindow::handle_key(const Key& key) {
  editor_.handle_key(key);
  after_input();
}

void MainWindow::handle_char(wchar_t unit) {
  if (const std::optional<Key> key = chars_.feed(unit)) {
    handle_key(*key);
  } else if (input_pending_ && !is_high_surrogate(unit)) {
    // Dropped character: nothing to draw for it.
    input_pending_ = false;
  }
}

void MainWindow::handle_ime_composition(LPARAM lparam) {
  if ((lparam & GCS_RESULTSTR) == 0) return;
  std::wstring text;
  if (!take_ime_result(hwnd_, text)) return;
  start_input_measure();
  chars_.reset();
  editor_.insert_text(text);
  after_input();
  // The composition may continue: move the IME windows to the new caret.
  UpdateWindow(hwnd_);
  place_ime();
}

void MainWindow::handle_drop(HDROP drop) {
  std::wstring path;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  if (count > 0) {
    const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    path.resize(length + 1);
    const UINT copied = DragQueryFileW(drop, 0, path.data(), length + 1);
    path.resize(copied);
  }
  DragFinish(drop);
  if (path.empty()) return;
  SetForegroundWindow(hwnd_);
  editor_.request_open(path);
  // One window holds one file: say that the other dropped files were not
  // opened (after request_open's own message, if any).
  const std::wstring note = dropped_files_note(count);
  if (!note.empty()) {
    editor_.set_message(editor_.message().empty() ? note : editor_.message() + L"; " + note);
  }
  after_input();
}

void MainWindow::after_input() {
  if (editor_.pending_keys_text().empty()) {
    KillTimer(hwnd_, kPendingKeysTimer);
    view_.show_pending_keys = false;
  } else if (!view_.show_pending_keys) {
    SetTimer(hwnd_, kPendingKeysTimer, kPendingKeysDelayMs, nullptr);
  }
  update_title();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::update_title() {
  std::wstring title = editor_.buffer_name();
  if (editor_.buffer().modified()) title += L'*';
  title += L" - ";
  title += kAppName;
  if (title == title_) return;
  title_ = std::move(title);
  SetWindowTextW(hwnd_, title_.c_str());
}

void MainWindow::keep_point_visible() {
  const size_t rows = static_cast<size_t>(std::max(1, visible_lines()));
  const size_t line = editor_.point_line();
  const size_t top = editor_.top_line();
  if (line < top) {
    editor_.set_top_line(line);
  } else if (line >= top + rows) {
    editor_.set_top_line(line - rows + 1);
  }
}

void MainWindow::place_ime() {
  const float scale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
  const POINT caret{static_cast<LONG>(std::lround(view_.caret_x * scale)),
                    static_cast<LONG>(std::lround(view_.caret_y * scale))};
  place_ime_windows(hwnd_, caret, static_cast<int>(std::lround(view_.caret_height * scale)),
                    renderer_.font_family(), static_cast<int>(std::lround(renderer_.em() * scale)));
}

}  // namespace geditor::win
