"""Drive a running geditor window for real-app checks (Windows only).

Used by tools/app_check.py. With HEADLESS (the default of this module and
of app_check; a caller sets HEADLESS = False to show windows) every
geditor is started with --headless: its window is outside every monitor and
never activated, keys are posted (WM_KEYDOWN / WM_CHAR with the modifier
state set through AttachThreadInput + SetKeyboardState), the clipboard is a
private store inside geditor and questions are read and answered through
WM_COPYDATA (App.state / wait_question / answer). In headless mode the
calls that would take the user's foreground, keyboard, clipboard or screen
(SendInput, SetForegroundWindow, BringWindowToTop, SetActiveWindow,
keybd_event, the clipboard functions, MoveWindow, activating ShowWindow /
SetWindowPos) raise DriverError instead of running (see _GUARDED).

Without HEADLESS (--visible) keys are sent with SendInput, which only
reaches the foreground window: every send first checks that the geditor
window is in the foreground and raises otherwise, so that keystrokes never
go to another program. On a locked desktop keys are posted instead (see
App.use_input). geditor windows are captured by asking geditor to draw into
a BMP (WM_COPYDATA); other windows with PrintWindow. Images are written as
PNG.
"""

import ctypes
import ctypes.wintypes as wt
import importlib.util
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

# No bytecode cache in the work tree. Python writes this module's .pyc while
# importing it (before this code runs) unless the importer set
# sys.dont_write_bytecode, so remove that file and the then empty folder.
sys.dont_write_bytecode = True
try:
    _cache = pathlib.Path(importlib.util.cache_from_source(__file__))
    if _cache.is_file():
        _cache.unlink()
    if _cache.parent.is_dir() and not any(_cache.parent.iterdir()):
        _cache.parent.rmdir()
except (OSError, NotImplementedError, ValueError):
    pass

class DriverError(RuntimeError):
    pass


# Start geditor with --headless and refuse every call in _GUARDED (the
# runtime guard of the headless mode). On by default, also for a script
# that imports this module as a library: showing a window on the user's
# screen needs an explicit HEADLESS = False (app_check --visible).
HEADLESS = True

SW_HIDE = 0
SW_SHOWNOACTIVATE = 4
SW_SHOWMINNOACTIVE = 7
SW_SHOWNA = 8
SWP_NOSIZE = 0x0001
SWP_NOMOVE = 0x0002
SWP_NOZORDER = 0x0004
SWP_NOACTIVATE = 0x0010


def _show_does_not_activate(hwnd, command, *_):
    # No minimising: Windows parks a minimised tool window (no taskbar
    # button) as a caption bar on the primary monitor (measured).
    return command in (SW_HIDE, SW_SHOWNOACTIVATE, SW_SHOWNA)


def _set_pos_does_not_move_or_activate(hwnd, after, x, y, cx, cy, flags, *_):
    return (flags & SWP_NOMOVE) and (flags & SWP_NOACTIVATE)


# Calls refused in headless mode: None refuses every call, a function
# allows the calls for which it returns true.
_GUARDED = {
    "SendInput": None, "keybd_event": None, "SetForegroundWindow": None,
    "BringWindowToTop": None, "SetActiveWindow": None, "SetFocus": None,
    "OpenClipboard": None, "EmptyClipboard": None, "SetClipboardData": None,
    "GetClipboardData": None, "MoveWindow": None,
    "ShowWindow": _show_does_not_activate,
    "SetWindowPos": _set_pos_does_not_move_or_activate,
}


class _GuardedFunction:
    """A user32 function that raises DriverError when HEADLESS refuses the
    call. Attributes (argtypes, restype) go to the real function."""

    def __init__(self, name, function, allow):
        object.__setattr__(self, "_name", name)
        object.__setattr__(self, "_function", function)
        object.__setattr__(self, "_allow", allow)

    def __call__(self, *args):
        if HEADLESS and (self._allow is None or not self._allow(*args)):
            raise DriverError(f"{self._name} is not allowed in headless mode")
        return self._function(*args)

    def __getattr__(self, attr):
        return getattr(self._function, attr)

    def __setattr__(self, attr, value):
        setattr(self._function, attr, value)


class _GuardedDll:
    """user32 with the functions of _GUARDED wrapped in _GuardedFunction."""

    def __init__(self, dll):
        object.__setattr__(self, "_dll", dll)
        object.__setattr__(self, "_wrapped", {})

    def __getattr__(self, name):
        if name in _GUARDED:
            if name not in self._wrapped:
                self._wrapped[name] = _GuardedFunction(name, getattr(self._dll, name), _GUARDED[name])
            return self._wrapped[name]
        return getattr(self._dll, name)


user32 = _GuardedDll(ctypes.WinDLL("user32", use_last_error=True))
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

# Physical pixels for window rectangles and captures.
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2

WM_CHAR = 0x0102
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_SYSKEYDOWN = 0x0104
WM_SYSKEYUP = 0x0105
WM_IME_CHAR = 0x0286
WM_CLOSE = 0x0010
WM_USER = 0x0400
TDM_CLICK_BUTTON = WM_USER + 102
INPUT_KEYBOARD = 1
KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_UNICODE = 0x0004
PW_RENDERFULLCONTENT = 0x00000002

