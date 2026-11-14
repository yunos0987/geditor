"""Real-app checks of geditor M1, M2 and M3a (run by hand; Windows only).

Starts build/release/geditor.exe on generated test files, sends keys and
characters, takes screenshots and checks file bytes, the clipboard and the
window title. Every step prints PASS / FAIL / INFO with its evidence and the
screenshot path; look at the PNG files for the visual checks.

Every geditor started here gets --settings=PATH with a file under the output
folder, so the user's %USERPROFILE%\\.geditor.json is never read or written.
The M2 checks rewrite that file while geditor runs (live reload, syntax
error, expanded echo area, removal, a locked file, find-user-init-file) and
start geditor with a settings file in a missing folder (watcher failure).
The M3a checks change each display setting while geditor runs and look at
the pixels (a vivid colour scheme tells the colours apart), run the View
commands from M-x, text-scale, send WM_VSCROLL and a real WM_DPICHANGED
(Windows copies its RECT into geditor), make a font / render target
creation fail on purpose (WM_COPYDATA 'GFLT') and print the [C4] check
table (item | screenshot | what is seen).

Headless (the default; the line "mode headless" says so): every geditor
gets --headless. Its window is outside every monitor and never activated,
keys are posted as WM_KEYDOWN / WM_CHAR with the modifier state set through
AttachThreadInput + SetKeyboardState, the clipboard checks use geditor's
private clipboard (read through WM_COPYDATA 'GSTA', written with 'GCBW',
failures forced with 'GFLT'), and the questions are read and answered
through 'GSTA' / 'GANS' instead of task dialogs. The real clipboard is
never opened (its sequence number is compared before and after the run), a
sampler checks every 20 ms that no geditor window is the foreground and
that no geditor task dialog appears, and app_driver refuses the calls that
would take the user's screen, keyboard or clipboard. The checks that need
the real screen are reported as SKIP "needs --visible"; --strict does not
count them. The headless checks of the mode itself (window, guard,
questions, private clipboard, bad requests, measurement scripts) run in the
full run and with --only-headless.

--visible runs the present real-screen paths: keys go through SendInput
when the desktop is unlocked (the geditor window is brought to the
foreground first; nothing is sent while another window is in front), on a
locked desktop or with --input post they are posted (the line "input mode"
says which one ran), the real clipboard and task dialogs are used, and the
clipboard text is saved before and restored after the run. --visible
--only-visible runs only the checks that need the real screen.

  python tools/app_check.py build/release/geditor.exe [--out DIR] [--visible]
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import pathlib
import stat
import subprocess
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
# Names of the SKIP lines that only a --visible run can check.
visible_only = set()


def record(status: str, name: str, evidence: str):
    results.append((status, name))
    print(f"[{status}] {name}: {evidence}", flush=True)


def check(name: str, ok: bool, evidence: str):
    record("PASS" if ok else "FAIL", name, evidence)


def needs_visible(name: str, reason: str):
    """A check that a headless run cannot do (--strict does not count it)."""
    visible_only.add(name)
    record("SKIP", name, f"needs --visible: {reason}")


# Task dialog button ids of the labels (src/win/question.h).
BUTTON_IDS = {"Open in this window": 101, "Open in a new window": 102, "Cancel": 2,
              "Save": 201, "Don't Save": 202, "Yes": 6, "No": 7}


def answer(app, label: str, shot: pathlib.Path | None = None, timeout_s: float = 5.0) -> dict | None:
    """Waits for the question of `app` and presses `label`; None when no
    question came. Headless: the question channel (the returned dict holds
    its kind, text and buttons). Visible: the task dialog, captured to
    `shot` first."""
    if d.HEADLESS:
        question = app.wait_question(timeout_s)
        if question is None:
            return None
        if not app.answer(label):
            raise d.DriverError(f"geditor rejected the answer {label!r}: {app.state().get('last_rejected')}")
        return question
    dialog = app.wait_dialog(timeout_s)
    if not dialog:
        return None
    if shot is not None:
        d.capture(dialog, shot)
    d.click_dialog_button(dialog, BUTTON_IDS[label])
    return {"question": "dialog", "hwnd": dialog}


def describe(question: dict | None) -> str:
    if not question or question.get("question") == "dialog":
        return f"dialog={question and question.get('hwnd')}"
    return (f"question={question.get('question')} instruction={question.get('instruction')!r} "
            f"content={question.get('content')!r} buttons={question.get('button')} "
            f"default={question.get('default')!r}")


def clipboard_read(app) -> str | None:
    return app.clipboard() if d.HEADLESS else clipboard_text()


def clipboard_write(app, text: str) -> bool:
    return app.set_clipboard(text) if d.HEADLESS else set_clipboard_text(text)


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
    settings = no_settings(out)
    app = d.App(exe, [str(a)], cwd=work, settings=settings)
    try:
        mode = app.use_input()
        record("INFO", "input mode", mode)
        if d.HEADLESS:
            needs_visible("[visible] keys through SendInput to the foreground window",
                          "a headless window never takes the foreground; keys are posted")
            needs_visible("[visible] task dialogs: their look and their buttons",
                          "headless questions show no dialog (12_open_dialog / 14_save_dialog / "
                          "27_session_end_dialog / yes-no screenshots)")
            needs_visible("[visible] the real clipboard: C-k / C-y through CF_UNICODETEXT",
                          "a headless run never opens the real clipboard")
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

        # C-k + C-y through the real clipboard (headless: the private one).
        clipboard_ok = d.HEADLESS or clipboard_accessible()
        if not clipboard_ok:
            # Seen on a locked desktop: OpenClipboard fails with error 5 for
            # every program of the session, geditor included.
            record("SKIP", "clipboard checks",
                   f"OpenClipboard failed (error {ctypes.get_last_error()}); clipboard not accessible")
        app.keys("C-n C-a C-k")
        time.sleep(0.4)
        app.capture(shots / "06a_kill.png")
        if clipboard_ok:
            clip = clipboard_read(app)
            check("C-k writes the clipboard", clip == "world", f"clipboard={clip!r}")
            app.keys("C-k")  # the line end alone (M1 has no kill append)
            time.sleep(0.2)
            clip_nl = clipboard_read(app)
            check("killed newline is CRLF on the clipboard", clip_nl == "\r\n",
                  f"clipboard={clip_nl!r}")
            clipboard_write(app, "world\r\nmore")
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
        if d.HEADLESS:
            # H3: the failure is forced ('GFLT'); the real clipboard is not
            # held by this script.
            app.fault("clipboard-write")
            app.keys("C-a C-k")
            time.sleep(0.4)
            app.capture(shots / "07_clipboard_fail.png")
            message = app.state().get("message", "")
            app.keys("C-x C-s")
            time.sleep(0.3)
            check("[H3] forced clipboard write failure says so", message == "Cannot write to the clipboard",
                  f"message={message!r} (07_clipboard_fail.png)")
            check("failed clipboard write keeps the text", a.read_bytes() == data,
                  f"bytes={a.read_bytes()!r}")
            needs_visible("[visible] clipboard held open by another program",
                          "the real clipboard is never opened in a headless run")
        elif clipboard_ok and d.user32.OpenClipboard(None):
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
        before = set(d.started_windows())
        question = answer(app, "Open in a new window", shots / "12_open_dialog.png")
        check("open dialog appears", question is not None, describe(question))
        if d.HEADLESS and question is not None:
            check("[H4] open_choice: kind, text and buttons as the task dialog shows them",
                  question.get("question") == "open_choice" and question.get("instruction") == "Open other.txt?"
                  and question.get("content") == str(work / "other.txt") and
                  question.get("button") == ["Open in this window", "Open in a new window", "Cancel"] and
                  question.get("default") == "Open in this window", describe(question))
        if question:
            new = d.wait_for(lambda: [h for h in d.started_windows() if h not in before], 10.0)
            titles = [d.window_text(h) for h in new or []]
            check("Open in a new window starts another geditor",
                  bool(new) and titles[0] == "other.txt - geditor", f"titles={titles} (answer 'Open in a new window')")
            if d.HEADLESS and new:
                check_headless_new_window(app, new[0])
            for h in new or []:
                d.user32.PostMessageW(h, d.WM_CLOSE, 0, 0)  # unmodified: closes
            # The new window took the foreground; SendInput needs it back.
            closed = d.wait_for(lambda: not any(d.user32.IsWindow(h) for h in new or []), 5.0)
            if d.HEADLESS:
                check("[C6] the check closes the new window", bool(closed) and bool(new),
                      f"windows {new} closed={bool(closed)}")
            app.refocus()

        # Failed save on a read-only file keeps the buffer modified.
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.type(ro.name)  # the prompt starts with the directory of a.txt
        app.keys("RET")
        question = answer(app, "Open in this window", timeout_s=3.0)
        if question:
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
        if d.HEADLESS:
            # H4 negative: while the question waits, keys do not edit and
            # WM_CLOSE waits for the answer.
            pending = app.wait_question()
            length0 = pending and pending.get("length")
            app.type("QQ")
            app.keys("C-a C-k")
            app.close()
            time.sleep(0.3)
            later = app.state()
            check("[H4] while a question is pending, posted keys do not change the buffer",
                  pending is not None and later.get("length") == length0 and later.get("question") == "save_changes",
                  f"length {length0} -> {later.get('length')}, question={later.get('question')}, "
                  f"WM_CLOSE rejected: {later.get('last_rejected')!r}")
        question = answer(app, "Cancel", shots / "14_save_dialog.png")
        time.sleep(0.3)
        check("save dialog on close", question is not None, describe(question))
        if d.HEADLESS and question is not None:
            check("[H4] save_changes: kind, text and buttons as the task dialog shows them",
                  question.get("question") == "save_changes" and
                  question.get("instruction") == "Save changes to readonly.txt?" and
                  question.get("content") == "" and question.get("button") == ["Save", "Don't Save", "Cancel"] and
                  question.get("default") == "Save", describe(question))
        check("Cancel keeps the window", app.alive(), f"alive={app.alive()} (answer 'Cancel')")
        app.keys("C-/")  # undo the X: unmodified again
        time.sleep(0.2)
    finally:
        app.kill()
        os.chmod(ro, stat.S_IREAD | stat.S_IWRITE)

    # Horizontal scrolling of a long line, Alt alone, M-<f4>.
    app = d.App(exe, [str(work / "long.txt")], cwd=work, settings=settings)
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
        if d.HEADLESS:
            # Measured: the posted M-<f4> leaves a headless window open, while
            # the --input post run of 2026-11-13 (a window shown normally)
            # closed. The system's Alt+F4 acts on the active window.
            needs_visible("M-<f4> closes the window",
                          "geditor passes an unbound M-<f4> to the system, whose Alt+F4 closes the "
                          "active window; a headless window is never active")
        else:
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
    settings = no_settings(out)
    app = d.App(exe, [str(deep_file)], cwd=work, settings=settings)
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
        new = []
        before = set(d.started_windows())
        if answer(app, "Open in a new window"):
            new = d.wait_for(lambda: [h for h in d.started_windows()
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
        question = answer(app, "Yes", timeout_s=3.0)  # overwrite
        if question:
            time.sleep(0.4)
        app.capture(shots / "22_long_path_save_failed.png")
        record("INFO", "[S3b] long failed-save message",
               "see 22_long_path_save_failed.png: 'Cannot write C:\\…\\readonly_deep.txt: Access is denied'")
        if d.HEADLESS:
            message = app.state().get("message", "")
            check("[S3b] answer 'Yes' to overwrite; the failed save says why",
                  question is not None and question.get("question") == "yes_no" and
                  message.startswith("Cannot write ") and "readonly_deep.txt" in message,
                  f"{describe(question)} message={message!r}")

        # [S6] On a locked desktop the clipboard cannot be opened: C-y says so.
        # Headless: the failure is forced ('GFLT').
        if d.HEADLESS:
            app.fault("clipboard-read")
            app.keys("C-y")
            time.sleep(0.3)
            app.capture(shots / "23_clipboard_read_failed.png")
            message = app.state().get("message", "")
            check("[S6] [H3] forced clipboard read failure says so", message == "Cannot read the clipboard",
                  f"message={message!r} (23_clipboard_read_failed.png)")
        elif not clipboard_accessible():
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
    app = d.App(exe, ["+50", str(lines_file)], cwd=work, settings=settings)
    try:
        app.use_input()
        app.keys("C-p C-p C-p C-p C-p")
        time.sleep(0.2)
        before = app.capture(shots / "24_before_minimize.png")
        if d.HEADLESS:
            # Headless: never really minimised (Windows parks a minimised tool
            # window as a caption bar on the primary monitor). geditor's
            # WM_SIZE handling gets the messages of a minimise and a restore:
            # SIZE_MINIMIZED with a 0 x 0 client, then SIZE_RESTORED with the
            # old client size. (IsIconic stays false, so the fallback of
            # visible_lines for an iconic window is checked only by --visible.)
            client = d.client_size(app.hwnd)
            d.user32.SendMessageW(app.hwnd, d.WM_SIZE, d.SIZE_MINIMIZED, 0)
            time.sleep(0.5)
            d.user32.SendMessageW(app.hwnd, d.WM_SIZE, d.SIZE_RESTORED, (client[1] << 16) | client[0])
            time.sleep(0.5)
            how = f"synthetic WM_SIZE SIZE_MINIMIZED, then SIZE_RESTORED {client[0]}x{client[1]}"
        else:
            d.user32.ShowWindow(app.hwnd, 6)  # SW_MINIMIZE
            time.sleep(0.5)
            d.user32.ShowWindow(app.hwnd, 9)  # SW_RESTORE
            time.sleep(0.5)
            how = "SW_MINIMIZE, then SW_RESTORE"
        after = app.capture(shots / "25_after_restore.png")
        check("[S2] minimise/restore keeps the view", before[2] == after[2],
              f"{how}: 24_before_minimize.png and 25_after_restore.png are " +
              ("identical" if before[2] == after[2] else "different"))
        if d.HEADLESS:
            check_offscreen(app, "[S2] after the synthetic minimise and restore")
    finally:
        app.kill()

    # [S4] Combining marks and invisible format characters.
    marks = work / "marks.txt"
    marks.write_bytes("ab cd ef gh\na\u200bb c\ufeffd e\u2060f g\ufe0fh\nか\u3099 e\u0301 x\ufe0fy\n\n"
                      .encode("utf-8"))
    app = d.App(exe, ["+4", str(marks)], cwd=work, settings=settings)
    try:
        app.use_input()
        w, h, px = app.capture(shots / "26_marks.png")
        text_bottom, _, mode_bottom = layout((w, h, px))
        line_px = mode_bottom - text_bottom  # the mode line is one row high
        # M3a: the line numbers differ; compare the text right of the gutter
        # (the cursor of the empty line 4 is at the text's left edge).
        start = cursor_box((w, h, px))
        x0 = start[0] if start else 0

        def row_text(r):
            return b"".join(px[(y * w + x0) * 4:(y + 1) * w * 4] for y in range(r * line_px, (r + 1) * line_px))

        first = row_text(0)
        second = row_text(1)
        check("[S4] invisible format characters are not drawn", first == second,
              "rows 1 ('ab cd ef gh') and 2 (with U+200B, U+FEFF, U+2060, U+FE0F) are " +
              ("identical" if first == second else "different") + f" right of x={x0} (26_marks.png)")
        record("INFO", "[S4] combining marks",
               "see 26_marks.png row 3: 'が' (か + U+3099) and 'é' (e + U+0301) drawn on one cell group")
    finally:
        app.kill()

    # [S5] Session end asks to save; Cancel blocks, Don't Save allows.
    app = d.App(exe, [str(lines_file)], cwd=work, settings=settings)
    try:
        app.use_input()
        app.type("Z")
        time.sleep(0.2)
        reply = send_message_async(app.hwnd, 0x0011)  # WM_QUERYENDSESSION
        question = answer(app, "Cancel", shots / "27_session_end_dialog.png")
        reply["thread"].join(25)
        check("[S5] session end with Cancel is blocked", question is not None and reply.get("value") == 0,
              f"{describe(question)} reply={reply.get('value')} (27_session_end_dialog.png)")
        reply = send_message_async(app.hwnd, 0x0011)
        question = answer(app, "Don't Save")
        reply["thread"].join(25)
        check("[S5] session end with Don't Save is allowed", question is not None and reply.get("value") == 1,
              f"{describe(question)} reply={reply.get('value')}")
    finally:
        app.kill()


def no_settings(out: pathlib.Path) -> pathlib.Path:
    """A settings path under `out` whose file does not exist (built-in
    defaults); its folder exists so that geditor can watch it."""
    folder = out / "settings"
    folder.mkdir(parents=True, exist_ok=True)
    return folder / "none.json"


# ---- M2: settings ----

GENERIC_WRITE = 0x40000000
CREATE_ALWAYS = 2
INVALID_HANDLE = ctypes.c_void_p(-1).value
kernel32.CreateFileW.restype = wt.HANDLE
kernel32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD,
                                 wt.DWORD, wt.HANDLE]
kernel32.WriteFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD),
                               ctypes.c_void_p]
kernel32.CloseHandle.argtypes = [wt.HANDLE]


def write_locked(path: pathlib.Path, data: bytes):
    """Writes `data` to `path` through a handle that shares nothing and
    returns the open handle (other programs get a sharing violation until
    it is closed)."""
    handle = kernel32.CreateFileW(str(path), GENERIC_WRITE, 0, None, CREATE_ALWAYS, 0x80, None)
    if handle == INVALID_HANDLE or handle is None:
        raise d.DriverError(f"cannot open {path} exclusively (error {ctypes.get_last_error()})")
    written = wt.DWORD(0)
    kernel32.WriteFile(handle, data, len(data), ctypes.byref(written), None)
    return handle


def mode_line_rows(shot) -> tuple[int, int]:
    """(top of the mode line, its height) in pixels."""
    _, top, bottom = layout(shot)
    return top, bottom - top


def echo_pixels(shot) -> int:
    return count(shot, 0xD4D4D4, "echo")


def press_until_modified(app, key: str, timeout_s: float) -> tuple[bool, float, int]:
    """Presses `key` until the title shows the modified marker; returns
    (modified, seconds from the call, presses)."""
    start = time.monotonic()
    presses = 0
    while time.monotonic() - start < timeout_s:
        app.keys(key, pause_s=0.02)
        presses += 1
        time.sleep(0.03)
        if app.title().endswith("* - geditor"):
            return True, time.monotonic() - start, presses
    return False, time.monotonic() - start, presses


def run_m2_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C6], [C8] and [C14] of the M2 packet on the real app."""
    work = out / "files_m2"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    settings.write_bytes(b"{}\n")
    doc = work / "m2.txt"
    doc.write_bytes(b"line one\nline two\n")
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    try:
        record("INFO", "M2 input mode", app.use_input())
        time.sleep(0.5)  # the watcher starts after the first frame
        baseline = app.capture(shots / "m2_00_start.png")
        top0, line_px = mode_line_rows(baseline)

        # [C6] Rewriting the file binds <f5>; measure until the key works.
        settings.write_bytes(b'{\n  "keys": { "<f5>": ["insert-text", "M2OK"] }\n}\n')
        ok, elapsed, presses = press_until_modified(app, "<f5>", 3.0)
        check("[C6] rewritten settings work within 1 s", ok and elapsed <= 1.0,
              f"elapsed={elapsed:.3f}s from the write to the first effective <f5> ({presses} presses)")
        app.keys("C-x C-s")
        time.sleep(0.3)
        data = doc.read_bytes()
        check("[C6] the new binding inserted its text once", data == b"M2OKline one\nline two\n",
              f"bytes={data!r}")

        # [C6] A syntax error keeps the previous binding and shows the line.
        settings.write_bytes(b'{\n  "keys": { "<f5>": ["insert-text", "BAD"] }\n'
                             b'  "tab_width": 8\n}\n')
        time.sleep(1.0)
        shot = app.capture(shots / "m2_01_syntax_error.png")
        check("[C6] syntax error is shown in the echo area", echo_pixels(shot) > 0,
              f"echo text pixels={echo_pixels(shot)} (m2_01_syntax_error.png: "
              "'settings.json:3:3: Expected ',' or '}', found '\"' (file not applied)')")
        app.keys("<f5>")
        app.keys("C-x C-s")
        time.sleep(0.3)
        data = doc.read_bytes()
        check("[C6] syntax error keeps the previous binding", data == b"M2OKM2OKline one\nline two\n",
              f"bytes={data!r}")

        # [C8] Several semantic errors expand the echo area; the next key
        # returns it to one line.
        settings.write_bytes(b'{\n  "tab_widht": 4,\n  "colors": { "cursor": "red" },\n  "keys": {\n'
                             b'    "C-c a": "no-such-command",\n    "C-x": "undo",\n'
                             b'    "<f5>": ["insert-text", "M2OK"]\n  }\n}\n')
        time.sleep(1.0)
        shot = app.capture(shots / "m2_02_expanded_echo.png")
        top1, _ = mode_line_rows(shot)
        rows = round((top0 - top1) / line_px) + 1 if line_px else 0
        check("[C8] 4 errors expand the echo area to 4 lines", rows == 4,
              f"mode line top {top0}px -> {top1}px, line {line_px}px: {rows} echo rows "
              "(m2_02_expanded_echo.png)")
        app.keys("C-a")
        time.sleep(0.3)
        shot = app.capture(shots / "m2_03_after_key.png")
        top2, _ = mode_line_rows(shot)
        check("[C8] the next key returns the echo area to one line", top2 == top0,
              f"mode line top {top2}px (one-line layout {top0}px; m2_03_after_key.png)")

        # D2: deleting the file reverts to the defaults.
        app.keys("C-x C-s")  # unmodified before the next <f5>
        time.sleep(0.3)
        settings.unlink()
        time.sleep(1.0)
        shot = app.capture(shots / "m2_04_removed.png")
        app.keys("<f5>")
        time.sleep(0.3)
        check("[C6] removing the file reverts to the defaults",
              app.title() == "m2.txt - geditor" and echo_pixels(shot) > 0,
              f"title after <f5>={app.title()!r} (m2_04_removed.png: "
              "'Settings file removed; using defaults')")

        # [C14] A file that stays locked cannot be read: the previous
        # settings stay and the reason is shown.
        settings.write_bytes(b'{ "keys": { "<f5>": ["insert-text", "M2OK"] } }\n')
        ok, elapsed, _ = press_until_modified(app, "<f5>", 3.0)
        app.keys("C-/")
        time.sleep(0.2)
        handle = write_locked(settings, b'{ "keys": { "<f5>": ["insert-text", "NEW"] } }\n')
        try:
            time.sleep(1.8)  # 200 ms + 3 retries of 200 ms, then the read fails
            shot = app.capture(shots / "m2_05_locked.png")
            app.keys("<f5>")
            app.keys("C-x C-s")
            time.sleep(0.3)
        finally:
            kernel32.CloseHandle(handle)
        data = doc.read_bytes()
        check("[C14] a locked settings file keeps the previous settings and says why",
              ok and data.startswith(b"M2OKM2OKM2OK") and b"NEW" not in data and echo_pixels(shot) > 0,
              f"bytes={data[:40]!r} (m2_05_locked.png: 'Cannot read ...settings.json: The file is used "
              "by another process (settings kept)')")
        time.sleep(1.0)  # the close is not a change; nothing to wait for

        # [C8] find-user-init-file on a missing file: the template, unsaved.
        settings.unlink()
        time.sleep(1.0)
        app.keys("M-x")
        app.type("find-user-init-file")
        app.keys("RET")
        if answer(app, "Open in this window", timeout_s=3.0):
            time.sleep(0.5)
        shot = app.capture(shots / "m2_06_init_file_template.png")
        check("[C8] find-user-init-file opens the template unsaved",
              app.title() == "settings.json* - geditor" and not settings.exists(),
              f"title={app.title()!r} file exists={settings.exists()} (m2_06_init_file_template.png)")
        app.keys("C-x C-s")
        time.sleep(1.0)  # saved: the watcher reloads it
        shot = app.capture(shots / "m2_07_template_saved.png")
        data = settings.read_bytes() if settings.exists() else b""
        check("[C8] the saved template loads without errors",
              data.startswith(b"// geditor settings") and data.endswith(b"{\n}\n") and
              mode_line_rows(shot)[0] == top0,
              f"{len(data)} bytes, one-line echo area (m2_07_template_saved.png shows 'Wrote ...')")

        # [R2] Another file in the folder written every 50 ms must not delay
        # the reload of the settings file.
        stop_noise = threading.Event()

        def noise():
            i = 0
            while not stop_noise.is_set():
                (work / "noise.log").write_text(str(i))
                i += 1
                time.sleep(0.05)

        noise_thread = threading.Thread(target=noise)
        noise_thread.start()
        try:
            time.sleep(0.3)
            settings.write_bytes(b'{ "keys": { "<f5>": ["insert-text", "NOISE"] } }\n')
            ok, elapsed, presses = press_until_modified(app, "<f5>", 3.0)
        finally:
            stop_noise.set()
            noise_thread.join()
        check("[R2] reload within 1 s while another file in the folder changes every 50 ms",
              ok and elapsed <= 1.0, f"elapsed={elapsed:.3f}s ({presses} presses, noise.log rewritten every 50 ms)")

        # [R4] A low window: the echo area leaves the mode line and one text
        # row; the last echo line counts the hidden errors.
        app.resize(900, 200)
        time.sleep(0.4)
        settings.write_bytes(("{\n" + "".join(f'  "bad{i}": 1,\n' for i in range(12)) + "}\n").encode())
        time.sleep(1.0)
        shot = app.capture(shots / "m2_09_small_window.png")
        top, line_px = mode_line_rows(shot)
        height = shot[1]
        echo_rows = round((height - (top + line_px)) / line_px) if line_px else 0
        check("[R4] a low window keeps the mode line and one text row",
              line_px > 0 and top >= line_px and echo_rows >= 1,
              f"client height {height}px, mode line top {top}px, line {line_px}px, {echo_rows} echo rows "
              f"(m2_09_small_window.png: the last echo line is '... and N more')")
        if d.HEADLESS:
            check_offscreen(app, "after a resize and a settings reload")
    finally:
        app.kill()

    # D12 / [C14]: a settings file in a missing folder cannot be watched.
    missing = work / "no_such_folder" / "settings.json"
    app = d.App(exe, [str(doc)], cwd=work, settings=missing)
    try:
        time.sleep(0.8)
        shot = app.capture(shots / "m2_08_watch_failed.png")
        check("[C14] watcher failure is shown once", echo_pixels(shot) > 0 and app.alive(),
              f"echo text pixels={echo_pixels(shot)} (m2_08_watch_failed.png: "
              "'Cannot watch the settings file: C:\\...\\no_such_folder: No such folder')")
    finally:
        app.kill()


