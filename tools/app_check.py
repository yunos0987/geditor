"""Real-app checks of geditor M1 (run by hand; Windows only).

Starts build/release/geditor.exe on generated test files, sends keys and
characters, takes screenshots and checks file bytes, the clipboard and the
window title. Every step prints PASS / FAIL / INFO with its evidence and the
screenshot path; look at the PNG files for the visual checks.

Keys go through SendInput when the desktop is unlocked (the geditor window is
brought to the foreground first; nothing is sent while another window is in
front). On a locked desktop they are posted as WM_KEYDOWN / WM_CHAR with the
modifier state set through AttachThreadInput + SetKeyboardState; the report
line "input mode" says which one ran. The clipboard text is saved before and
restored after the run.

  python tools/app_check.py build/release/geditor.exe [--out DIR]
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import pathlib
import stat
import sys
import threading
import time

sys.dont_write_bytecode = True  # no tools/__pycache__ in the work tree
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import app_driver as d  # noqa: E402

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.GlobalAlloc.restype = wt.HGLOBAL
kernel32.GlobalLock.restype = ctypes.c_void_p
kernel32.GlobalLock.argtypes = [wt.HGLOBAL]
kernel32.GlobalUnlock.argtypes = [wt.HGLOBAL]
d.user32.GetClipboardData.restype = wt.HANDLE
d.user32.SetClipboardData.argtypes = [wt.UINT, wt.HANDLE]
d.user32.SetClipboardData.restype = wt.HANDLE
d.user32.SendMessageTimeoutW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM, wt.UINT, wt.UINT,
                                         ctypes.POINTER(ctypes.c_size_t)]
CF_UNICODETEXT = 13

SELECTION = 0x264F78
CURSOR = 0xFFFFFF
MODE_LINE = 0x007ACC

results = []


def record(status: str, name: str, evidence: str):
    results.append((status, name))
    print(f"[{status}] {name}: {evidence}", flush=True)


def check(name: str, ok: bool, evidence: str):
    record("PASS" if ok else "FAIL", name, evidence)


# ---- clipboard ----

def clipboard_text() -> str | None:
    if not d.user32.OpenClipboard(None):
        return None
    try:
        handle = d.user32.GetClipboardData(CF_UNICODETEXT)
        if not handle:
            return None
        p = kernel32.GlobalLock(handle)
        try:
            return ctypes.wstring_at(p)
        finally:
            kernel32.GlobalUnlock(handle)
    finally:
        d.user32.CloseClipboard()


def set_clipboard_text(text: str) -> bool:
    data = ctypes.create_unicode_buffer(text)
    size = ctypes.sizeof(data)
    mem = kernel32.GlobalAlloc(0x0002, size)  # GMEM_MOVEABLE
    p = kernel32.GlobalLock(mem)
    ctypes.memmove(p, data, size)
    kernel32.GlobalUnlock(mem)
    if not d.user32.OpenClipboard(None):
        return False
    try:
        d.user32.EmptyClipboard()
        return bool(d.user32.SetClipboardData(CF_UNICODETEXT, mem))
    finally:
        d.user32.CloseClipboard()


# ---- screenshot analysis ----

def layout(shot):
    """(text_area_bottom, mode_line_top, mode_line_bottom) in pixels."""
    w, h, px = shot
    top = None
    for y in range(h):
        i = y * w * 4 + 4
        if (px[i + 2] << 16 | px[i + 1] << 8 | px[i]) == MODE_LINE:
            top = y
            break
    if top is None:
        return h, h, h
    bottom = top
    while bottom < h:
        i = bottom * w * 4 + 4
        if (px[i + 2] << 16 | px[i + 1] << 8 | px[i]) != MODE_LINE:
            break
        bottom += 1
    return top, top, bottom


def cursor_box(shot, region: str = "text"):
    """Bounding box of cursor-coloured pixels in the text area or echo line."""
    w, h, px = shot
    text_bottom, _, mode_bottom = layout(shot)
    y0, y1 = (0, text_bottom) if region == "text" else (mode_bottom, h)
    xs, ys = [], []
    for y in range(y0, y1):
        row = px[y * w * 4:(y + 1) * w * 4]
        for x in range(w):
            i = x * 4
            if row[i] == 0xFF and row[i + 1] == 0xFF and row[i + 2] == 0xFF:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def count(shot, rgb: int, region: str = "text") -> int:
    w, h, px = shot
    text_bottom, _, mode_bottom = layout(shot)
    box = (0, 0, w, text_bottom) if region == "text" else (0, mode_bottom, w, h)
    return d.count_color(w, h, px, rgb, box)


# ---- the checks ----

def run_checks(exe: pathlib.Path, out: pathlib.Path):
    work = out / "files"
    work.mkdir(parents=True, exist_ok=True)
    a = work / "a.txt"
    a.write_bytes(b"hello\nworld\nthird line\n")
    (work / "other.txt").write_bytes(b"other file\n")
    long_line = "".join(f"{i:04d}-" for i in range(120)) + "END"
    (work / "long.txt").write_bytes(long_line.encode() + b"\n")
    ro = work / "readonly.txt"
    ro.write_bytes(b"keep me\n")
    os.chmod(ro, stat.S_IREAD)

    shots = out / "shots"
    app = d.App(exe, [str(a)], cwd=work)
    try:
        mode = app.use_input()
        record("INFO", "input mode", mode)
        shot = app.capture(shots / "01_open.png")
        check("open file", app.title() == "a.txt - geditor", f"title={app.title()!r} shot=01_open.png")

        # Typing: ASCII as typed keys, Japanese as WM_IME_CHAR and WM_CHAR.
        app.type("ABC ")
        app.post_chars("日本語", d.WM_IME_CHAR)
        app.post_chars("テキスト", d.WM_CHAR)
        app.post_chars("\U0001F600", d.WM_CHAR)  # surrogate pair as two WM_CHAR
        time.sleep(0.3)
        shot = app.capture(shots / "02_typed.png")
        check("title modified marker", app.title() == "a.txt* - geditor", f"title={app.title()!r}")
        record("INFO", "typed text", "see 02_typed.png: line 1 starts with 'ABC 日本語テキスト😀hello'")

        # C-x C-s saves.
        app.keys("C-x C-s")
        time.sleep(0.3)
        expected = "ABC 日本語テキスト\U0001F600hello\nworld\nthird line\n".encode("utf-8")
        data = a.read_bytes()
        check("C-x C-s saves", data == expected, f"bytes={data!r}")
        check("title after save", app.title() == "a.txt - geditor", f"title={app.title()!r}")
        app.capture(shots / "03_saved.png")

        # Cursor movement: the bar moves (bounding box of the cursor colour).
        positions = {}
        for i, key in enumerate(["C-a", "C-e", "C-n", "C-p", "C-a", "C-f", "C-b"]):
            app.keys(key)
            time.sleep(0.15)
            shot = app.capture(shots / f"04_move_{i}_{key}.png")
            positions[f"{i}:{key}"] = cursor_box(shot)
        p = list(positions.values())
        ok = (all(b is not None for b in p) and
              p[1][0] > p[0][0] and p[1][1] == p[0][1] and  # C-e: right, same line
              p[2][1] > p[1][1] and                         # C-n: down
              p[3][1] == p[1][1] and p[3][0] == p[1][0] and  # C-p: back (goal column)
              p[4][0] == p[0][0] and                        # C-a: line start
              p[5][0] > p[4][0] and p[5][1] == p[4][1] and  # C-f: right
              p[6][0] == p[4][0] and p[6][1] == p[4][1])    # C-b: back
        check("C-a/C-e/C-n/C-p/C-f/C-b move the cursor", ok,
              " ".join(f"{k}={v}" for k, v in positions.items()))

        # Region highlight.
        app.keys("C-a C-SPC C-e")
        time.sleep(0.15)
        shot = app.capture(shots / "05_region.png")
        n = count(shot, SELECTION)
        check("C-SPC + movement highlights the region", n > 500, f"selection pixels={n}")
        app.keys("C-g")

        # C-k + C-y through the real clipboard.
        clipboard_ok = bool(d.user32.OpenClipboard(None))
        if clipboard_ok:
            d.user32.CloseClipboard()
        else:
            # Seen on a locked desktop: OpenClipboard fails with error 5 for
            # every program of the session, geditor included.
            record("SKIP", "clipboard checks",
                   f"OpenClipboard failed (error {ctypes.get_last_error()}); clipboard not accessible")
        app.keys("C-n C-a C-k")
        time.sleep(0.4)
        app.capture(shots / "06a_kill.png")
        if clipboard_ok:
            clip = clipboard_text()
            check("C-k writes the clipboard", clip == "world", f"clipboard={clip!r}")
            app.keys("C-k")  # the line end alone (M1 has no kill append)
            time.sleep(0.2)
            clip_nl = clipboard_text()
            check("killed newline is CRLF on the clipboard", clip_nl == "\r\n",
                  f"clipboard={clip_nl!r}")
            set_clipboard_text("world\r\nmore")
            app.keys("C-y")
            time.sleep(0.2)
            app.capture(shots / "06_yank.png")
            app.keys("C-x C-s")
            time.sleep(0.3)
            data = a.read_bytes()
            check("C-y inserts clipboard text with LF", b"\nworld\nmorethird line\n" in data,
                  f"bytes={data!r}")
        else:
            app.keys("C-x C-s")
            time.sleep(0.3)
            data = a.read_bytes()
            check("failed clipboard write keeps the killed text", b"\nworld\n" in data,
                  f"bytes={data!r} (06a_kill.png shows 'Cannot write to the clipboard')")

        # A failed clipboard write keeps the text and shows the message.
        if clipboard_ok and d.user32.OpenClipboard(None):
            try:
                app.keys("C-a C-k")
                time.sleep(0.4)
                shot = app.capture(shots / "07_clipboard_fail.png")
            finally:
                d.user32.CloseClipboard()
            app.keys("C-x C-s")
            time.sleep(0.3)
            record("INFO", "clipboard held by another program",
                   "see 07_clipboard_fail.png: echo area 'Cannot write to the clipboard'")
            check("failed clipboard write keeps the text", a.read_bytes() == data,
                  f"bytes={a.read_bytes()!r}")

        # C-x wait 1 s -> "C-x-".
        app.keys("C-x")
        time.sleep(0.4)
        early = app.capture(shots / "08_cx_early.png")
        time.sleep(1.0)
        late = app.capture(shots / "09_cx_late.png")
        n_early = count(early, 0xD4D4D4, "echo")
        n_late = count(late, 0xD4D4D4, "echo")
        check("C-x echoes after 1 s", n_early == 0 and n_late > 0,
              f"echo text pixels at 0.4s={n_early}, at 1.4s={n_late} (09_cx_late.png shows C-x-)")
        app.keys("C-g")

        # C-x C-f: prompt in the minibuffer, Tab completes.
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.capture(shots / "10_find_file_prompt.png")
        app.type("oth")
        app.keys("TAB")
        time.sleep(0.2)
        shot = app.capture(shots / "11_find_file_tab.png")
        record("INFO", "C-x C-f prompt and Tab",
               "see 10_find_file_prompt.png ('Find file: <dir>\\') and 11_find_file_tab.png (other.txt)")
        check("minibuffer cursor in the echo line", cursor_box(shot, "echo") is not None,
              f"echo cursor box={cursor_box(shot, 'echo')}")

        # Opening another file shows the 3-button dialog.
        app.type("x")  # edits the minibuffer input, then DEL removes it again
        app.keys("DEL RET")
        dialog = app.wait_dialog()
        check("open dialog appears", dialog is not None, f"dialog={dialog}")
        if dialog:
            d.capture(dialog, shots / "12_open_dialog.png")
            before = set(d.windows_of_class("geditor.main"))
            d.click_dialog_button(dialog, 102)  # Open in a new window
            new = d.wait_for(lambda: [h for h in d.windows_of_class("geditor.main") if h not in before], 10.0)
            titles = [d.window_text(h) for h in new or []]
            check("Open in a new window starts another geditor",
                  bool(new) and titles[0] == "other.txt - geditor", f"titles={titles}")
            for h in new or []:
                d.user32.PostMessageW(h, d.WM_CLOSE, 0, 0)  # unmodified: closes
            # The new window took the foreground; SendInput needs it back.
            d.wait_for(lambda: not any(d.user32.IsWindow(h) for h in new or []), 5.0)
            app.refocus()

        # Failed save on a read-only file keeps the buffer modified.
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.type(ro.name)  # the prompt starts with the directory of a.txt
        app.keys("RET")
        dialog = app.wait_dialog(3.0)
        if dialog:
            d.click_dialog_button(dialog, 101)  # Open in this window
            time.sleep(0.5)
        check("opened the read-only file", app.title() == "readonly.txt - geditor", f"title={app.title()!r}")
        app.type("X")
        app.keys("C-x C-s")
        time.sleep(0.3)
        app.capture(shots / "13_save_failed.png")
        check("failed save keeps the buffer modified",
              app.title() == "readonly.txt* - geditor" and ro.read_bytes() == b"keep me\n",
              f"title={app.title()!r} bytes={ro.read_bytes()!r} (13_save_failed.png shows the message)")

        # Closing a modified buffer shows the save dialog; Cancel keeps it.
        app.close()
        dialog = app.wait_dialog()
        check("save dialog on close", dialog is not None, f"dialog={dialog}")
        if dialog:
            d.capture(dialog, shots / "14_save_dialog.png")
            d.click_dialog_button(dialog, 2)  # IDCANCEL
            time.sleep(0.3)
        check("Cancel keeps the window", app.alive(), f"alive={app.alive()}")
        app.keys("C-/")  # undo the X: unmodified again
        time.sleep(0.2)
    finally:
        app.kill()
        os.chmod(ro, stat.S_IREAD | stat.S_IWRITE)

    # Horizontal scrolling of a long line, Alt alone, M-<f4>.
    app = d.App(exe, [str(work / "long.txt")], cwd=work)
    try:
        app.use_input()
        app.keys("C-e")
        time.sleep(0.2)
        shot = app.capture(shots / "15_long_line_end.png")
        box = cursor_box(shot)
        check("long line scrolls to keep the cursor visible", box is not None and box[0] < shot[0],
              f"cursor box={box} width={shot[0]} (15_long_line_end.png shows ...END)")
        app.keys("C-a")
        time.sleep(0.2)
        app.capture(shots / "16_long_line_start.png")

        # Alt alone must not enter a menu: the next character is still typed.
        if app.mode == "sendinput":
            app._require_foreground()
            d._send([d._key_input(d.VK_MENU, False), d._key_input(d.VK_MENU, True)])
        else:
            app._post_key_raw(d.VK_MENU)
        time.sleep(0.3)
        app.type("q")
        time.sleep(0.2)
        app.capture(shots / "17_after_alt.png")
        check("Alt alone does not enter a menu", app.title() == "long.txt* - geditor",
              f"title={app.title()!r} (a 'q' typed after Alt reached the buffer)")
        app.keys("C-/")
        time.sleep(0.2)
        app.keys("M-<f4>")
        check("M-<f4> closes the window", app.wait_exit(5.0), f"alive={app.alive()}")
    finally:
        app.kill()


def long_path(path: pathlib.Path) -> str:
    """The "\\\\?\\" form of an absolute path, for Python file calls on long paths."""
    return "\\\\?\\" + str(path)


def send_message_async(hwnd, msg: int, wparam: int = 0, lparam: int = 0) -> dict:
    """Sends a message from a thread (the reply may wait for a dialog)."""
    result = {}

    def worker():
        value = ctypes.c_size_t(0)
        ok = d.user32.SendMessageTimeoutW(wt.HWND(hwnd), msg, wparam, lparam, 0, 20000,
                                          ctypes.byref(value))
        result["ok"] = bool(ok)
        result["value"] = value.value

    thread = threading.Thread(target=worker)
    thread.start()
    result["thread"] = thread
    return result


def run_retry1_checks(exe: pathlib.Path, out: pathlib.Path):
    """Checks added in retry 1 ([S1]-[S5] of the M1b packet)."""
    work = out / "files_retry1"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"

    # [S1] A file deeper than 260 characters opens, saves and completes.
    deep = work
    level = 0
    while len(str(deep)) < 420:
        deep = deep / f"deep_directory_level_{level:02d}"
        level += 1
    os.makedirs(long_path(deep), exist_ok=True)
    deep_file = deep / "deep_file.txt"
    pathlib.Path(long_path(deep_file)).write_bytes(b"deep file\n")
    pathlib.Path(long_path(deep / "zz_other.txt")).write_bytes(b"other\n")
    ro_file = deep / "readonly_deep.txt"
    pathlib.Path(long_path(ro_file)).write_bytes(b"read only\n")
    os.chmod(long_path(ro_file), stat.S_IREAD)
    record("INFO", "long path", f"{len(str(deep_file))} characters")
    app = d.App(exe, [str(deep_file)], cwd=work)
    try:
        app.use_input()
        shot = app.capture(shots / "20_long_path_open.png")
        # "(New file)" would leave the text area empty: count text pixels.
        text_pixels = count(shot, 0xD4D4D4)
        check("[S1] long path opens the existing file",
              app.title() == "deep_file.txt - geditor" and text_pixels > 20,
              f"title={app.title()!r} text pixels={text_pixels} "
              "(20_long_path_open.png shows 'deep file', not '(New file)')")
        app.type("X")
        app.keys("C-x C-s")
        time.sleep(0.3)
        data = pathlib.Path(long_path(deep_file)).read_bytes()
        check("[S1] long path saves", data == b"Xdeep file\n", f"bytes={data!r}")

        # [S3a] The prompt stays visible with a long directory; Tab completes.
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.type("zz_")
        app.keys("TAB")
        time.sleep(0.2)
        app.capture(shots / "21_long_prompt_tab.png")
        record("INFO", "[S3a] long prompt",
               "see 21_long_prompt_tab.png: 'Find file: …\\zz_other.txt' with the cursor at the end")
        app.keys("RET")
        dialog = app.wait_dialog()
        new = []
        if dialog:
            before = set(d.windows_of_class("geditor.main"))
            d.click_dialog_button(dialog, 102)  # Open in a new window
            new = d.wait_for(lambda: [h for h in d.windows_of_class("geditor.main")
                                      if h not in before], 10.0) or []
        titles = [d.window_text(h) for h in new]
        check("[S1] Tab completion in a long directory", titles == ["zz_other.txt - geditor"],
              f"titles={titles}")
        for h in new:
            d.user32.PostMessageW(h, d.WM_CLOSE, 0, 0)
        d.wait_for(lambda: not any(d.user32.IsWindow(h) for h in new), 5.0)
        app.refocus()

        # [S3b] A failed save in a long directory keeps the reason readable.
        app.keys("C-x C-w")
        time.sleep(0.2)
        app.type(ro_file.name)
        app.keys("RET")
        dialog = app.wait_dialog(3.0)
        if dialog:
            d.click_dialog_button(dialog, 6)  # IDYES: overwrite
            time.sleep(0.4)
        app.capture(shots / "22_long_path_save_failed.png")
        record("INFO", "[S3b] long failed-save message",
               "see 22_long_path_save_failed.png: 'Cannot write C:\\…\\readonly_deep.txt: Access is denied'")

        # [S6] On a locked desktop the clipboard cannot be opened: C-y says so.
        if not clipboard_accessible():
            app.keys("C-y")
            time.sleep(0.3)
            app.capture(shots / "23_clipboard_read_failed.png")
            record("INFO", "[S6] clipboard read failure",
                   "see 23_clipboard_read_failed.png: 'Cannot read the clipboard'")
    finally:
        app.kill()
        os.chmod(long_path(ro_file), stat.S_IREAD | stat.S_IWRITE)

    # [S2] Minimise / restore keeps the view.
    lines_file = work / "lines200.txt"
    lines_file.write_bytes("".join(f"line {i:03d}\n" for i in range(1, 201)).encode())
    app = d.App(exe, ["+50", str(lines_file)], cwd=work)
    try:
        app.use_input()
        app.keys("C-p C-p C-p C-p C-p")
        time.sleep(0.2)
        before = app.capture(shots / "24_before_minimize.png")
        d.user32.ShowWindow(app.hwnd, 6)  # SW_MINIMIZE
        time.sleep(0.5)
        d.user32.ShowWindow(app.hwnd, 9)  # SW_RESTORE
        time.sleep(0.5)
        after = app.capture(shots / "25_after_restore.png")
        check("[S2] minimise/restore keeps the view", before[2] == after[2],
              "24_before_minimize.png and 25_after_restore.png are " +
              ("identical" if before[2] == after[2] else "different"))
    finally:
        app.kill()

    # [S4] Combining marks and invisible format characters.
    marks = work / "marks.txt"
    marks.write_bytes("ab cd ef gh\na\u200bb c\ufeffd e\u2060f g\ufe0fh\nか\u3099 e\u0301 x\ufe0fy\n\n"
                      .encode("utf-8"))
    app = d.App(exe, ["+4", str(marks)], cwd=work)
    try:
        app.use_input()
        w, h, px = app.capture(shots / "26_marks.png")
        text_bottom, _, mode_bottom = layout((w, h, px))
        line_px = mode_bottom - text_bottom  # the mode line is one row high
        first = px[0:line_px * w * 4]
        second = px[line_px * w * 4:2 * line_px * w * 4]
        check("[S4] invisible format characters are not drawn", first == second,
              "rows 1 ('ab cd ef gh') and 2 (with U+200B, U+FEFF, U+2060, U+FE0F) are " +
              ("identical" if first == second else "different") + " (26_marks.png)")
        record("INFO", "[S4] combining marks",
               "see 26_marks.png row 3: 'が' (か + U+3099) and 'é' (e + U+0301) drawn on one cell group")
    finally:
        app.kill()

    # [S5] Session end asks to save; Cancel blocks, Don't Save allows.
    app = d.App(exe, [str(lines_file)], cwd=work)
    try:
        app.use_input()
        app.type("Z")
        time.sleep(0.2)
        reply = send_message_async(app.hwnd, 0x0011)  # WM_QUERYENDSESSION
        dialog = app.wait_dialog(5.0)
        if dialog:
            d.capture(dialog, shots / "27_session_end_dialog.png")
            d.click_dialog_button(dialog, 2)  # Cancel
        reply["thread"].join(25)
        check("[S5] session end with Cancel is blocked", dialog is not None and reply.get("value") == 0,
              f"dialog={dialog} reply={reply.get('value')} (27_session_end_dialog.png)")
        reply = send_message_async(app.hwnd, 0x0011)
        dialog = app.wait_dialog(5.0)
        if dialog:
            d.click_dialog_button(dialog, 202)  # Don't Save
        reply["thread"].join(25)
        check("[S5] session end with Don't Save is allowed", dialog is not None and reply.get("value") == 1,
              f"dialog={dialog} reply={reply.get('value')}")
    finally:
        app.kill()


def clipboard_accessible() -> bool:
    if d.user32.OpenClipboard(None):
        d.user32.CloseClipboard()
        return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path,
                        default=pathlib.Path("build") / "app_check" / time.strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--strict", action="store_true", help="exit 1 when any check is SKIP")
    parser.add_argument("--only-retry1", action="store_true",
                        help="run only the checks added in retry 1 ([S1]-[S5])")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    saved = clipboard_text()
    try:
        if not args.only_retry1:
            run_checks(args.exe.resolve(), args.out.resolve())
        run_retry1_checks(args.exe.resolve(), args.out.resolve())
    except d.DriverError as exc:
        record("FAIL", "driver", str(exc))
    finally:
        if saved is not None:
            set_clipboard_text(saved)
    passed = sum(1 for s, _ in results if s == "PASS")
    failed = sum(1 for s, _ in results if s == "FAIL")
    skipped = sum(1 for s, _ in results if s == "SKIP")
    print(f"output: {args.out.resolve()}")
    print(f"{passed} passed, {failed} failed, {skipped} skipped")
    if failed:
        return 1
    if skipped and args.strict:
        print("--strict: skipped checks count as failures")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