VK_SHIFT = 0x10
VK_CONTROL = 0x11
VK_MENU = 0x12

NAMED_KEYS = {
    "RET": 0x0D, "TAB": 0x09, "ESC": 0x1B, "DEL": 0x08, "SPC": 0x20,
    "<delete>": 0x2E, "<insert>": 0x2D, "<home>": 0x24, "<end>": 0x23,
    "<prior>": 0x21, "<next>": 0x22, "<left>": 0x25, "<up>": 0x26,
    "<right>": 0x27, "<down>": 0x28,
}
EXTENDED_KEYS = {0x2E, 0x2D, 0x24, 0x23, 0x21, 0x22, 0x25, 0x26, 0x27, 0x28}


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", INPUTUNION)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetWindowThreadProcessId.restype = wt.DWORD
user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.GetForegroundWindow.restype = wt.HWND
user32.IsWindow.argtypes = [wt.HWND]
user32.SendInput.argtypes = [wt.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.GetDC.restype = wt.HDC
gdi32.CreateCompatibleDC.restype = wt.HDC
gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
gdi32.CreateCompatibleBitmap.restype = wt.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.restype = wt.HGDIOBJ
gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wt.HDC]
user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
user32.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                ctypes.c_int, wt.UINT]
user32.MoveWindow.argtypes = [wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              wt.BOOL]
gdi32.BitBlt.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.HDC,
                         ctypes.c_int, ctypes.c_int, wt.DWORD]
gdi32.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p,
                            ctypes.c_void_p, wt.UINT]


# Set by tools/app_check.py --input post: keys are always posted. HEADLESS
# posts them too.
FORCE_POST = False

user32.SendMessageTimeoutW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM, wt.UINT, wt.UINT,
                                       ctypes.POINTER(ctypes.c_size_t)]
user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t
user32.GetWindowLongPtrW.argtypes = [wt.HWND, ctypes.c_int]
user32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
user32.GetClipboardSequenceNumber.restype = wt.DWORD
GWL_EXSTYLE = -20
WS_EX_TOOLWINDOW = 0x00000080
WS_EX_APPWINDOW = 0x00040000
WS_EX_NOACTIVATE = 0x08000000
WS_EX_LAYERED = 0x00080000
SMTO_ABORTIFHUNG = 0x0002


def window_class(hwnd) -> str:
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def window_text(hwnd) -> str:
    buf = ctypes.create_unicode_buffer(1024)
    user32.GetWindowTextW(hwnd, buf, 1024)
    return buf.value


def windows_of(pid: int, class_name: str | None = None, visible_only: bool = True) -> list:
    found = []

    def callback(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and (not visible_only or user32.IsWindowVisible(hwnd)):
            if class_name is None or window_class(hwnd) == class_name:
                found.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(callback), 0)
    return found


def windows_of_class(class_name: str) -> list:
    """Visible top-level windows of `class_name` in any process."""
    found = []

    def callback(hwnd, _):
        if user32.IsWindowVisible(hwnd) and window_class(hwnd) == class_name:
            found.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(callback), 0)
    return found


# The geditor processes that App started; started_windows() adds the
# processes that they started (new windows).
STARTED_PIDS = set()


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD),
                ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", wt.LONG), ("dwFlags", wt.DWORD), ("szExeFile", wt.WCHAR * 260)]


_kernel32_toolhelp = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32_toolhelp.CreateToolhelp32Snapshot.restype = wt.HANDLE
_kernel32_toolhelp.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
_kernel32_toolhelp.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
_kernel32_toolhelp.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
_kernel32_toolhelp.CloseHandle.argtypes = [wt.HANDLE]


def parent_pids() -> dict:
    """pid -> parent pid of every process (CreateToolhelp32Snapshot)."""
    parents = {}
    snapshot = _kernel32_toolhelp.CreateToolhelp32Snapshot(0x00000002, 0)  # TH32CS_SNAPPROCESS
    if snapshot in (None, ctypes.c_void_p(-1).value):
        return parents
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        ok = _kernel32_toolhelp.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            parents[entry.th32ProcessID] = entry.th32ParentProcessID
            ok = _kernel32_toolhelp.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        _kernel32_toolhelp.CloseHandle(snapshot)
    return parents


def started_windows(class_name: str = "geditor.main") -> list:
    """Visible top-level windows of `class_name` that belong to a process this
    run started (App) or to a process those started, so that another
    geditor on the desktop does not count."""
    parents = parent_pids()
    changed = True
    while changed:  # children of started processes are ours too
        changed = False
        for pid, parent in parents.items():
            if parent in STARTED_PIDS and pid not in STARTED_PIDS:
                STARTED_PIDS.add(pid)
                changed = True
    found = []

    def callback(hwnd, _):
        if user32.IsWindowVisible(hwnd) and window_class(hwnd) == class_name:
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value in STARTED_PIDS:
                found.append(hwnd)
        return True

    user32.EnumWindows(WNDENUMPROC(callback), 0)
    return found