# ---- M3a: display ----

# A vivid colour scheme on black: text colours are told apart by their hue
# (anti-aliased pixels of pure colours on black keep the zero channels).
VIVID = {
    "background": "#000000", "foreground": "#ff0000", "cursor": "#ffffff",
    "selection": "#400040", "line_number": "#00ff00", "line_number_current": "#0000ff",
    "whitespace": "#ffff00", "eol": "#00ffff",
}


def is_red(r, g, b):
    return r > 60 and g < 30 and b < 30


def is_green(r, g, b):
    return g > 60 and r < 30 and b < 30


def is_blue(r, g, b):
    return b > 60 and r < 30 and g < 30


def is_yellow(r, g, b):
    return r > 60 and g > 60 and b < 30


def is_cyan(r, g, b):
    return g > 60 and b > 60 and r < 30


def count_where(shot, pred, box=None) -> int:
    """Pixels for which pred(r, g, b) holds, inside box (x0, y0, x1, y1)."""
    w, h, px = shot
    x0, y0, x1, y1 = box or (0, 0, w, h)
    n = 0
    for y in range(max(0, y0), min(h, y1)):
        row = px[y * w * 4:(y + 1) * w * 4]
        for x in range(max(0, x0), min(w, x1)):
            i = x * 4
            if pred(row[i + 2], row[i + 1], row[i]):
                n += 1
    return n


