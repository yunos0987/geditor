"""Drive a running geditor window for real-app checks (Windows only).

Used by tools/app_check.py. Keys are sent with SendInput, which only reaches
the foreground window: every send first checks that the geditor window is in
the foreground and raises otherwise, so that keystrokes never go to another
program. On a locked desktop keys are posted instead (see App.use_input).
geditor windows are captured by asking geditor to draw into a BMP
(WM_COPYDATA); other windows with PrintWindow. Images are written as PNG.
"""

import ctypes
import ctypes.wintypes as wt
import importlib.util
import pathlib
import struct
import subprocess
import sys
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

user32 = ctypes.WinDLL("user32", use_last_error=True)
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


class DriverError(RuntimeError):
    pass


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
    user's %USERPROFILE%\\.geditor.json (the file need not exist)."""

    def __init__(self, exe: pathlib.Path, args: list[str], cwd: pathlib.Path | None = None,
                 width: int = 900, height: int = 500, settings: pathlib.Path | None = None):
        if settings is not None:
            args = [f"--settings={settings}", *args]
        self.proc = subprocess.Popen([str(exe), *args], cwd=cwd)
        windows = wait_for(lambda: windows_of(self.proc.pid, "geditor.main"), 15.0)
        if not windows:
            self.kill()
            raise DriverError("geditor window did not appear")
        self.hwnd = windows[0]
        # A fixed size keeps screenshots small and comparable.
        user32.MoveWindow(self.hwnd, 100, 100, width, height, True)
        time.sleep(0.3)

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
        brought to the foreground (an unlocked desktop), else "post"."""
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