def wait_for(predicate, timeout_s: float = 10.0, interval_s: float = 0.05):
    end = time.monotonic() + timeout_s
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() > end:
            return value
        time.sleep(interval_s)


class App:
    """A geditor process started by this driver (stopped by its own handle).

    `settings` is passed as --settings=PATH so that checks never touch the
    user's %USERPROFILE%\\.geditor.json (the file need not exist). With
    HEADLESS, --headless comes first. Visible with FORCE_POST, the window
    is shown without activation (SW_SHOWNOACTIVATE)."""

    def __init__(self, exe: pathlib.Path, args: list[str], cwd: pathlib.Path | None = None,
                 width: int = 900, height: int = 500, settings: pathlib.Path | None = None):
        if settings is not None:
            args = [f"--settings={settings}", *args]
        self.headless = HEADLESS
        if self.headless:
            args = ["--headless", *args]
        startupinfo = None
        if not self.headless and FORCE_POST:
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startupinfo.wShowWindow = SW_SHOWNOACTIVATE
        self.proc = subprocess.Popen([str(exe), *args], cwd=cwd, startupinfo=startupinfo)
        windows = wait_for(lambda: windows_of(self.proc.pid, "geditor.main"), 15.0)
        if not windows:
            self.kill()
            raise DriverError("geditor window did not appear")
        self.hwnd = windows[0]
        STARTED_PIDS.add(self.proc.pid)
        if self.headless:
            # A build without --headless shows its window and has no state
            # request: refuse it instead of checking a visible window.
            try:
                confirmed = read_state(self.hwnd).get("headless") == "1"
            except DriverError:
                confirmed = False
            if not confirmed:
                self.kill()
                raise DriverError("this build did not confirm --headless (no 'headless=1' in its state); "
                                  "check it with --visible")
        # A fixed size keeps screenshots small and comparable.
        self.resize(width, height)
        time.sleep(0.3)

    def resize(self, width: int, height: int):
        """Headless: a new size in place (the window stays outside every
        monitor). Visible: at (100, 100) on the screen."""
        if self.headless:
            user32.SetWindowPos(self.hwnd, None, 0, 0, width, height,
                                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
            if on_a_monitor(self.hwnd):
                raise DriverError(f"the headless window is on a monitor after the resize: {window_rect(self.hwnd)}")
        else:
            user32.MoveWindow(self.hwnd, 100, 100, width, height, True)

    # ---- headless channel (WM_COPYDATA) ----
    def state(self) -> dict:
        return read_state(self.hwnd)

    def wait_question(self, timeout_s: float = 5.0) -> dict | None:
        """The pending question (kind, text, buttons) once there is one."""
        def pending():
            s = self.state()
            return s if s.get("question", "none") != "none" else None
        return wait_for(pending, timeout_s)

    def answer(self, label: str) -> bool:
        """Presses the button `label` of the pending question; False when
        geditor rejects it (no such button, or no question)."""
        return copy_data(self.hwnd, COPYDATA_ANSWER, label) == 1

    def clipboard(self) -> str | None:
        """The private clipboard (CRLF line ends); None when it holds no text."""
        s = self.state()
        return s.get("clipboard_text") if s.get("clipboard") == "text" else None

    def set_clipboard(self, text: str) -> bool:
        return copy_data(self.hwnd, COPYDATA_CLIPBOARD_WRITE, text) == 1

    def fault(self, what: str) -> bool:
        """'GFLT': "target", "font", "clipboard-read" or "clipboard-write"."""
        return inject_fault(self.hwnd, what)

    # ---- state ----
    def title(self) -> str:
        return window_text(self.hwnd)

    def alive(self) -> bool:
        return self.proc.poll() is None

    def dialogs(self) -> list:
        return windows_of(self.proc.pid, "#32770")

    def wait_dialog(self, timeout_s: float = 5.0):
        found = wait_for(self.dialogs, timeout_s)
        if found:
            time.sleep(0.6)  # let the dialog draw itself before a capture
        return found[0] if found else None

    def wait_exit(self, timeout_s: float = 5.0) -> bool:
        try:
            self.proc.wait(timeout_s)
            return True
        except subprocess.TimeoutExpired:
            return False

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(5)

    # ---- input ----
    def focus(self):
        if user32.GetForegroundWindow() == self.hwnd:
            return
        # Joining the input queue of the foreground thread lets this process
        # move the foreground (no keystroke goes to another program).
        foreground = user32.GetForegroundWindow()
        fg_thread = user32.GetWindowThreadProcessId(foreground, None) if foreground else 0
        own_thread = ctypes.windll.kernel32.GetCurrentThreadId()
        attached = bool(fg_thread) and fg_thread != own_thread and \
            user32.AttachThreadInput(own_thread, fg_thread, True)
        try:
            user32.ShowWindow(self.hwnd, 9)  # SW_RESTORE
            user32.BringWindowToTop(self.hwnd)
            user32.SetForegroundWindow(self.hwnd)
        finally:
            if attached:
                user32.AttachThreadInput(own_thread, fg_thread, False)
        if not wait_for(lambda: user32.GetForegroundWindow() == self.hwnd, 2.0):
            raise DriverError("cannot bring the geditor window to the foreground")
        time.sleep(0.1)

    def use_input(self) -> str:
        """Chooses how keys are sent: "sendinput" when the window can be
        brought to the foreground (an unlocked desktop), else "post".
        FORCE_POST and HEADLESS pick "post" without touching the foreground
        (someone is using the desktop)."""
        if FORCE_POST or self.headless:
            self.mode = "post"
            return self.mode
        try:
            self.focus()
            self.mode = "sendinput"
        except DriverError:
            self.mode = "post"
        return self.mode

    mode = "post"

    def refocus(self):
        """Brings the window back to the foreground after another window
        (such as a second geditor) took it; nothing to do in "post" mode."""
        if self.mode == "sendinput":
            self.focus()

    def _require_foreground(self):
        if user32.GetForegroundWindow() != self.hwnd:
            raise DriverError("geditor is not the foreground window; input not sent")

    def keys(self, notation: str, pause_s: float = 0.08):
        """Sends keys written in Emacs notation, such as "C-x C-s"."""
        for key in notation.split():
            if self.mode == "sendinput":
                self._require_foreground()
                _send(_chord_inputs(key))
            else:
                self._post_key(key)
            time.sleep(pause_s)

    def _post_key(self, key: str):
        """Posts WM_KEYDOWN / WM_SYSKEYDOWN with the modifier state set
        through the shared input state of AttachThreadInput (GetKeyState in
        geditor then sees Ctrl / Alt / Shift as if they were held)."""
        vk, mods = _parse_chord(key)
        self._post_vk(vk, mods)

    def _post_key_raw(self, vk: int):
        """Posts a single key such as VK_MENU alone (Alt pressed and released)."""
        self._post_vk(vk, [vk] if vk in (VK_MENU, VK_CONTROL, VK_SHIFT) else [])

    def _post_vk(self, vk: int, mods: list):
        app_thread = user32.GetWindowThreadProcessId(self.hwnd, None)
        own_thread = ctypes.windll.kernel32.GetCurrentThreadId()
        if not user32.AttachThreadInput(own_thread, app_thread, True):
            raise DriverError("AttachThreadInput failed")
        try:
            state = (ctypes.c_ubyte * 256)()
            for m in mods:
                state[m] = 0x80
            user32.SetKeyboardState(state)
            alt = VK_MENU in mods
            scan = user32.MapVirtualKeyW(vk, 0)
            lparam = 1 | (scan << 16) | ((1 << 24) if vk in EXTENDED_KEYS else 0) | \
                ((1 << 29) if alt else 0)
            user32.PostMessageW(self.hwnd, WM_SYSKEYDOWN if alt else WM_KEYDOWN, vk, lparam)
            time.sleep(0.15)  # let geditor read the state before it is reset
            up = lparam | (1 << 30) | (1 << 31)
            user32.PostMessageW(self.hwnd, WM_SYSKEYUP if alt else WM_KEYUP, vk, up)
            time.sleep(0.05)
            user32.SetKeyboardState((ctypes.c_ubyte * 256)())
        finally:
            user32.AttachThreadInput(own_thread, app_thread, False)

    def type(self, text: str, pause_s: float = 0.05):
        """Types text with KEYEVENTF_UNICODE (VK_PACKET -> WM_CHAR), or posts
        WM_CHAR when SendInput cannot be used."""
        if self.mode != "sendinput":
            self.post_chars(text, WM_CHAR, pause_s)
            return
        self._require_foreground()
        inputs = []
        for unit in struct.unpack(f"<{len(text.encode('utf-16-le')) // 2}H",
                                  text.encode("utf-16-le")):
            inputs.append(_unicode_input(unit, False))
            inputs.append(_unicode_input(unit, True))
        _send(inputs)
        time.sleep(pause_s)

    def post_chars(self, text: str, message: int = WM_CHAR, pause_s: float = 0.05):
        """Posts WM_CHAR (or WM_IME_CHAR) messages, one per UTF-16 unit."""
        data = text.encode("utf-16-le")
        for (unit,) in struct.iter_unpack("<H", data):
            user32.PostMessageW(self.hwnd, message, unit, 1)
        time.sleep(pause_s)

    def close(self):
        user32.PostMessageW(self.hwnd, WM_CLOSE, 0, 0)

    # ---- screen ----
    def capture(self, path: pathlib.Path, hwnd=None) -> tuple[int, int, bytes]:
        return capture(hwnd or self.hwnd, path)


def click_dialog_button(dialog, button_id: int):
    user32.SendMessageW(dialog, TDM_CLICK_BUTTON, button_id, 0)


def _key_input(vk: int, up: bool) -> INPUT:
    flags = KEYEVENTF_KEYUP if up else 0
    if vk in EXTENDED_KEYS:
        flags |= KEYEVENTF_EXTENDEDKEY
    scan = user32.MapVirtualKeyW(vk, 0)
    return INPUT(INPUT_KEYBOARD, INPUTUNION(ki=KEYBDINPUT(vk, scan, flags, 0, 0)))


def _unicode_input(unit: int, up: bool) -> INPUT:
    flags = KEYEVENTF_UNICODE | (KEYEVENTF_KEYUP if up else 0)
    return INPUT(INPUT_KEYBOARD, INPUTUNION(ki=KEYBDINPUT(0, unit, flags, 0, 0)))


def _chord_inputs(key: str) -> list:
    vk, mods = _parse_chord(key)
    inputs = [_key_input(m, False) for m in mods]
    inputs += [_key_input(vk, False), _key_input(vk, True)]
    inputs += [_key_input(m, True) for m in reversed(mods)]
    return inputs


def _parse_chord(key: str) -> tuple[int, list]:
    """Virtual key and modifier keys for one key in Emacs notation."""
    mods = []
    while len(key) > 2 and key[1] == "-" and key[0] in "CMS":
        mods.append({"C": VK_CONTROL, "M": VK_MENU, "S": VK_SHIFT}[key[0]])
        key = key[2:]
    if key in NAMED_KEYS:
        vk = NAMED_KEYS[key]
    elif key.startswith("<f") and key.endswith(">"):
        vk = 0x70 + int(key[2:-1]) - 1
    elif len(key) == 1:
        scan = user32.VkKeyScanW(ord(key))
        if scan == -1 or scan == 0xFFFF:
            raise DriverError(f"no key for {key!r} in this layout")
        vk = scan & 0xFF
        if scan & 0x100 and VK_SHIFT not in mods:
            mods.append(VK_SHIFT)
    else:
        raise DriverError(f"unknown key {key!r}")
    return vk, mods


def _send(inputs: list):
    array = (INPUT * len(inputs))(*inputs)
    sent = user32.SendInput(len(inputs), array, ctypes.sizeof(INPUT))
    if sent != len(inputs):
        raise DriverError(f"SendInput sent {sent} of {len(inputs)} (error {ctypes.get_last_error()})")


class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [("dwData", ctypes.c_size_t), ("cbData", wt.DWORD), ("lpData", ctypes.c_void_p)]


COPYDATA_CAPTURE = 0x50414347  # 'GCAP', see MainWindow WM_COPYDATA
COPYDATA_STATE = 0x41545347  # 'GSTA', see src/win/headless.h
COPYDATA_CLIPBOARD_WRITE = 0x57424347  # 'GCBW'
COPYDATA_ANSWER = 0x534E4147  # 'GANS'


def copy_data(hwnd, kind: int, text: str | None, size: int | None = None,
              timeout_ms: int = 5000) -> int:
    """Sends WM_COPYDATA `kind` with the UTF-16 `text` (None: no data
    pointer); `size` overrides cbData (for the checks of bad payloads).
    Returns geditor's reply (1 done, 0 rejected); raises when geditor does
    not answer within `timeout_ms`."""
    if text is None:
        data = COPYDATASTRUCT(kind, size or 0, None)
    else:
        buffer = ctypes.create_unicode_buffer(text)
        units = len(text.encode("utf-16-le")) // 2
        data = COPYDATASTRUCT(kind, units * 2 if size is None else size, ctypes.cast(buffer, ctypes.c_void_p))
    result = ctypes.c_size_t(0)
    if not user32.SendMessageTimeoutW(hwnd, 0x004A, 0, ctypes.addressof(data), SMTO_ABORTIFHUNG,
                                      timeout_ms, ctypes.byref(result)):
        raise DriverError(f"WM_COPYDATA {kind:#x} got no answer (error {ctypes.get_last_error()})")
    return result.value


def _unescape(value: str) -> str:
    out = []
    i = 0
    while i < len(value):
        c = value[i]
        if c == "\\" and i + 1 < len(value):
            out.append({"n": "\n", "r": "\r", "\\": "\\"}.get(value[i + 1], value[i + 1]))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def read_state(hwnd) -> dict:
    """The state of a geditor window ('GSTA', src/win/headless.h): one
    value per key, except "button" (a list of the labels)."""
    path = pathlib.Path(tempfile.gettempdir()) / f"geditor_state_{os.getpid()}_{threading.get_ident()}.txt"
    try:
        if copy_data(hwnd, COPYDATA_STATE, str(path)) != 1:
            raise DriverError("geditor did not write its state")
        text = path.read_bytes().decode("utf-16-le")
    finally:
        try:
            path.unlink()
        except OSError:
            pass
    state = {"button": []}
    for line in text.split("\n"):
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = _unescape(value)
        if key == "button":
            state["button"].append(value)
        else:
            state[key] = value
    return state


def ex_style(hwnd) -> int:
    return user32.GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & 0xFFFFFFFF


MONITORENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM)
user32.EnumDisplayMonitors.argtypes = [wt.HDC, ctypes.c_void_p, MONITORENUMPROC, wt.LPARAM]