def color_box(shot, rgb: int, box=None):
    """Bounding box of the pixels of exactly `rgb` (None when there are none)."""
    w, h, px = shot
    x0, y0, x1, y1 = box or (0, 0, w, h)
    b, g, r = rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF
    xs, ys = [], []
    for y in range(max(0, y0), min(h, y1)):
        row = px[y * w * 4:(y + 1) * w * 4]
        for x in range(max(0, x0), min(w, x1)):
            i = x * 4
            if row[i] == b and row[i + 1] == g and row[i + 2] == r:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def text_box(shot):
    w, _, _ = shot
    return (0, 0, w, layout(shot)[0])


def settings_json(items: dict) -> bytes:
    import json
    return (json.dumps(items, ensure_ascii=False) + "\n").encode("utf-8")


class M3aRun:
    """State of the M3a checks: the [C4] check table and helpers."""

    def __init__(self, out: pathlib.Path):
        self.shots = out / "shots"
        self.table = []

    def row(self, item: str, shot: str, seen: str):
        self.table.append((item, shot, seen))

    def capture(self, app, name: str):
        return app.capture(self.shots / name)


def apply_settings(settings: pathlib.Path, items: dict, wait_s: float = 1.0):
    settings.write_bytes(settings_json(items))
    time.sleep(wait_s)  # the watcher waits 200 ms, then the frame


