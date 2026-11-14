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
#include "headless.h"
#include "ime.h"
#include "measure.h"
#include "process.h"
#include "question.h"
#include "unicode.h"

namespace geditor::win {
namespace {

constexpr wchar_t kWindowClass[] = L"geditor.main";
constexpr wchar_t kAppName[] = L"geditor";
constexpr UINT kMsgDestroy = WM_APP + 1;     // Host::quit, after the current call
// From the settings watcher: wParam 0 = the file changed; 1 = watching
// stopped (lParam: new std::wstring with the reason).
constexpr UINT kMsgSettingsChanged = WM_APP + 2;
constexpr UINT_PTR kPendingKeysTimer = 1;   // echo prefix keys after 1 second
constexpr UINT kPendingKeysDelayMs = 1000;
constexpr UINT_PTR kRedrawTimer = 2;        // retry a frame that could not be drawn
constexpr UINT kRedrawDelayMs = 500;
constexpr DWORD kToUnicodeNoStateChange = 0x4;  // Windows 10 1607 and later
constexpr ULONG_PTR kCopyDataCapture = 0x50414347;  // 'GCAP': WM_COPYDATA screenshot
// 'GFLT': WM_COPYDATA from tools/app_check.py; the payload (UTF-16) "target"
// or "font" makes the next render target / font creation fail.
constexpr ULONG_PTR kCopyDataFault = 0x544C4647;
// --headless: the window starts this far right of the virtual screen (no
// monitor shows it) with this size; the tools resize it in place.
constexpr int kHeadlessGap = 200;
constexpr int kHeadlessWidth = 900;
constexpr int kHeadlessHeight = 600;
constexpr wchar_t kInitError[] = L"Failed to initialize Direct2D/DirectWrite.";

// Keyboard, mouse and drop input: what a modal dialog keeps from its owner.
bool is_user_input(UINT msg) {
  return (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
         msg == WM_IME_CHAR || msg == WM_IME_COMPOSITION || msg == WM_IME_STARTCOMPOSITION ||
         msg == WM_DROPFILES;
}

std::wstring hex_hresult(HRESULT hr) {
  wchar_t text[16];
  swprintf_s(text, L"0x%08lX", static_cast<unsigned long>(hr));
  return text;
}

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

bool MainWindow::create(HINSTANCE instance, bool measure_startup, bool measure_input, bool headless) {
  measure_startup_ = measure_startup;
  measure_input_ = measure_input;
  headless_ = headless;
  // GEDITOR_HEADLESS_FAULT=init (headless only) fails here on purpose, so
  // that tools/app_check.py can see that no message box appears.
  const bool init_fault = headless_ && environment_variable(L"GEDITOR_HEADLESS_FAULT") == L"init";
  // The default font (Consolas 12pt = 16 DIPs); settings that ask for another
  // one rebuild it after they are read (apply_font).
  if (init_fault || !renderer_.init(FontSpec{})) {
    if (headless_) {
      // H4: no window in headless mode; stderr and the debugger say why.
      write_stderr("headless_error=init Failed to initialize Direct2D/DirectWrite.\n");
      OutputDebugStringW((std::wstring(L"geditor: ") + kInitError + L"\n").c_str());
    } else {
      MessageBoxW(nullptr, kInitError, kAppName, MB_ICONERROR);
    }
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
  // No WS_VSCROLL yet: update_scroll_bar adds the bar after the first frame.
  DWORD ex_style = WS_EX_ACCEPTFILES;
  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  int width = CW_USEDEFAULT;
  int height = CW_USEDEFAULT;
  if (headless_) {
    // H1: right of the virtual screen, so that no monitor shows it. A plain
    // window there never drew its first frame (measured on Windows 11 with
    // --measure-startup); a layered one (opaque, see below) paints as usual
    // (measured the same way). A tool window has no taskbar button and no
    // Alt+Tab entry; NOACTIVATE keeps it out of the foreground.
    ex_style |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + kHeadlessGap;
    y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = kHeadlessWidth;
    height = kHeadlessHeight;
  }
  hwnd_ = CreateWindowExW(ex_style, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW, x, y, width, height,
                          nullptr, nullptr, instance, this);
  if (hwnd_ == nullptr) return false;
  // A layered window shows nothing until its attributes are set.
  if (headless_) SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
  // The measurement scripts refuse a build that does not confirm the mode
  // (one that does not know --headless shows its window instead).
  if (headless_ && (measure_startup_ || measure_input_)) write_stderr("headless=1\n");
  renderer_.set_dpi(static_cast<float>(GetDpiForWindow(hwnd_)));
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
  apply_font(false);
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
  update_scroll_bar();
}

void MainWindow::show(int show_command) {
  if (headless_) {
    // Shown (so that WM_PAINT and EndDraw run as usual) but not activated.
    // Not ShowWindow: its first call may take the show command from the
    // STARTUPINFO of the parent process.
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  } else {
    ShowWindow(hwnd_, show_command);
  }
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
  if (!headless_) return read_clipboard(hwnd_, text);
  // H3: the private store; the real clipboard is never opened.
  text.clear();
  if (fail_clipboard_read_) {
    fail_clipboard_read_ = false;
    return ClipboardRead::kFailed;
  }
  if (!clipboard_has_text_) return ClipboardRead::kNoText;
  text = from_clipboard_text(clipboard_store_.c_str(), clipboard_store_.size());
  return ClipboardRead::kOk;
}

bool MainWindow::set_clipboard(const std::wstring& text) {
  if (!headless_) return write_clipboard(hwnd_, text);
  if (fail_clipboard_write_) {
    fail_clipboard_write_ = false;
    return false;
  }
  // CRLF line ends, as write_clipboard puts them on the real clipboard.
  clipboard_store_ = to_clipboard_text(text);
  clipboard_has_text_ = true;
  return true;
}

OpenChoice MainWindow::ask_open_choice(const std::wstring& path) {
  if (!headless_) return win::ask_open_choice(hwnd_, path);
  return open_choice_for(ask_headless(open_choice_question(path))).value_or(OpenChoice::kCancel);
}

SaveChoice MainWindow::ask_save_changes(const std::wstring& buffer_name) {
  if (!headless_) return win::ask_save_changes(hwnd_, buffer_name);
  return save_choice_for(ask_headless(save_changes_question(buffer_name))).value_or(SaveChoice::kCancel);
}

bool MainWindow::ask_yes_no(const std::wstring& question) {
  if (!headless_) return win::ask_yes_no(hwnd_, question);
  return yes_no_for(ask_headless(yes_no_question(question))).value_or(false);
}

bool MainWindow::launch_new_window(const std::wstring& path, std::wstring& error) {
  // H5: a new window of a headless one is headless too.
  return launch_editor(own_executable_path(), path, settings_argument_, error, headless_);
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
  const float scale = renderer_.dpi() / 96.0f;
  // An expanded echo area (settings errors) leaves fewer text rows.
  const float height = static_cast<float>(rc.bottom - rc.top) / scale;
  const int rows = renderer_.text_rows(height, renderer_.echo_rows(height, editor_));
  if (!IsIconic(hwnd_) && rc.bottom > rc.top && rc.right > rc.left) last_rows_ = rows;
  return rows;
}

int MainWindow::visible_columns() {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  // Minimised: keep the width of the last real size (no rewrapping).
  if (IsIconic(hwnd_) || rc.bottom <= rc.top || rc.right <= rc.left) {
    if (last_columns_ > 0) return last_columns_;
  }
  const float scale = renderer_.dpi() / 96.0f;
  const int columns = renderer_.text_columns(static_cast<float>(rc.right - rc.left) / scale);
  if (!IsIconic(hwnd_) && rc.bottom > rc.top && rc.right > rc.left) last_columns_ = columns;
  return columns;
}

int MainWindow::dpi() {
  return static_cast<int>(std::lround(renderer_.dpi()));
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
  if (question_ != nullptr) {
    // A headless question is pending: as under a modal dialog, input does
    // not reach the buffer. Closing and ending the session wait for the
    // answer (a second question cannot be asked meanwhile).
    if (is_user_input(msg)) return 0;
    if (msg == WM_CLOSE) {
      reject_request(L"WM_CLOSE while a question is pending");
      return 0;
    }
    if (msg == WM_QUERYENDSESSION) {
      reject_request(L"WM_QUERYENDSESSION while a question is pending");
      return FALSE;
    }
  }
  switch (msg) {
    case WM_PAINT:
      paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Direct2D clears the whole client area.
    case WM_PRINTCLIENT:
      renderer_.paint_to_dc(hwnd_, reinterpret_cast<HDC>(wparam), editor_, view_);
      return 0;
    case WM_COPYDATA:
      return handle_copy_data(reinterpret_cast<const COPYDATASTRUCT*>(lparam));
    case WM_SIZE:
      // Minimising (SIZE_MINIMIZED, 0 x 0 client) must not move the view.
      if (wparam == SIZE_MINIMIZED || LOWORD(lparam) == 0 || HIWORD(lparam) == 0) return 0;
      renderer_.resize(LOWORD(lparam), HIWORD(lparam));
      keep_point_visible();
      update_scroll_bar();
      return 0;
    case WM_DPICHANGED:
      handle_dpi_changed(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
      return 0;
    case WM_VSCROLL:
      handle_vscroll(LOWORD(wparam), HIWORD(wparam));
      return 0;
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
      if (wparam == kRedrawTimer) {
        KillTimer(hwnd_, kRedrawTimer);
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
      after_input();  // rebuilds the font when the settings changed it
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
  if (FAILED(result) && result != D2DERR_RECREATE_TARGET && !measure_startup_) {
    // No Direct2D frame (the render target could not be made): say so with
    // GDI instead of leaving a blank or stale window.
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    FillRect(ps.hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(ps.hdc, RGB(0xFF, 0xFF, 0xFF));
    SetBkMode(ps.hdc, TRANSPARENT);
    std::wstring text = L"Cannot draw the window (" + hex_hresult(result) + L"); retrying";
    DrawTextW(ps.hdc, text.data(), static_cast<int>(text.size()), &rc, DT_LEFT | DT_TOP | DT_NOPREFIX);
  }
  EndPaint(hwnd_, &ps);
  if (result == D2DERR_RECREATE_TARGET) {
    // Device lost: drop the target and paint again with a new one (measured
    // on the repainted frame).
    renderer_.discard_target();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (FAILED(result) && !measure_startup_) {
    if (draw_error_ == S_OK) draw_error_ = result;
    SetTimer(hwnd_, kRedrawTimer, kRedrawDelayMs, nullptr);
  } else if (SUCCEEDED(result) && draw_error_ != S_OK) {
    // Silent failures are reported once drawing works again ([C14]).
    append_message(L"Drawing failed (" + hex_hresult(draw_error_) + L"); the window was drawn again");
    draw_error_ = S_OK;
    InvalidateRect(hwnd_, nullptr, FALSE);
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
    // The scroll bar from now on (the client area narrows: another frame).
    ShowScrollBar(hwnd_, SB_VERT, TRUE);
    update_scroll_bar();
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

LRESULT MainWindow::handle_copy_data(const COPYDATASTRUCT* data) {
  if (data == nullptr) {
    reject_request(L"WM_COPYDATA without data");
    return 0;
  }
  const std::wstring kind = copy_data_kind_text(data->dwData);
  const std::optional<std::wstring> text = copy_data_text(data);
  if (!text) {
    reject_request(kind + L": bad payload (" + std::to_wstring(data->cbData) + L" bytes)");
    return 0;
  }
  switch (data->dwData) {
    case kCopyDataCapture:
      // Screenshot request of tools/app_driver.py: the UTF-16 path of a BMP
      // file to write the current frame to. Works on a locked desktop, where
      // the screen and PrintWindow show nothing of the window.
      if (!text->empty() && capture_to_bmp(*text)) return 1;
      reject_request(kind + L": cannot write the screenshot '" + *text + L"'");
      return 0;
    case kCopyDataFault:
      if (*text == L"target") {
        renderer_.fail_next_target();
        renderer_.discard_target();
        return 1;
      }
      if (*text == L"font") {
        renderer_.fail_next_font();
        return 1;
      }
      if (headless_ && *text == L"clipboard-read") {
        fail_clipboard_read_ = true;
        return 1;
      }
      if (headless_ && *text == L"clipboard-write") {
        fail_clipboard_write_ = true;
        return 1;
      }
      reject_request(kind + L": unknown fault '" + *text + L"'" +
                     (headless_ ? L"" : L" (clipboard faults need --headless)"));
      return 0;
    case kCopyDataState:
      if (!text->empty() && write_state(*text)) return 1;
      reject_request(kind + L": cannot write the state '" + *text + L"'");
      return 0;
    case kCopyDataClipboardWrite:
      if (!headless_) {
        reject_request(kind + L": the private clipboard needs --headless");
        return 0;
      }
      clipboard_store_ = *text;
      clipboard_has_text_ = true;
      return 1;
    case kCopyDataAnswer: {
      if (!headless_) {
        reject_request(kind + L": answers need --headless");
        return 0;
      }
      if (question_ == nullptr) {
        reject_request(kind + L": no question is pending ('" + *text + L"')");
        return 0;
      }
      const int id = question_button_id(*question_, *text);
      if (id == 0) {
        // The question stays pending.
        reject_request(kind + L": no button '" + *text + L"' in '" + question_->instruction + L"'");
        return 0;
      }
      question_answer_ = id;
      PostMessageW(hwnd_, WM_NULL, 0, 0);  // wakes up the loop of ask_headless
      return 1;
    }
    default:
      reject_request(L"unknown WM_COPYDATA kind " + kind);
      return 0;
  }
}

void MainWindow::reject_request(const std::wstring& reason) {
  ++rejected_requests_;
  last_rejected_ = reason;
  OutputDebugStringW((L"geditor: rejected: " + reason + L"\n").c_str());
}

bool MainWindow::write_state(const std::wstring& path) {
  std::wstring text = state_line(L"headless", headless_ ? L"1" : L"0");
  text += state_line(L"settings", settings_argument_);
  text += state_line(L"title", title_);
  text += state_line(L"message", editor_.message());
  text += state_line(L"modified", editor_.buffer().modified() ? L"1" : L"0");
  text += state_line(L"length", std::to_wstring(editor_.buffer().text.length()));
  if (headless_) {
    text += state_line(L"clipboard", clipboard_has_text_ ? L"text" : L"empty");
    text += state_line(L"clipboard_text", clipboard_store_);
  }
  text += state_line(L"rejected", std::to_wstring(rejected_requests_));
  text += state_line(L"last_rejected", last_rejected_);
  text += question_ != nullptr ? question_state(*question_) : state_line(L"question", L"none");
  std::wstring error;
  return win::write_file(path,
                         std::string_view(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t)),
                         error);
}

int MainWindow::ask_headless(const Question& question) {
  if (question_ != nullptr) {
    // Not reached while handle_message holds back input, closing and the
    // session end; answered like Esc if it ever is.
    reject_request(L"a second question while one is pending: " + question.instruction);
    return IDCANCEL;
  }
  // H4: no window. The tools read the question from the state ('GSTA') and
  // press one of its buttons ('GANS'). Meanwhile this loop runs like the
  // modal loop of a task dialog: painting, timers, the settings watcher and
  // WM_COPYDATA go on; handle_message holds back input.
  question_ = &question;
  question_answer_ = 0;
  InvalidateRect(hwnd_, nullptr, FALSE);
  int answer = IDCANCEL;
  MSG msg{};
  for (;;) {
    if (question_answer_ != 0) {
      answer = question_answer_;
      break;
    }
    const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
    if (got <= 0) {
      if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));  // for the main loop
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  question_ = nullptr;
  question_answer_ = 0;
  return answer;
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
  if (!headless_) SetForegroundWindow(hwnd_);  // H1: headless stays in the background
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
  apply_font(false);
  update_scroll_bar();
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
  editor_.ensure_point_visible();
}

void MainWindow::apply_font(bool force) {
  const Config& config = editor_.config();
  const float dpi = renderer_.dpi();
  FontSpec spec;
  spec.family = config.font_family;
  spec.fallback = config.font_fallback;
  spec.em = static_cast<float>(editor_.font_pixels(dpi) * 96.0 / dpi);
  if (!force && spec == renderer_.font_spec()) return;
  std::vector<std::wstring> missing;
  if (!renderer_.set_font(spec, &missing)) {
    const FontSpec& kept = renderer_.font_spec();
    append_message(L"Cannot create the font " + spec.family + L" at " +
                   std::to_wstring(std::lround(spec.em * dpi / 96.0f)) + L" px; keeping " + kept.family);
  }
  // Decision E1: once per value of font.family / font.fallback; rebuilds for
  // text-scale, DPI or font.size stay silent.
  for (const std::wstring& name : missing_fonts_.take(spec.family, spec.fallback, missing)) {
    append_message(L"Font not found: " + name);
  }
  editor_.ensure_point_visible();
  update_scroll_bar();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::handle_dpi_changed(UINT dpi, const RECT& suggested) {
  // Decision E7: fonts and cells for the new DPI, the suggested rectangle,
  // and the same top line.
  const RowPos top = editor_.top();
  renderer_.set_dpi(static_cast<float>(dpi));
  apply_font(true);
  SetWindowPos(hwnd_, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
               suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
  editor_.set_top(top);
  editor_.ensure_point_visible();
  update_scroll_bar();
  update_title();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::handle_vscroll(int code, unsigned position) {
  const long long page = std::max(1, visible_lines() - 2);
  switch (code) {
    case SB_LINEUP: editor_.scroll_by_rows(-1); break;
    case SB_LINEDOWN: editor_.scroll_by_rows(1); break;
    case SB_PAGEUP: editor_.scroll_by_rows(-page); break;
    case SB_PAGEDOWN: editor_.scroll_by_rows(page); break;
    case SB_TOP: editor_.scroll_to_line(0); break;
    case SB_BOTTOM: editor_.scroll_to_line(editor_.buffer().text.line_count() - 1); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
      // The message carries 16 bits of the position: beyond that range the
      // 32-bit tracking position of the drag is used.
      SCROLLINFO si{};
      si.cbSize = sizeof(si);
      si.fMask = SIF_TRACKPOS | SIF_RANGE;
      if (!GetScrollInfo(hwnd_, SB_VERT, &si)) return;
      const int pos = si.nMax <= 0xFFFF ? static_cast<int>(position) : si.nTrackPos;
      editor_.scroll_to_line(static_cast<size_t>(std::max(0, pos)));
      break;
    }
    default: return;
  }
  after_input();
}

void MainWindow::update_scroll_bar() {
  // The bar appears after the first frame (PLAN 4.1: its first, themed
  // drawing is not part of the startup time); SetScrollInfo adds it.
  if (!first_frame_done_ || measure_startup_) return;
  const ScrollBarState state = editor_.scroll_bar();
  constexpr size_t kMaxRange = 0x7FFFFFFF;
  SCROLLINFO si{};
  si.cbSize = sizeof(si);
  // Always shown (disabled when everything fits), so that the client width
  // and the wrap width do not change with it.
  si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
  si.nMin = 0;
  si.nMax = static_cast<int>(std::min(state.max, kMaxRange));
  si.nPage = static_cast<UINT>(std::min(state.page, kMaxRange));
  si.nPos = static_cast<int>(std::min(state.pos, kMaxRange));
  // Unchanged: no call (each one repaints the bar).
  if (scroll_set_ && si.nMax == scroll_shown_.nMax && si.nPage == scroll_shown_.nPage &&
      si.nPos == scroll_shown_.nPos) {
    return;
  }
  // Set first: the call below may resize the client area (WM_SIZE comes
  // back here while it runs).
  scroll_shown_ = si;
  scroll_set_ = true;
  SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void MainWindow::place_ime() {
  const float scale = renderer_.dpi() / 96.0f;
  const POINT caret{static_cast<LONG>(std::lround(view_.caret_x * scale)),
                    static_cast<LONG>(std::lround(view_.caret_y * scale))};
  place_ime_windows(hwnd_, caret, static_cast<int>(std::lround(view_.caret_height * scale)),
                    renderer_.font_family(), static_cast<int>(std::lround(renderer_.em() * scale)));
}

}  // namespace geditor::win