def monitor_rects() -> list:
    """(left, top, right, bottom) of every monitor (EnumDisplayMonitors)."""
    rects = []

    def callback(monitor, dc, rect, _):
        r = rect.contents
        rects.append((r.left, r.top, r.right, r.bottom))
        return True

    user32.EnumDisplayMonitors(None, None, MONITORENUMPROC(callback), 0)
    return rects


user32.MonitorFromRect.argtypes = [ctypes.POINTER(wt.RECT), wt.DWORD]
user32.MonitorFromRect.restype = wt.HMONITOR
user32.IsIconic.argtypes = [wt.HWND]
user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
WM_SIZE = 0x0005
SIZE_RESTORED = 0
SIZE_MINIMIZED = 1


def client_size(hwnd) -> tuple[int, int]:
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    return rect.right - rect.left, rect.bottom - rect.top


def on_a_monitor(hwnd) -> bool:
    """True when the window rectangle meets a monitor or the window is
    iconic (a minimised window may be parked on a monitor)."""
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return bool(user32.IsIconic(hwnd)) or bool(user32.MonitorFromRect(ctypes.byref(rect), 0))


def intersects_a_monitor(hwnd) -> tuple[bool, tuple, list]:
    r = window_rect(hwnd)
    rect = (r.left, r.top, r.right, r.bottom)
    monitors = monitor_rects()
    hit = any(rect[0] < m[2] and m[0] < rect[2] and rect[1] < m[3] and m[1] < rect[3] for m in monitors)
    return hit, rect, monitors


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.GetCurrentProcess.restype = wt.HANDLE
kernel32.GetCurrentThread.restype = wt.HANDLE
kernel32.SetPriorityClass.argtypes = [wt.HANDLE, wt.DWORD]
kernel32.SetThreadPriority.argtypes = [wt.HANDLE, ctypes.c_int]
user32.FindWindowExW.argtypes = [wt.HWND, wt.HWND, wt.LPCWSTR, wt.LPCWSTR]
user32.FindWindowExW.restype = wt.HWND
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def process_image(pid: int) -> str:
    """The full path of the program of process `pid` ("" when unknown)."""
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return ""
    try:
        size = wt.DWORD(1024)
        buffer = ctypes.create_unicode_buffer(size.value)
        if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return ""
        return buffer.value
    finally:
        kernel32.CloseHandle(handle)