def run_m3a_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C4] [C5] [C7]-[C11] [C14] of the M3a packet on the real app."""
    run = M3aRun(out)
    work = out / "files_m3a"
    work.mkdir(parents=True, exist_ok=True)
    settings = work / "settings.json"
    settings.write_bytes(b"{}\n")
    doc = work / "m3a.txt"
    lines = [
        "int main() {",                                  # 1
        "\treturn 0;  ",                                 # 2: TAB, trailing spaces
        "}",                                             # 3
        "a b  c",                                        # 4: spaces
        "日本語のテキスト　全角空白 ABC 漢字 😀",            # 5: Japanese, U+3000, emoji
        "".join(f"{i:02d}-------" for i in range(15)),   # 6: 135 cells
        "12345678901234567890",                          # 7: exactly 20 cells
        "last line",                                     # 8: no line end
    ]
    doc.write_bytes("\n".join(lines).encode("utf-8"))
    app = d.App(exe, [str(doc)], cwd=work, settings=settings, height=800)
    try:
        record("INFO", "M3a input mode", app.use_input())
        time.sleep(0.6)
        base = run.capture(app, "m3a_01_defaults.png")
        _, lh = mode_line_rows(base)
        record("INFO", "M3a window", f"client {base[0]}x{base[1]}px, line height {lh}px, "
               f"DPI {d.user32.GetDpiForWindow(app.hwnd)}")
        n_num = count(base, 0x858585)
        n_cur = count(base, 0xC6C6C6)
        n_ws = count(base, 0x404040)
        check("[C4] defaults: line numbers, trailing whitespace and end-of-line marks are drawn",
              n_num > 0 and n_cur > 0 and n_ws > 0,
              f"line_number px={n_num} line_number_current px={n_cur} whitespace px={n_ws} "
              "(m3a_01_defaults.png)")
        run.row("defaults (line_numbers, wrap max, whitespace tab/fullwidth/trailing, eol marks)",
                "m3a_01_defaults.png",
                "numbers 1-8 in the gutter (1 brighter), → before 'return', grey box after 'return 0;', "
                "□ in line 5, ↓ after lines 1-7, none after 'last line', line 6 wrapped, 'wrap:max'")

        # Cell width from the cursor: C-e on line 1 (12 cells).
        app.keys("M-< C-e")
        time.sleep(0.2)
        shot = run.capture(app, "m3a_02_line1_end.png")
        end1 = cursor_box(shot)
        app.keys("C-a")
        time.sleep(0.2)
        start1 = cursor_box(run.capture(app, "m3a_03_line1_start.png"))
        cw = (end1[0] - start1[0]) / 12 if end1 and start1 else 0
        record("INFO", "cell width", f"{cw:.2f}px (cursor x {start1} -> {end1})")

        # ---- colours (vivid scheme) ----
        apply_settings(settings, {"colors": VIVID})
        app.keys("C-n C-n C-n C-a C-SPC C-e")  # region on line 4
        time.sleep(0.2)
        vivid = run.capture(app, "m3a_04_colors.png")
        app.keys("C-g")
        tb = text_box(vivid)
        gutter_right = start1[0] if start1 else 0
        results = {
            "background": count(vivid, 0x000000) > vivid[0] * tb[3] // 3,
            "foreground": count_where(vivid, is_red, tb) > 300,
            "cursor": color_box(vivid, 0xFFFFFF, tb) is not None,
            "selection": count(vivid, 0x400040) > 100,
            "line_number": count_where(vivid, is_green, (0, 0, gutter_right, tb[3])) > 20,
            "line_number_current": count_where(vivid, is_blue, (0, 0, gutter_right, tb[3])) > 5,
            "whitespace": count(vivid, 0xFFFF00) > 50,
            "eol": count_where(vivid, is_cyan, tb) > 20,
        }
        check("[C4] colours: background, foreground, cursor, selection, line_number, "
              "line_number_current, whitespace, eol", all(results.values()),
              " ".join(f"{k}={'ok' if v else 'MISSING'}" for k, v in results.items()) +
              " (m3a_04_colors.png)")
        run.row("colors.background / foreground / cursor / selection / line_number / "
                "line_number_current / whitespace / eol", "m3a_04_colors.png",
                "black background, red text, white cursor bar, purple region on line 4, green "
                "numbers with blue '4' (current line), yellow marks and trailing box, cyan ↓")
        apply_settings(settings, {"colors": {"mode_line_background": "#008000",
                                             "mode_line_foreground": "#ffff00",
                                             "cursor": "#ff8000"}})
        shot = run.capture(app, "m3a_05_mode_line_colors.png")
        n_bg = count(shot, 0x008000)
        n_fg = count_where(shot, lambda r, g, b: r > 100 and g > 100 and b < 60)
        n_cursor = count(shot, 0xFF8000)
        check("[C4] colours: mode_line_background, mode_line_foreground, cursor",
              n_bg > shot[0] * 5 and n_fg > 50 and n_cursor > 10,
              f"green mode line px={n_bg} yellow mode line text px={n_fg} orange cursor px={n_cursor} "
              "(m3a_05_mode_line_colors.png)")
        run.row("colors.mode_line_background / mode_line_foreground / cursor", "m3a_05_mode_line_colors.png",
                "green mode line with yellow text, orange cursor bar")
        record("INFO", "[C4] search_match / search_current",
               "kept in the settings (test display_each_color_key_reaches_the_model); "
               "nothing is searched before M5, so no screenshot")
        run.row("colors.search_match / search_current", "-",
                "not drawable before M5 (no search yet); the values reach the model (unit test)")

        # ---- live reload keeps the text, point and undo ([C4]) ----
        apply_settings(settings, {})
        app.keys("M-< C-e")
        app.type("XYZ")
        time.sleep(0.2)
        before = cursor_box(run.capture(app, "m3a_06_before_reload.png"))
        apply_settings(settings, {"colors": {"background": "#202040"}})
        shot = run.capture(app, "m3a_07_after_reload.png")
        after = cursor_box(shot)
        title_mod = app.title()
        app.keys("C-/")
        time.sleep(0.3)
        check("[C4] a settings change keeps the text, point and undo history",
              title_mod == "m3a.txt* - geditor" and before == after and app.title() == "m3a.txt - geditor"
              and count(shot, 0x202040) > 1000,
              f"title after reload={title_mod!r}, cursor {before} -> {after}, title after C-/="
              f"{app.title()!r} (m3a_06/07: same text and cursor, blue-grey background)")
        run.row("settings change while running", "m3a_06_before_reload.png / m3a_07_after_reload.png",
                "'XYZ' typed, cursor after it; after the reload the background changes, text and cursor stay; "
                "C-/ then removes XYZ")

        # ---- font family / size / fallback, missing names ----
        apply_settings(settings, {"font": {"family": "Courier New"}})
        app.keys("M-< C-e")
        time.sleep(0.2)
        shot = run.capture(app, "m3a_08_font_family.png")
        end_courier = cursor_box(shot)
        check("[C4] font.family changes the font (cell width)",
              end1 is not None and end_courier is not None and end_courier[0] != end1[0],
              f"line 1 end x: Consolas {end1 and end1[0]} -> Courier New {end_courier and end_courier[0]} "
              "(m3a_08_font_family.png)")
        run.row("font.family", "m3a_08_font_family.png", "Courier New: thinner serif letters, wider cells")
        apply_settings(settings, {"font": {"size": 18}})
        shot = run.capture(app, "m3a_09_font_size.png")
        _, lh18 = mode_line_rows(shot)
        check("[C4] font.size changes the line height", lh18 > lh * 1.3,
              f"mode line {lh}px -> {lh18}px at 18pt (m3a_09_font_size.png)")
        run.row("font.size", "m3a_09_font_size.png", "18pt: everything 1.5 times larger")
        japanese_box = (0, 4 * lh, base[0], 5 * lh)
        apply_settings(settings, {"font": {"fallback": ["MS Gothic"]}})
        fallback = run.capture(app, "m3a_10_font_fallback.png")
        apply_settings(settings, {})
        default_again = run.capture(app, "m3a_11_font_default.png")
        diff = sum(1 for i in range(0, len(fallback[2]), 4)
                   if fallback[2][i:i + 3] != default_again[2][i:i + 3]
                   and japanese_box[1] * fallback[0] * 4 <= i < japanese_box[3] * fallback[0] * 4)
        check("[C4] font.fallback changes the font of Japanese text", diff > 200,
              f"{diff} pixels of line 5 differ between fallback MS Gothic and the system fallback "
              "(m3a_10_font_fallback.png vs m3a_11_font_default.png)")
        run.row("font.fallback", "m3a_10_font_fallback.png (vs m3a_11_font_default.png)",
                "line 5 Japanese in MS Gothic (thin, bitmap-like) instead of Yu Gothic UI")
        apply_settings(settings, {"font": {"family": "No Such Font", "fallback": ["Missing Fallback"]}})
        shot = run.capture(app, "m3a_12_font_missing.png")
        n_echo = echo_pixels(shot)
        app.keys("M-< C-e")
        time.sleep(0.2)
        end_missing = cursor_box(run.capture(app, "m3a_12b_font_missing_cells.png"))
        check("[C14] missing font names are reported; Consolas is used (E1)",
              n_echo > 0 and end_missing is not None and end1 is not None and end_missing[0] == end1[0],
              f"echo text px={n_echo}, line 1 end x={end_missing and end_missing[0]} (Consolas {end1[0]}) "
              "(m3a_12_font_missing.png: 'Font not found: No Such Font; Font not found: Missing Fallback')")
        run.row("font.family / fallback not installed (E1)", "m3a_12_font_missing.png",
                "echo area 'Font not found: No Such Font; Font not found: Missing Fallback', text in Consolas")

        # ---- tab_width ----
        apply_settings(settings, {})
        app.keys("M-< C-n C-a C-f")
        time.sleep(0.2)
        tab4 = cursor_box(run.capture(app, "m3a_13_tab_width_4.png"))
        apply_settings(settings, {"tab_width": 8})
        tab8 = cursor_box(run.capture(app, "m3a_14_tab_width_8.png"))
        moved = (tab8[0] - tab4[0]) / cw if tab4 and tab8 and cw else 0
        check("[C4] tab_width moves the text after a TAB", abs(moved - 4) < 0.6,
              f"cursor after the TAB: x {tab4 and tab4[0]} -> {tab8 and tab8[0]} = {moved:.2f} cells "
              "(m3a_13 / m3a_14)")
        run.row("tab_width", "m3a_13_tab_width_4.png / m3a_14_tab_width_8.png",
                "'return' starts 4 cells further right with 8")

        # ---- line_numbers ----
        apply_settings(settings, {"line_numbers": False})
        app.keys("C-a")
        time.sleep(0.2)
        shot = run.capture(app, "m3a_15_no_line_numbers.png")
        nonum = cursor_box(shot)
        gutter = (0, 0, gutter_right, layout(shot)[0])
        n_gutter = d.count_color(shot[0], shot[1], shot[2], 0x858585, gutter)
        check("[C4] line_numbers false removes the gutter",
              n_gutter == 0 and nonum is not None and start1 is not None and nonum[0] < start1[0],
              f"line_number px in the old gutter={n_gutter}, line start x {start1 and start1[0]} -> "
              f"{nonum and nonum[0]} (m3a_15_no_line_numbers.png)")
        run.row("line_numbers", "m3a_15_no_line_numbers.png", "no numbers; text starts at the left margin")

        # ---- wrap.mode / wrap.column ----
        ys = {}
        for name, wrap in [("none", {"mode": "none"}), ("window", {"mode": "window"}),
                           ("fixed", {"mode": "fixed", "column": 20}), ("max", {"mode": "max", "column": 30})]:
            apply_settings(settings, {"wrap": wrap})
            # From the last line: C-p moves by screen rows, C-e to the end of
            # the logical line.
            app.keys("M-> C-p C-p C-e")
            time.sleep(0.2)
            shot = run.capture(app, f"m3a_16_wrap_{name}.png")
            box = cursor_box(shot)
            ys[name] = box[1] if box else None
        rows_fixed = round((ys["fixed"] - ys["none"]) / lh) if ys["fixed"] and ys["none"] is not None else -1
        rows_max = round((ys["max"] - ys["none"]) / lh) if ys["max"] and ys["none"] is not None else -1
        # Line 5 (38 cells) takes 2 rows at 20 and 30 cells; line 6 (135
        # cells) takes 7 rows at 20 and 5 at 30: its end is 1 + 6 rows and
        # 1 + 4 rows below the row it has without wrapping.
        check("[C4] wrap.mode / wrap.column: the end of the 135-cell line is on the expected row",
              rows_fixed == 7 and rows_max == 5 and ys["window"] is not None and
              ys["none"] < ys["window"] <= ys["fixed"],
              f"cursor y none={ys['none']} window={ys['window']} fixed 20={ys['fixed']} ({rows_fixed} rows "
              f"below, expected 7) max 30={ys['max']} ({rows_max} rows below, expected 5) "
              "(m3a_16_wrap_*.png; the mode line shows wrap:<mode>)")
        run.row("wrap.mode / wrap.column", "m3a_16_wrap_none / window / fixed / max.png",
                "line 6: one row scrolled to its end (none), window-wide rows (window), 20-cell rows "
                "(fixed 20), 30-cell rows (max 30); mode line wrap:<mode>")

        # ---- whitespace flags (vivid: marks are yellow) ----
        def ws_count(items, name):
            apply_settings(settings, {"colors": VIVID, **items})
            shot = run.capture(app, name)
            return count_where(shot, is_yellow, text_box(shot)), count(shot, 0xFFFF00)

        app.keys("M-<")
        base_ws, base_rect = ws_count({}, "m3a_17_ws_default.png")
        space_ws, _ = ws_count({"whitespace": {"space": True}}, "m3a_18_ws_space.png")
        tab_ws, _ = ws_count({"whitespace": {"tab": False}}, "m3a_19_ws_tab_off.png")
        full_ws, _ = ws_count({"whitespace": {"fullwidth_space": False}}, "m3a_20_ws_fullwidth_off.png")
        trail_ws, trail_rect = ws_count({"whitespace": {"trailing": False}}, "m3a_21_ws_trailing_off.png")
        check("[C4] whitespace.space / tab / fullwidth_space / trailing",
              space_ws > base_ws and tab_ws < base_ws and full_ws < base_ws and trail_rect < base_rect,
              f"yellow px: default={base_ws} space on={space_ws} tab off={tab_ws} fullwidth off={full_ws}; "
              f"trailing box px {base_rect} -> {trail_rect} (m3a_17..21)")
        run.row("whitespace.space / tab / fullwidth_space / trailing",
                "m3a_17_ws_default / 18_ws_space / 19_ws_tab_off / 20_ws_fullwidth_off / 21_ws_trailing_off.png",
                "· appears in spaces (18); → gone (19); □ gone (20); trailing box gone (21)")

        # ---- eol_marks ----
        _ = ws_count({}, "m3a_22_eol_default.png")
        apply_settings(settings, {"colors": VIVID, "eol_marks": {"show": False}})
        shot = run.capture(app, "m3a_23_eol_off.png")
        off = count_where(shot, is_cyan, text_box(shot))
        apply_settings(settings, {"colors": VIVID, "eol_marks": {"lf": "$"}})
        shot = run.capture(app, "m3a_24_eol_symbol.png")
        dollar = count_where(shot, is_cyan, text_box(shot))
        check("[C4] eol_marks.show / symbols", off == 0 and dollar > 0,
              f"cyan px: show false={off}, lf '$'={dollar} (m3a_22 / 23 / 24)")
        run.row("eol_marks.show / lf (crlf: CRLF file below)", "m3a_23_eol_off.png / m3a_24_eol_symbol.png",
                "no marks (23); '$' at the line ends (24)")

        # ---- [C7] marks: wrap-width line, cursor at the end ----
        apply_settings(settings, {"colors": VIVID, "wrap": {"mode": "fixed", "column": 20}})
        app.keys("M-> C-p C-e")  # line 7 (exactly 20 cells), at its end
        time.sleep(0.2)
        shot = run.capture(app, "m3a_25_eol_full_row.png")
        cur = color_box(shot, 0xFFFFFF, text_box(shot))
        band = (cur[0], cur[1], shot[0], cur[3] + 1) if cur else None
        mark = count_where(shot, is_cyan, band) if band else 0
        app.keys("C-n C-a")
        time.sleep(0.2)
        nxt = cursor_box(run.capture(app, "m3a_26_after_full_row.png"))
        check("[C7] a line as wide as the wrap width keeps its mark (reserved cell); the cursor at the "
              "end does not hide it; the next line is the next row",
              cur is not None and mark > 5 and nxt is not None and abs((nxt[1] - cur[1]) - lh) <= 2,
              f"cursor {cur}, cyan px right of the cursor on its row={mark}, next line y {nxt and nxt[1]} "
              f"(one row = {lh}px) (m3a_25_eol_full_row.png)")
        apply_settings(settings, {"colors": VIVID, "wrap": {"mode": "fixed", "column": 20},
                                  "eol_marks": {"show": False}})
        app.keys("C-p C-e")
        time.sleep(0.2)
        cur_off = color_box(run.capture(app, "m3a_27_eol_off_full_row.png"), 0xFFFFFF, tb)
        check("[C7] marks do not move the cursor or the wrap columns", cur_off == cur,
              f"cursor at the end of line 7 with marks {cur} / without {cur_off} (m3a_25 / m3a_27)")
        run.row("[M3-7] marks at line ends / full row / cursor at the end",
                "m3a_22_eol_default / m3a_25_eol_full_row / m3a_27_eol_off_full_row.png",
                "cyan ↓ after each line; on the 20-cell line the ↓ is in the cell after it, right of "
                "the cursor bar; same cursor place without marks")

        # ---- [C8] View commands from M-x ----
        apply_settings(settings, {"colors": VIVID})
        before_bytes = settings.read_bytes()
        for command, shot_name in [("toggle-line-numbers", "m3a_28_mx_line_numbers.png"),
                                   ("toggle-eol-marks", "m3a_29_mx_eol_marks.png"),
                                   ("toggle-whitespace-marks", "m3a_30_mx_whitespace.png")]:
            app.keys("M-x")
            app.type(command)
            app.keys("RET")
            time.sleep(0.3)
            run.capture(app, shot_name)
        app.keys("M-x")
        app.type("set-wrap-mode")
        app.keys("RET")
        app.type("none")
        app.keys("RET")
        time.sleep(0.3)
        shot = run.capture(app, "m3a_31_mx_wrap_none.png")
        tbx = text_box(shot)
        greens = count_where(shot, is_green, tbx)
        cyans = count_where(shot, is_cyan, tbx)
        yellows = count_where(shot, is_yellow, tbx)
        check("[C8] M-x toggle-line-numbers / toggle-eol-marks / toggle-whitespace-marks / set-wrap-mode",
              greens == 0 and cyans == 0 and yellows == 0 and settings.read_bytes() == before_bytes,
              f"green={greens} cyan={cyans} yellow={yellows} px; settings file unchanged="
              f"{settings.read_bytes() == before_bytes} (m3a_28..31; 31 shows wrap:none)")
        run.row("[C8] View commands (M-x)", "m3a_28 .. m3a_31_mx_*.png",
                "numbers go (28), marks go (29), whitespace marks go (30), mode line wrap:none (31)")
        # E6: another item changes; the toggled values stay.
        apply_settings(settings, {"colors": VIVID, "tab_width": 8})
        shot = run.capture(app, "m3a_32_reload_keeps_toggles.png")
        check("[C8] a reload keeps the toggled values while the file's values stay (E6)",
              count_where(shot, is_green, text_box(shot)) == 0 and count_where(shot, is_cyan, text_box(shot)) == 0,
              "no numbers and no marks after a tab_width change (m3a_32_reload_keeps_toggles.png)")

        # ---- [C9] text-scale ----
        apply_settings(settings, {"line_numbers": True, "wrap": {"mode": "window"}, "tab_width": 4})
        shot0 = run.capture(app, "m3a_33_scale_0.png")
        _, h0 = mode_line_rows(shot0)
        app.keys("C-x C-+")
        time.sleep(0.4)
        shot1 = run.capture(app, "m3a_34_scale_plus1.png")
        _, h1 = mode_line_rows(shot1)
        app.keys("C-x C-0")
        time.sleep(0.4)
        _, h2 = mode_line_rows(run.capture(app, "m3a_35_scale_reset.png"))
        check("[C9] text-scale-increase is 1.2 times; reset returns; point stays visible",
              h0 > 0 and 1.1 <= h1 / h0 <= 1.3 and h2 == h0 and cursor_box(shot1) is not None,
              f"mode line {h0}px -> {h1}px (x{h1 / h0 if h0 else 0:.3f}) -> {h2}px; cursor visible="
              f"{cursor_box(shot1) is not None} (m3a_33 / 34 / 35)")
        run.row("[C9] text-scale", "m3a_34_scale_plus1.png", "text 1.2 times larger, 'Text scale +1'")

        # ---- [M3-3] / [C5] fonts ----
        apply_settings(settings, {"font": {"size": 20}})
        app.keys("M-< C-n C-n C-n C-n")
        time.sleep(0.3)
        run.capture(app, "m3a_36_fonts.png")
        record("INFO", "[C5] fonts", "m3a_36_fonts.png: ASCII in Consolas, Japanese in the system "
               "fallback (Yu Gothic UI); the unit test win_font_* prints the family of each sample")
        run.row("[M3-3] fonts", "m3a_36_fonts.png",
                "'ABC' in Consolas, '日本語のテキスト' / '漢字' in a Japanese font, the emoji in Segoe UI Emoji")
    finally:
        app.kill()

    run_m3a_window_checks(exe, out, run)
    run_m3a_font_report_checks(exe, out)
    print("[C4 table] item | screenshot | what is seen", flush=True)
    for item, shot, seen in run.table:
        print(f"[C4 table] {item} | {shot} | {seen}", flush=True)


def run_m3a_font_report_checks(exe: pathlib.Path, out: pathlib.Path):
    """Review R1 (decision E1): a missing font name is reported once per value
    of font.family / font.fallback, not again on text-scale, a DPI change or
    a font.size change."""
    work = out / "files_m3a"
    work.mkdir(parents=True, exist_ok=True)
    settings = work / "settings_fonts.json"
    settings.write_bytes(settings_json({"font": {"family": "NoSuchFontXYZ"}}))
    doc = work / "fonts.txt"
    doc.write_bytes(b"font report\n")
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)

    def message():
        return app.state().get("message", "")

    try:
        app.use_input()
        time.sleep(0.6)
        start = message()
        app.keys("C-x C-+")
        time.sleep(0.3)
        plus1 = message()
        app.keys("C-x C-+")
        time.sleep(0.3)
        plus2 = message()
        check("[R1] text-scale with a missing family: 'Font not found' once (at start only)",
              start.count("Font not found: NoSuchFontXYZ") == 1 and "Font not found" not in plus1 and
              "Font not found" not in plus2 and "Text scale +1" in plus1 and "Text scale +2" in plus2,
              f"start={start!r} +1={plus1!r} +2={plus2!r}")
        app.keys("C-g")
        time.sleep(0.2)
        dpi0 = d.user32.GetDpiForWindow(app.hwnd)
        rect = d.window_rect(app.hwnd)
        d.send_dpi_changed(app.hwnd, dpi0 * 3 // 2, rect)
        time.sleep(0.4)
        after_dpi = message()
        d.send_dpi_changed(app.hwnd, dpi0, rect)
        time.sleep(0.4)
        after_back = message()
        check("[R1] WM_DPICHANGED with a missing family: no repeated 'Font not found'",
              "Font not found" not in after_dpi and "Font not found" not in after_back,
              f"after {dpi0} -> {dpi0 * 3 // 2} DPI: {after_dpi!r}; back: {after_back!r}")
        if d.HEADLESS:
            check_offscreen(app, "after the WM_DPICHANGED of [R1]")
        apply_settings(settings, {"font": {"family": "NoSuchFontXYZ", "size": 14}})
        size_only = message()
        apply_settings(settings, {"font": {"family": "OtherMissingFont", "size": 14}})
        other = message()
        apply_settings(settings, {"font": {"family": "OtherMissingFont", "size": 14,
                                           "fallback": ["MissingFallbackXYZ"]}})
        fallback = message()
        check("[R1] font.size only: silent; another missing family / fallback: reported",
              "Font not found" not in size_only and other.count("Font not found: OtherMissingFont") == 1 and
              "Font not found: MissingFallbackXYZ" in fallback,
              f"size only={size_only!r} family changed={other!r} fallback added={fallback!r}")
    finally:
        app.kill()


def run_m3a_window_checks(exe: pathlib.Path, out: pathlib.Path, run: M3aRun):
    """The scroll bar ([C11]), Per-Monitor V2 ([C10]) and failures ([C14])."""
    work = out / "files_m3a"
    settings = work / "settings_window.json"
    settings.write_bytes(b"{}\n")
    doc = work / "m3a_lines.txt"
    doc.write_bytes("".join(f"line {i + 1}\n" for i in range(200)).encode())
    crlf = work / "m3a_crlf.txt"
    crlf.write_bytes(b"crlf one\r\ncrlf two\r\nlast")
    app = d.App(exe, [str(crlf)], cwd=work, settings=settings)
    try:
        time.sleep(0.5)
        shot = run.capture(app, "m3a_37_crlf.png")
        check("[C7] CRLF file: marks on lines with a line end only", count(shot, 0x505050) > 0,
              f"eol px={count(shot, 0x505050)} (m3a_37_crlf.png: ↵ after lines 1-2, none after 'last')")
        run.row("eol_marks.crlf", "m3a_37_crlf.png", "↵ after 'crlf one' and 'crlf two', nothing after 'last'")
    finally:
        app.kill()

    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    try:
        app.use_input()
        time.sleep(0.5)
        si = d.scroll_info(app.hwnd)
        page = si.nPage
        check("[C11] scroll bar range: logical lines, page = rows", si.nMax == 200 and si.nPos == 0 and page > 5,
              f"nMin={si.nMin} nMax={si.nMax} (201 lines) nPage={si.nPage} nPos={si.nPos}")
        d.user32.SendMessageW(app.hwnd, d.WM_VSCROLL, 1, 0)  # SB_LINEDOWN
        time.sleep(0.2)
        pos1 = d.scroll_info(app.hwnd).nPos
        d.user32.SendMessageW(app.hwnd, d.WM_VSCROLL, 3, 0)  # SB_PAGEDOWN
        time.sleep(0.2)
        pos2 = d.scroll_info(app.hwnd).nPos
        shot = run.capture(app, "m3a_38_scroll_page.png")
        visible2 = cursor_box(shot) is not None
        d.user32.SendMessageW(app.hwnd, d.WM_VSCROLL, (100 << 16) | 5, 0)  # SB_THUMBTRACK to 100
        time.sleep(0.2)
        pos3 = d.scroll_info(app.hwnd).nPos
        shot = run.capture(app, "m3a_39_scroll_thumb.png")
        visible3 = cursor_box(shot) is not None
        d.user32.SendMessageW(app.hwnd, d.WM_VSCROLL, 7, 0)  # SB_BOTTOM
        time.sleep(0.2)
        pos4 = d.scroll_info(app.hwnd).nPos
        check("[C11] WM_VSCROLL line / page / thumb track / bottom scroll and keep point visible",
              pos1 == 1 and pos2 == 1 + page - 2 and pos3 == 100 and pos4 == 201 - page and visible2 and visible3,
              f"nPos: line down={pos1}, page down={pos2} (page {page}), thumb 100={pos3}, bottom={pos4} "
              f"(expected {201 - page}); cursor visible={visible2}/{visible3} (m3a_38 / m3a_39)")
        run.row("[C11] scroll bar", "m3a_38_scroll_page.png / m3a_39_scroll_thumb.png",
                "after page down / thumb to 100: top line moved, cursor bar on the top row")

        # ---- [C10] Per-Monitor V2 and WM_DPICHANGED ----
        awareness, v2 = d.dpi_awareness(app.hwnd)
        check("[C10] the window is Per-Monitor V2 aware (no bitmap stretching, no blur)", awareness == 2 and v2,
              f"GetAwarenessFromDpiAwarenessContext={awareness} (2 = per monitor), V2={v2}")
        d.user32.SendMessageW(app.hwnd, d.WM_VSCROLL, (40 << 16) | 5, 0)
        time.sleep(0.2)
        before = run.capture(app, "m3a_40_dpi_before.png")
        _, lh0 = mode_line_rows(before)
        top0 = d.scroll_info(app.hwnd).nPos
        dpi0 = d.user32.GetDpiForWindow(app.hwnd)
        rect = d.window_rect(app.hwnd)
        new_dpi = dpi0 * 3 // 2
        suggested = wt.RECT(rect.left, rect.top, rect.left + (rect.right - rect.left) * 3 // 2,
                            rect.top + (rect.bottom - rect.top) * 3 // 2)
        d.send_dpi_changed(app.hwnd, new_dpi, suggested)
        time.sleep(0.5)
        after = run.capture(app, "m3a_41_dpi_after.png")
        _, lh1 = mode_line_rows(after)
        top1 = d.scroll_info(app.hwnd).nPos
        r1 = d.window_rect(app.hwnd)
        check("[C10] WM_DPICHANGED: larger text, the suggested rectangle, the same top line",
              lh0 > 0 and 1.35 <= lh1 / lh0 <= 1.65 and top1 == top0 and
              (r1.right - r1.left) == (suggested.right - suggested.left),
              f"DPI {dpi0} -> {new_dpi}: line {lh0}px -> {lh1}px (x{lh1 / lh0 if lh0 else 0:.2f}), top line "
              f"{top0} -> {top1}, window width {r1.right - r1.left} (suggested "
              f"{suggested.right - suggested.left}) (m3a_40 / m3a_41)")
        run.row("[C10] Per-Monitor V2", "m3a_40_dpi_before.png / m3a_41_dpi_after.png",
                f"after {dpi0} -> {new_dpi} DPI the text is 1.5 times larger and sharp; 'line 41' stays on top")
        if d.HEADLESS:
            check_offscreen(app, "after WM_DPICHANGED to the suggested rectangle")

        # ---- [C14] a failed font creation after a DPI change ----
        d.inject_fault(app.hwnd, "font")
        d.send_dpi_changed(app.hwnd, dpi0, rect)
        time.sleep(0.5)
        shot = run.capture(app, "m3a_42_font_failure.png")
        check("[C14] a failed font creation after a DPI change is reported", echo_pixels(shot) > 0,
              f"echo text px={echo_pixels(shot)} (m3a_42_font_failure.png: 'Cannot create the font "
              "Consolas at N px; keeping Consolas')")
        if d.HEADLESS:
            check_offscreen(app, "after WM_DPICHANGED back")

        # ---- [C14] a failed render target ----
        app.keys("C-g")
        time.sleep(0.2)
        d.inject_fault(app.hwnd, "target")
        d.user32.InvalidateRect(app.hwnd, None, False)
        time.sleep(1.2)  # the failed frame, the 500 ms retry, the report
        shot = run.capture(app, "m3a_43_target_failure.png")
        check("[C14] a frame that could not be drawn is reported once drawing works again",
              echo_pixels(shot) > 0,
              f"echo text px={echo_pixels(shot)} (m3a_43_target_failure.png: 'Drawing failed (0x80004005); "
              "the window was drawn again')")

        # ---- [C14] invalid display values ----
        settings.write_bytes(b'{\n  "font": {"size": 0},\n  "colors": {"cursor": "#zzz"},\n'
                             b'  "wrap": {"mode": "zigzag"}\n}\n')
        time.sleep(1.0)
        shot = run.capture(app, "m3a_44_invalid_values.png")
        top_m, lh_m = mode_line_rows(shot)
        echo_rows = round((shot[1] - (top_m + lh_m)) / lh_m) if lh_m else 0
        check("[C14] invalid display values are listed in the echo area", echo_rows == 3,
              f"{echo_rows} echo rows (m3a_44_invalid_values.png: font.size, colors.cursor, wrap.mode)")
    finally:
        app.kill()


def clipboard_accessible() -> bool:
    if d.user32.OpenClipboard(None):
        d.user32.CloseClipboard()
        return True
    return False


# ---- the headless mode itself ----

def check_headless_window(name: str, hwnd):
    """[C2] (a) and (c) for one window."""
    hit, rect, monitors = d.intersects_a_monitor(hwnd)
    style = d.ex_style(hwnd)
    check(f"{name} (a): the window is outside every monitor", not hit,
          f"window {rect}, monitors {monitors} (EnumDisplayMonitors)")
    check(f"{name} (c): tool window without WS_EX_APPWINDOW (no taskbar button), WS_EX_NOACTIVATE",
          bool(style & d.WS_EX_TOOLWINDOW) and not style & d.WS_EX_APPWINDOW and bool(style & d.WS_EX_NOACTIVATE),
          f"ex style {style:#010x}")


def check_offscreen(app, step: str):
    """H1 after a state change driven by the check: the headless window is
    on no monitor and not iconic (the sampler checks the whole run too)."""
    hit, rect, monitors = d.intersects_a_monitor(app.hwnd)
    iconic = bool(d.user32.IsIconic(app.hwnd))
    check(f"[H1] {step}: the window is on no monitor and not iconic", not hit and not iconic,
          f"window {rect} iconic={iconic} monitors {monitors}")


def check_headless_new_window(app, hwnd):
    """[C6] a window opened from a headless window."""
    check_headless_window("[C6] new window", hwnd)
    mine, theirs = app.state(), d.read_state(hwnd)
    check("[C6] the new window is headless and has the same --settings",
          theirs.get("headless") == "1" and bool(mine.get("settings")) and
          theirs.get("settings") == mine.get("settings"),
          f"headless={theirs.get('headless')} settings={theirs.get('settings')!r} "
          f"(opening window: {mine.get('settings')!r})")


def run_measure_script(script: str, extra: list[str]) -> tuple[int, str]:
    tools = pathlib.Path(__file__).resolve().parent
    result = subprocess.run([sys.executable, "-I", str(tools / script), *extra], capture_output=True,
                            timeout=600, check=False, cwd=tools.parent)
    return result.returncode, (result.stdout + result.stderr).decode("utf-8", errors="replace")


LIBRARY_PROBE = r"""
import pathlib, sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
import app_driver as d
default = d.HEADLESS
app = d.App(pathlib.Path(sys.argv[2]), [], settings=pathlib.Path(sys.argv[3]))
try:
    state = d.read_state(app.hwnd).get("headless")
    print(f"default={default} app.headless={app.headless} state_headless={state} "
          f"on_a_monitor={d.on_a_monitor(app.hwnd)}")