class ForegroundSampler:
    """Samples, every `interval_s`, the foreground window and the visible
    task dialogs (#32770) in a process of its own (so that the work of the
    checks never delays a sample), and reports those that belong to a
    process running `exe` (every headless geditor of a check, new windows
    and the measurement scripts included) or are a "geditor.main" window.
    The dialogs are scanned by a thread of their own (dialog_scans,
    dialog_scan_max_gap_s); the same thread checks that every headless
    geditor window (a "geditor.main" tool window) is off every monitor and
    not iconic (window_checks, on_screen). start(), then stop() fills samples, max_gap_s,
    gaps_over_50ms, long_gaps, foreground_hits, dialogs,
    foreground_windows and geditor_windows_seen."""

    def __init__(self, exe: pathlib.Path, interval_s: float = 0.02):
        self.exe = str(exe)
        self.interval_s = interval_s
        self.proc = None
        self.samples = 0
        self.max_gap_s = 0.0
        self.gaps_over_50ms = 0
        self.foreground_hits = []
        self.dialogs = []
        self.long_gaps = []
        self.foreground_windows = 0
        self.geditor_windows_seen = 0
        self.dialog_scans = 0
        self.dialog_scan_max_gap_s = 0.0
        self.window_checks = 0
        self.on_screen = []

    def start(self):
        self.proc = subprocess.Popen(
            [sys.executable, "-I", str(pathlib.Path(__file__).resolve()), "--sample-foreground",
             self.exe, str(self.interval_s)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        ready = self.proc.stdout.readline()
        if not ready.startswith(b"ready"):
            raise DriverError(f"the foreground sampler did not start: {ready!r}")

    def stop(self):
        import json
        out, _ = self.proc.communicate(b"", timeout=30)  # closing stdin stops it
        result = json.loads(out.decode("utf-8"))
        for key, value in result.items():
            setattr(self, key, value)


def _sample_foreground(exe: str, interval_s: float):
    """The loop of ForegroundSampler (python app_driver.py --sample-foreground
    EXE INTERVAL); stops when stdin is closed and prints the result as JSON."""
    import json
    target = os.path.normcase(os.path.abspath(exe))
    images = {}          # pid -> program path (normcase); filled by resolve()
    pending = set()      # pids whose program path is not known yet
    seen = set()         # geditor windows of `exe` (the control that the path matches)
    lock = threading.Lock()
    stop = threading.Event()

    def wait_for_eof():
        sys.stdin.buffer.read()
        stop.set()

    def pid_of(hwnd) -> int:
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        return pid.value

    def resolve():
        # Opening a process that has just started can take hundreds of
        # milliseconds (measured), so it is done here and never in the loop
        # that samples.
        with lock:
            todo = list(pending)
        for pid in todo:
            image = os.path.normcase(process_image(pid))
            with lock:
                images[pid] = image
                pending.discard(pid)

    def resolver():
        while not stop.is_set():
            window = None
            windows = []
            while True:
                window = user32.FindWindowExW(None, window, "geditor.main", None)
                if not window:
                    break
                windows.append((window, pid_of(window)))
            with lock:
                pending.update(pid for _, pid in windows if pid not in images)
            resolve()
            with lock:
                seen.update(w for w, pid in windows if images.get(pid) == target)
            stop.wait(0.05)

    start = time.monotonic()
    dialogs = {}         # (hwnd, pid) -> first time
    dialog_scans = {"count": 0, "max_gap_s": 0.0, "window_checks": 0}
    on_screen = []       # [time, hwnd, pid, rect, iconic] of a headless window on a monitor

    def dialog_scanner():
        # Enumerating the #32770 windows sometimes waits a few hundred
        # milliseconds while another program starts (measured), so it runs
        # here and does not delay the foreground samples.
        last_scan = time.monotonic()
        while not stop.is_set():
            dialog = None
            while True:
                dialog = user32.FindWindowExW(None, dialog, "#32770", None)
                if not dialog:
                    break
                if user32.IsWindowVisible(dialog):
                    key = (dialog, pid_of(dialog))
                    if key not in dialogs:
                        dialogs[key] = round(time.monotonic() - start, 3)
                        with lock:
                            if key[1] not in images:
                                pending.add(key[1])
            # Every headless geditor window (a "geditor.main" tool window)
            # must stay off every monitor and must not be iconic.
            window = None
            while True:
                window = user32.FindWindowExW(None, window, "geditor.main", None)
                if not window:
                    break
                if ex_style(window) & WS_EX_TOOLWINDOW and user32.IsWindowVisible(window):
                    dialog_scans["window_checks"] += 1
                    if on_a_monitor(window):
                        r = window_rect(window)
                        on_screen.append([round(time.monotonic() - start, 3), window, pid_of(window),
                                          [r.left, r.top, r.right, r.bottom], bool(user32.IsIconic(window))])
            now_scan = time.monotonic()
            dialog_scans["count"] += 1
            dialog_scans["max_gap_s"] = max(dialog_scans["max_gap_s"], now_scan - last_scan)
            last_scan = now_scan
            stop.wait(interval_s)

    threading.Thread(target=wait_for_eof, daemon=True).start()
    workers = [threading.Thread(target=resolver, daemon=True), threading.Thread(target=dialog_scanner, daemon=True)]
    for worker in workers:
        worker.start()
    # A short loop every 20 ms: high priority keeps a busy PC from delaying
    # it (HIGH_PRIORITY_CLASS, THREAD_PRIORITY_HIGHEST).
    kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), 0x00000080)
    kernel32.SetThreadPriority(kernel32.GetCurrentThread(), 2)

    foregrounds = {}     # (hwnd, pid) -> [first time, class]
    result = {"samples": 0, "max_gap_s": 0.0, "gaps_over_50ms": 0, "long_gaps": []}
    print("ready", flush=True)
    last = time.monotonic()
    while not stop.is_set():
        stamp = round(time.monotonic() - start, 3)
        foreground = user32.GetForegroundWindow()
        if foreground:
            key = (foreground, pid_of(foreground))
            if key not in foregrounds:
                foregrounds[key] = [stamp, window_class(foreground)]
                with lock:
                    if key[1] not in images:
                        pending.add(key[1])
        result["samples"] += 1
        now = time.monotonic()
        gap = now - last
        last = now
        result["max_gap_s"] = max(result["max_gap_s"], gap)
        if gap > 0.05:
            result["gaps_over_50ms"] += 1
            result["long_gaps"].append([stamp, round(gap * 1000)])  # (time s, gap ms)
        time.sleep(interval_s)
    for worker in workers:
        worker.join(10)
    resolve()
    result["dialog_scans"] = dialog_scans["count"]
    result["dialog_scan_max_gap_s"] = dialog_scans["max_gap_s"]
    result["window_checks"] = dialog_scans["window_checks"]
    result["on_screen"] = on_screen
    result["foreground_hits"] = [[t, hwnd, cls] for (hwnd, pid), (t, cls) in foregrounds.items()
                                 if cls == "geditor.main" or images.get(pid) == target]
    result["dialogs"] = [[t, hwnd, pid] for (hwnd, pid), t in dialogs.items() if images.get(pid) == target]
    result["foreground_windows"] = len(foregrounds)
    result["geditor_windows_seen"] = len(seen)
    print(json.dumps(result), flush=True)