finally:
    app.kill()
"""


def check_library_default_headless(exe: pathlib.Path, out: pathlib.Path):
    """Review R3: a script that imports app_driver and makes a bare App(...)
    (not app_check, which sets HEADLESS itself) gets a headless geditor."""
    import subprocess
    settings = no_settings(out)
    result = subprocess.run([sys.executable, "-c", LIBRARY_PROBE, str(pathlib.Path(__file__).resolve().parent),
                             str(exe), str(settings)], capture_output=True, text=True, timeout=60)
    line = result.stdout.strip()
    check("[R3] a bare App(...) of the imported app_driver is headless",
          result.returncode == 0 and "default=True" in line and "app.headless=True" in line and
          "state_headless=1" in line and "on_a_monitor=False" in line,
          f"exit={result.returncode} {line} {result.stderr.strip()[-200:]}")


def run_headless_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C2]-[C6] and [C10] of the headless packet: the window, the guard of
    the driver, the private clipboard, every question kind and button, bad
    requests, a failed start and the measurement scripts."""
    work = out / "files_headless"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = no_settings(out)
    target = work / "target.txt"
    target.write_bytes(b"old target\n")
    (work / "other.txt").write_bytes(b"other\n")
    doc = work / "h.txt"
    doc.write_bytes(b"alpha\nbeta\n")

    app = d.App(exe, [str(work / "new.txt")], cwd=work, settings=settings)
    try:
        record("INFO", "headless input mode", app.use_input())
        check_headless_window("[C2] headless window", app.hwnd)
        state = app.state()
        check("[C2] the window reports headless mode", state.get("headless") == "1",
              f"headless={state.get('headless')} settings={state.get('settings')!r}")

        # (d) The capture after typing shows the typed text.
        empty = app.capture(shots / "h_01_empty.png")
        app.type("HEADLESS")
        app.keys("RET")
        app.type("two")
        time.sleep(0.3)
        typed = app.capture(shots / "h_02_typed.png")
        n0, n1 = count(empty, 0xD4D4D4), count(typed, 0xD4D4D4)
        box0, box1 = cursor_box(empty), cursor_box(typed)
        check("[C2] (d) a capture after typing shows the typed text",
              n1 > n0 + 40 and box0 is not None and box1 is not None and box1[1] > box0[1] and
              app.title() == "new.txt* - geditor",
              f"text px {n0} -> {n1}, cursor {box0} -> {box1}, title={app.title()!r} "
              "(h_02_typed.png shows 'HEADLESS' and 'two')")

        # [C3] The runtime guard of the driver.
        guarded = [name for name in d._GUARDED if isinstance(getattr(d.user32, name), d._GuardedFunction)]
        refused = []
        for name, call in [("SendInput", lambda: d.user32.SendInput(0, None, 0)),
                           ("SetForegroundWindow", lambda: d.user32.SetForegroundWindow(None)),
                           ("BringWindowToTop", lambda: d.user32.BringWindowToTop(None)),
                           ("SetActiveWindow", lambda: d.user32.SetActiveWindow(None)),
                           ("OpenClipboard", lambda: d.user32.OpenClipboard(None)),
                           ("ShowWindow 9", lambda: d.user32.ShowWindow(app.hwnd, 9)),
                           ("SetWindowPos move", lambda: d.user32.SetWindowPos(app.hwnd, None, 0, 0, 0, 0,
                                                                               d.SWP_NOSIZE | d.SWP_NOACTIVATE)),
                           ("MoveWindow", lambda: d.user32.MoveWindow(app.hwnd, 0, 0, 10, 10, True))]:
            try:
                call()
            except d.DriverError:
                refused.append(name)
        check("[C3] the driver refuses SendInput, keybd_event, SetForegroundWindow, BringWindowToTop, "
              "SetActiveWindow, the clipboard and window moves in headless mode",
              sorted(guarded) == sorted(d._GUARDED) and len(refused) == 8,
              f"guarded={guarded} refused={refused}")

        # [C10] Requests that geditor does not understand.
        rejected0 = int(app.state()["rejected"])
        replies = {
            "unknown kind 'GXXX'": d.copy_data(app.hwnd, 0x58585847, "x"),
            "odd payload size": d.copy_data(app.hwnd, d.COPYDATA_CAPTURE, "abc", size=5),
            "empty capture path": d.copy_data(app.hwnd, d.COPYDATA_CAPTURE, ""),
            "unknown fault": d.copy_data(app.hwnd, d.COPYDATA_FAULT, "frobnicate"),
            "state into a missing folder": d.copy_data(app.hwnd, d.COPYDATA_STATE,
                                                       str(work / "no_such_folder" / "s.txt")),
            "answer without a question": d.copy_data(app.hwnd, d.COPYDATA_ANSWER, "Yes"),
        }
        state = app.state()
        check("[C10] bad WM_COPYDATA requests are rejected and counted; geditor keeps running",
              all(v == 0 for v in replies.values()) and int(state["rejected"]) == rejected0 + len(replies) and
              app.alive() and state.get("question") == "none",
              f"replies={replies} rejected {rejected0} -> {state['rejected']}, last={state['last_rejected']!r} "
              "(each also goes to OutputDebugString)")

        # [C4] The private clipboard.
        app.keys("C-x h C-w")
        time.sleep(0.3)
        state = app.state()
        check("[C4] kill-region puts the text into the private clipboard with CRLF line ends",
              state.get("clipboard") == "text" and state.get("clipboard_text") == "HEADLESS\r\ntwo" and
              state.get("length") == "0",
              f"clipboard={state.get('clipboard_text')!r} buffer length={state.get('length')}")
        stored = app.set_clipboard("x\r\ny")
        app.keys("C-y")
        time.sleep(0.3)
        state = app.state()
        check("[C4] text put into the private clipboard by the tool is yanked with LF",
              stored and state.get("length") == "3", f"set={stored} buffer length={state.get('length')} "
              "(saved below: target.txt gets b'x\\ny')")

        # [C5] yes / no: "No" keeps the file, "Yes" overwrites it.
        for label in ("No", "Yes"):
            app.keys("C-x C-w")
            time.sleep(0.2)
            app.type(target.name)
            app.keys("RET")
            question = answer(app, label)
            time.sleep(0.4)
            expected = b"old target\n" if label == "No" else b"x\ny"
            data = target.read_bytes()
            check(f"[C5] yes_no: answer '{label}' to overwriting",
                  question is not None and question.get("question") == "yes_no" and
                  question.get("instruction") == f"File {target} exists; overwrite?" and
                  question.get("button") == ["Yes", "No"] and question.get("default") == "No" and data == expected,
                  f"{describe(question)} bytes={data!r}")
        title = app.title()

        # [C4] Forced failures keep the text and say so.
        app.fault("clipboard-write")
        app.keys("C-a C-k")
        time.sleep(0.3)
        state = app.state()
        check("[C4] forced write failure: 'Cannot write to the clipboard', the kill text is kept",
              state.get("message") == "Cannot write to the clipboard" and state.get("length") == "3" and
              state.get("clipboard_text") == "x\r\ny",
              f"message={state.get('message')!r} buffer length={state.get('length')} "
              f"clipboard={state.get('clipboard_text')!r}")
        app.fault("clipboard-read")
        app.keys("C-y")
        time.sleep(0.3)
        state = app.state()
        check("[C4] forced read failure: 'Cannot read the clipboard'",
              state.get("message") == "Cannot read the clipboard" and state.get("length") == "3",
              f"message={state.get('message')!r} buffer length={state.get('length')}")

        # [C5] open choice: a wrong label is rejected, then "Cancel".
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.type("other.txt")
        app.keys("RET")
        question = app.wait_question()
        wrong = app.answer("Maybe")
        state = app.state()
        check("[C10] an answer that names no button is rejected; the question stays",
              question is not None and not wrong and state.get("question") == "open_choice" and
              "no button 'Maybe'" in state.get("last_rejected", ""),
              f"answer 'Maybe' accepted={wrong}, question={state.get('question')}, "
              f"last={state.get('last_rejected')!r}")
        accepted = app.answer("Cancel")
        time.sleep(0.3)
        check("[C5] open_choice: answer 'Cancel' keeps this buffer",
              accepted and app.title() == title and app.state().get("question") == "none",
              f"{describe(question)} title={app.title()!r}")

        # [C6] "Open in a new window" from a headless window.
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.type("other.txt")
        app.keys("RET")
        before = set(d.started_windows())
        question = answer(app, "Open in a new window")
        new = d.wait_for(lambda: [h for h in d.started_windows() if h not in before], 10.0) or []
        check("[C5] open_choice: answer 'Open in a new window' starts another window",
              question is not None and [d.window_text(h) for h in new] == ["other.txt - geditor"],
              f"{describe(question)} new={[d.window_text(h) for h in new]}")
        if new:
            check_headless_new_window(app, new[0])
        for h in new:
            d.user32.PostMessageW(h, d.WM_CLOSE, 0, 0)
        closed = d.wait_for(lambda: not any(d.user32.IsWindow(h) for h in new), 5.0)
        check("[C6] the check closes the new window", bool(new) and bool(closed), f"closed={bool(closed)}")

        # [C5] save changes: "Save" saves and closes.
        app.type("Z")
        app.close()
        question = answer(app, "Save")
        exited = app.wait_exit(5.0)
        data = target.read_bytes()
        check("[C5] save_changes: answer 'Save' writes the file and closes the window",
              question is not None and question.get("question") == "save_changes" and exited and data == b"x\nZy",
              f"{describe(question)} exited={exited} bytes={data!r}")
    finally:
        app.kill()

    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    try:
        app.use_input()
        # [C5] the session end question: "Save" saves and lets the session end.
        app.type("W")
        time.sleep(0.2)
        reply = send_message_async(app.hwnd, 0x0011)  # WM_QUERYENDSESSION
        question = answer(app, "Save")
        reply["thread"].join(25)
        data = doc.read_bytes()
        check("[C5] session end: answer 'Save' writes the file and allows the end",
              question is not None and question.get("question") == "save_changes" and reply.get("value") == 1 and
              data == b"Walpha\nbeta\n", f"{describe(question)} reply={reply.get('value')} bytes={data!r}")
        # [C5] "Don't Save" closes without writing.
        app.type("V")
        app.close()
        question = answer(app, "Don't Save")
        exited = app.wait_exit(5.0)
        data = doc.read_bytes()
        check("[C5] save_changes: answer \"Don't Save\" closes without writing",
              question is not None and exited and data == b"Walpha\nbeta\n",
              f"{describe(question)} exited={exited} bytes={data!r}")
    finally:
        app.kill()

    # [C5] The message box of a failed start: stderr instead of a window.
    env = dict(os.environ, GEDITOR_HEADLESS_FAULT="init")
    result = subprocess.run([str(exe), "--headless", f"--settings={settings}"], env=env, capture_output=True,
                            timeout=20, check=False)
    err = result.stderr.decode("utf-8", errors="replace").strip()
    check("[C5] a failed start in headless mode shows no message box (stderr, exit code 1)",
          result.returncode == 1 and err.startswith("headless_error=init"),
          f"exit={result.returncode} stderr={err!r} (the sampler reports any #32770 window)")

    # [C2] (e) The measurement scripts run headless by default.
    code, text = run_measure_script("measure_startup.py", [str(exe), "--runs", "3", "--limit-ms", "100000"])
    startup = [line for line in text.splitlines() if line.startswith(("mode=", "startup:", "app:"))]
    check("[C2] (e) measure_startup.py prints startup_ms headless",
          code == 0 and "mode=headless" in text and any("median_ms=" in s for s in startup), " | ".join(startup))
    code, text = run_measure_script("measure_input.py", [str(exe), "--chars", "5", "--limit-ms", "100000"])
    lines = [line for line in text.splitlines() if "median_ms" in line or line.startswith("mode=")]
    check("[C2] (e) measure_input.py prints input_ms headless",
          code == 0 and "mode=headless" in text and any(s.startswith("lines_100mb:") for s in lines),
          " | ".join(lines))


# ---- --only-visible: the checks that need the real screen ----

def run_visible_checks(exe: pathlib.Path, out: pathlib.Path):
    """The checks that a headless run reports as "needs --visible": keys
    through SendInput, the real clipboard and the task dialogs."""
    work = out / "files_visible"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = no_settings(out)
    a = work / "a.txt"
    a.write_bytes(b"one\ntwo\n")
    (work / "other.txt").write_bytes(b"other\n")
    target = work / "target.txt"
    target.write_bytes(b"old\n")
    app = d.App(exe, [str(a)], cwd=work, settings=settings)
    try:
        mode = app.use_input()
        if mode == "sendinput":
            app.type("S")
            app.keys("C-x C-s")
            time.sleep(0.3)
            check("[visible] keys through SendInput to the foreground window", a.read_bytes() == b"Sone\ntwo\n",
                  f"bytes={a.read_bytes()!r}")
        else:
            record("SKIP", "[visible] keys through SendInput to the foreground window",
                   "the window cannot take the foreground (locked desktop or --input post)")
        if clipboard_accessible():
            app.keys("M-< C-k")
            time.sleep(0.3)
            clip = clipboard_text()
            set_clipboard_text("A\r\nB")
            app.keys("C-y C-x C-s")
            time.sleep(0.3)
            data = a.read_bytes()
            ok = clip in ("one", "Sone") and data.startswith(b"A\nB\n")
            if d.user32.OpenClipboard(None):
                try:
                    app.keys("C-a C-k")
                    time.sleep(0.4)
                    app.capture(shots / "v_01_clipboard_held.png")
                finally:
                    d.user32.CloseClipboard()
            app.keys("C-x C-s")
            time.sleep(0.3)
            check("[visible] the real clipboard: C-k / C-y through CF_UNICODETEXT",
                  ok, f"clipboard after C-k={clip!r} bytes after C-y={data!r}")
            check("[visible] clipboard held open by another program", a.read_bytes() == data,
                  f"bytes={a.read_bytes()!r} (v_01_clipboard_held.png: 'Cannot write to the clipboard')")
        else:
            record("SKIP", "[visible] the real clipboard: C-k / C-y through CF_UNICODETEXT",
                   f"OpenClipboard failed (error {ctypes.get_last_error()})")
            record("SKIP", "[visible] clipboard held open by another program", "the clipboard cannot be opened")
        # The task dialogs: open choice (Cancel), yes / no (No), save
        # changes (Cancel, then Don't Save at the session end).
        app.keys("C-x C-f")
        time.sleep(0.2)
        app.type("other.txt")
        app.keys("RET")
        open_dialog = answer(app, "Cancel", shots / "v_02_open_dialog.png")
        app.refocus()
        app.keys("C-x C-w")
        time.sleep(0.2)
        app.type(target.name)
        app.keys("RET")
        yes_no = answer(app, "No", shots / "v_03_yes_no_dialog.png")
        app.refocus()
        app.type("Q")
        app.close()
        save = answer(app, "Cancel", shots / "v_04_save_dialog.png")
        reply = send_message_async(app.hwnd, 0x0011)  # WM_QUERYENDSESSION
        session = answer(app, "Don't Save", shots / "v_05_session_end_dialog.png")
        reply["thread"].join(25)
        check("[visible] task dialogs: their look and their buttons",
              all(q is not None for q in (open_dialog, yes_no, save, session)) and app.alive() and
              target.read_bytes() == b"old\n" and reply.get("value") == 1,
              f"open={describe(open_dialog)} yes_no={describe(yes_no)} save={describe(save)} "
              f"session={describe(session)} reply={reply.get('value')} "
              "(v_02..v_05 show the dialogs: English buttons, the instruction and the path)")
    finally:
        app.kill()
    # The system's Alt+F4 closes the active window.
    app = d.App(exe, [str(target)], cwd=work, settings=settings)
    try:
        app.use_input()
        app.keys("M-<f4>")
        check("M-<f4> closes the window", app.wait_exit(5.0), f"alive={app.alive()} input mode {app.mode}")
    finally:
        app.kill()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path,
                        default=pathlib.Path("build") / "app_check" / time.strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--strict", action="store_true", help="exit 1 when any check is SKIP")
    parser.add_argument("--only-retry1", action="store_true",
                        help="run only the checks added in retry 1 ([S1]-[S5])")
    parser.add_argument("--only-m2", action="store_true",
                        help="run only the M2 settings checks")
    parser.add_argument("--only-m3a", action="store_true",
                        help="run only the M3a display checks")
    parser.add_argument("--input", choices=["auto", "post"], default="auto",
                        help="post: always post the keys (never take the foreground; --visible only)")
    parser.add_argument("--visible", action="store_true",
                        help="real screen, foreground, clipboard and task dialogs instead of --headless")
    parser.add_argument("--only-headless", action="store_true",
                        help="run only the checks of the headless mode itself")
    parser.add_argument("--only-visible", action="store_true",
                        help="with --visible: run only the checks that need the real screen")
    args = parser.parse_args()
    if args.only_visible and not args.visible:
        parser.error("--only-visible needs --visible")
    if args.only_headless and args.visible:
        parser.error("--only-headless cannot be combined with --visible")
    d.HEADLESS = not args.visible
    d.FORCE_POST = args.input == "post"
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print(f"mode {'headless' if d.HEADLESS else 'visible'}", flush=True)
    # Headless: the real clipboard is never opened; its sequence number
    # (no open needed) shows that nothing wrote to it.
    saved = None if d.HEADLESS else clipboard_text()
    sequence0 = d.user32.GetClipboardSequenceNumber()
    only = args.only_retry1 or args.only_m2 or args.only_m3a or args.only_headless or args.only_visible
    exe, out = args.exe.resolve(), args.out.resolve()
    sampler = None
    if d.HEADLESS:
        sampler = d.ForegroundSampler(exe, 0.02)
        sampler.start()
    try:
        if args.only_visible:
            run_visible_checks(exe, out)
        if not only:
            run_checks(exe, out)
        if not only or args.only_retry1:
            run_retry1_checks(exe, out)
        if not only or args.only_m2:
            run_m2_checks(exe, out)
        if not only or args.only_m3a:
            run_m3a_checks(exe, out)
        if d.HEADLESS and (not only or args.only_headless):
            run_headless_checks(exe, out)
            check_library_default_headless(exe, out)
    except d.DriverError as exc:
        record("FAIL", "driver", str(exc))
    finally:
        if saved is not None:
            set_clipboard_text(saved)
        if sampler is not None:
            sampler.stop()
    if sampler is not None:
        sequence1 = d.user32.GetClipboardSequenceNumber()
        check("[C4] the real clipboard was not written (GetClipboardSequenceNumber)", sequence0 == sequence1,
              f"sequence number {sequence0} -> {sequence1} (its text is not read: that would open it)")
        check("[C2] (b) no geditor window was ever the foreground window",
              not sampler.foreground_hits and sampler.geditor_windows_seen > 0 and sampler.gaps_over_50ms == 0,
              f"{sampler.samples} samples of GetForegroundWindow (a process of its own, every 20 ms), "
              f"max gap {sampler.max_gap_s * 1000:.0f} ms, {sampler.gaps_over_50ms} gaps over 50 ms, "
              f"{sampler.geditor_windows_seen} geditor windows of {exe.name} seen, "
              f"gaps over 50 ms [s, gap ms]={sampler.long_gaps[:10]}, "
              f"{sampler.foreground_windows} different foreground windows, "
              f"hits={sampler.foreground_hits[:5]}")
        check("[H1] no headless geditor window was ever on a monitor or iconic",
              not sampler.on_screen and sampler.window_checks > 0,
              f"{sampler.window_checks} checks of the headless windows (every 20 ms, in the sampler process), "
              f"on a monitor or iconic={sampler.on_screen[:5]}")
        check("[C5] no task dialog (#32770) of a geditor process appeared", not sampler.dialogs,
              f"{sampler.dialog_scans} scans of the visible #32770 windows (every 20 ms, longest gap "
              f"{sampler.dialog_scan_max_gap_s * 1000:.0f} ms), dialogs={sampler.dialogs[:5]}")
    passed = sum(1 for s, _ in results if s == "PASS")
    failed = sum(1 for s, _ in results if s == "FAIL")
    skipped = sum(1 for s, _ in results if s == "SKIP")
    skipped_visible = sum(1 for s, name in results if s == "SKIP" and name in visible_only)
    print(f"output: {out}")
    print(f"{passed} passed, {failed} failed, {skipped} skipped ({skipped_visible} need --visible)")
    if failed:
        return 1
    if skipped - skipped_visible and args.strict:
        print("--strict: skipped checks count as failures (the 'needs --visible' ones do not)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