def capture(hwnd, path: pathlib.Path) -> tuple[int, int, bytes]:
    """Captures a window to a PNG; returns (width, height, BGRA bytes).

    geditor windows are asked (WM_COPYDATA) to draw their client area into a
    BMP file, which works on a locked desktop too. Other windows (task
    dialogs) are captured with PrintWindow."""
    if window_class(hwnd) == "geditor.main":
        bmp = path.with_suffix(".bmp")
        bmp.parent.mkdir(parents=True, exist_ok=True)
        name = ctypes.create_unicode_buffer(str(bmp.resolve()))
        data = COPYDATASTRUCT(COPYDATA_CAPTURE, len(str(bmp.resolve())) * 2,
                              ctypes.cast(name, ctypes.c_void_p))
        if not user32.SendMessageW(hwnd, 0x004A, 0, ctypes.addressof(data)):  # WM_COPYDATA
            raise DriverError("geditor did not write the screenshot")
        raw = bmp.read_bytes()
        bmp.unlink()
        offset, = struct.unpack_from("<I", raw, 10)
        width, height = struct.unpack_from("<ii", raw, 18)
        height = -height
        pixels = raw[offset:offset + width * height * 4]
        write_png(path, width, height, pixels)
        return width, height, pixels
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    screen = user32.GetDC(None)
    dc = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    old = gdi32.SelectObject(dc, bitmap)
    user32.PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT)
    info = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    buffer = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(dc, bitmap, 0, height, buffer, ctypes.byref(info), 0)
    gdi32.SelectObject(dc, old)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(dc)
    user32.ReleaseDC(None, screen)
    pixels = buffer.raw
    write_png(path, width, height, pixels)
    return width, height, pixels


def write_png(path: pathlib.Path, width: int, height: int, bgra: bytes):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        row = bgra[y * width * 4:(y + 1) * width * 4]
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows += rgb

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def count_color(width: int, height: int, bgra: bytes, rgb: int,
                box: tuple[int, int, int, int] | None = None) -> int:
    """Counts pixels of colour 0xRRGGBB, optionally inside box (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box or (0, 0, width, height)
    b, g, r = rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF
    count = 0
    for y in range(max(0, y0), min(height, y1)):
        row = bgra[y * width * 4:(y + 1) * width * 4]
        for x in range(max(0, x0), min(width, x1)):
            i = x * 4
            if row[i] == b and row[i + 1] == g and row[i + 2] == r:
                count += 1
    return count


# ---- M3a: scroll bar, DPI and fault injection ----

class SCROLLINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.UINT), ("fMask", wt.UINT), ("nMin", ctypes.c_int),
                ("nMax", ctypes.c_int), ("nPage", wt.UINT), ("nPos", ctypes.c_int),
                ("nTrackPos", ctypes.c_int)]


SB_VERT = 1
SIF_ALL = 0x17
WM_VSCROLL = 0x0115
WM_DPICHANGED = 0x02E0
COPYDATA_FAULT = 0x544C4647  # 'GFLT', see MainWindow WM_COPYDATA

user32.GetScrollInfo.argtypes = [wt.HWND, ctypes.c_int, ctypes.POINTER(SCROLLINFO)]
user32.GetDpiForWindow.argtypes = [wt.HWND]
user32.GetDpiForWindow.restype = wt.UINT
user32.GetWindowDpiAwarenessContext.argtypes = [wt.HWND]
user32.GetWindowDpiAwarenessContext.restype = ctypes.c_void_p
user32.AreDpiAwarenessContextsEqual.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
user32.GetAwarenessFromDpiAwarenessContext.argtypes = [ctypes.c_void_p]
user32.GetAwarenessFromDpiAwarenessContext.restype = ctypes.c_int
user32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.InvalidateRect.argtypes = [wt.HWND, ctypes.c_void_p, wt.BOOL]


def scroll_info(hwnd) -> SCROLLINFO:
    """The vertical scroll bar of a window (works across processes)."""
    si = SCROLLINFO()
    si.cbSize = ctypes.sizeof(SCROLLINFO)
    si.fMask = SIF_ALL
    if not user32.GetScrollInfo(hwnd, SB_VERT, ctypes.byref(si)):
        raise DriverError(f"GetScrollInfo failed (error {ctypes.get_last_error()})")
    return si


def dpi_awareness(hwnd) -> tuple[int, bool]:
    """(DPI_AWARENESS of the window, whether it is Per-Monitor V2)."""
    context = user32.GetWindowDpiAwarenessContext(hwnd)
    awareness = user32.GetAwarenessFromDpiAwarenessContext(context)
    v2 = bool(user32.AreDpiAwarenessContextsEqual(context, ctypes.c_void_p(-4)))
    return awareness, v2


def window_rect(hwnd) -> wt.RECT:
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return rect


def send_dpi_changed(hwnd, dpi: int, rect: wt.RECT):
    """Sends a real WM_DPICHANGED (as when the window moves to a monitor of
    `dpi`) with the suggested rectangle `rect`. Windows copies the RECT that
    lParam points to into the receiving process (measured on Windows 11), so
    a pointer into this process works."""
    suggested = wt.RECT(rect.left, rect.top, rect.right, rect.bottom)
    user32.SendMessageW(hwnd, WM_DPICHANGED, (dpi << 16) | dpi, ctypes.addressof(suggested))


def inject_fault(hwnd, what: str) -> bool:
    """Makes the next render target ("target") or font ("font") creation of
    geditor fail (WM_COPYDATA 'GFLT'); with --headless also the next read
    ("clipboard-read") or write ("clipboard-write") of its clipboard."""
    name = ctypes.create_unicode_buffer(what)
    data = COPYDATASTRUCT(COPYDATA_FAULT, len(what) * 2, ctypes.cast(name, ctypes.c_void_p))
    return bool(user32.SendMessageW(hwnd, 0x004A, 0, ctypes.addressof(data)))


if __name__ == "__main__" and len(sys.argv) == 4 and sys.argv[1] == "--sample-foreground":
    _sample_foreground(sys.argv[2], float(sys.argv[3]))
