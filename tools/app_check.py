"""Real-app checks of geditor M1, M2, M3a, M3b and M3c (run by hand; Windows only).

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
table (item | screenshot | what is seen). The M3b checks (--only-m3b) post
mouse messages at the cells that the state's grid gives (clicks, drags,
double-clicks, Shift+clicks, the wheel), read the real menu bar of the
window (GetMenu and GetMenuItemInfoW after WM_INITMENUPOPUP), invoke items
with WM_COMMAND, and answer the menus that F10 and WM_CONTEXTMENU leave
pending in the headless state ('GSTA' / 'GANS'). The M3c checks
(--only-m3c) look at the pixels of grapheme clusters in their cells (U+FE0F,
a ZWJ family, a flag, a keycap, か + U+3099), DEL and undo by cluster, a
failed shaping ('GFLT' "shape") and a cluster without glyphs, and F10 in
the headless state; with --visible, F10 through SendInput drops the File
menu down (Left / Right / Esc, read from the state's menu_popup).

Headless (the default; the line "mode headless" says so): every geditor
gets --headless. Its window is outside every monitor and never activated,
keys are posted as WM_KEYDOWN / WM_CHAR with the modifier state set through
AttachThreadInput + SetKeyboardState, the clipboard checks use geditor's
private clipboard (read through WM_COPYDATA 'GSTA', written with 'GCBW',
failures forced with 'GFLT'), and the questions are read and answered
through 'GSTA' / 'GANS' instead of task dialogs. The real clipboard is
never opened (its sequence number is compared before and after the run), a
sampler checks every 20 ms that no geditor window is the foreground and
that no geditor task dialog, menu or popup menu (#32768) appears, and
app_driver refuses the calls that
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
        write_settings(settings, b'{\n  "keys": { "<f5>": ["insert-text", "BAD"] }\n'
                                 b'  "tab_width": 8\n}\n')
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
        write_settings(settings, b'{\n  "tab_widht": 4,\n  "colors": { "cursor": "red" },\n  "keys": {\n'
                                 b'    "C-c a": "no-such-command",\n    "C-x": "undo",\n'
                                 b'    "<f5>": ["insert-text", "M2OK"]\n  }\n}\n')
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
        write_settings(settings, ("{\n" + "".join(f'  "bad{i}": 1,\n' for i in range(12)) + "}\n").encode())
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
    write_settings(settings, settings_json(items), wait_s)


def write_settings(settings: pathlib.Path, data: bytes, wait_s: float = 1.0):
    """Writes the settings file and waits until every running App that reads
    it has applied the reload and drawn a frame with it (GSTA
    "settings_reloads" / "reloads_drawn"), not for a fixed time: a busy PC
    can take longer than any sleep (R4). The count must then stay the same
    for 0.3 s (the watcher can see one save as two changes). Without such
    an App, it sleeps `wait_s`."""
    apps = d.apps_using_settings(settings)
    before = {app.hwnd: int(app.state().get("settings_reloads", "0")) for app in apps}
    settings.write_bytes(data)
    if not apps:
        time.sleep(wait_s)  # the watcher waits 200 ms, then the frame
        return
    if not d.wait_settings_applied(apps, before, 15.0):
        raise d.DriverError(f"the settings change was not applied within 15 s ({settings.name})")
    while True:
        counts = {app.hwnd: int(app.state()["settings_reloads"]) for app in apps}
        time.sleep(0.3)
        if all(int(app.state()["settings_reloads"]) == counts[app.hwnd] for app in apps):
            break
        if not d.wait_settings_applied(apps, {h: c for h, c in counts.items()}, 15.0):
            raise d.DriverError(f"a repeated settings reload was not drawn within 15 s ({settings.name})")


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


# ---- M3b: menus, the right-click menu and the mouse ----

# The command items of PLAN 4.9 with the key text of decision B1 (built-in
# keys, no settings) and whether the command is implemented in M3b (else
# grey). tests/test_menu.cpp holds the same table for the core model.
M3B_ITEMS = [
    ("File > Open...", "C-x C-f", True), ("File > Open with Encoding...", "C-x RET c", False),
    ("File > Reopen with Encoding...", "C-x RET r", False), ("File > Save", "C-M-s", True),
    ("File > Save As...", "C-x C-w", True),
    *[(f"File > Encoding for Saving > {name}", "", False) for name in (
        "utf-8", "utf-8-bom", "utf-16le", "utf-16le-bom", "utf-16be", "utf-16be-bom", "shift_jis", "euc-jp",
        "iso-2022-jp")],
    ("File > Line Endings > LF", "", False), ("File > Line Endings > CRLF", "", False),
    ("File > Line Endings > CR", "", False), ("File > Read Only", "C-x C-q", True),
    ("File > Settings", "", True), ("File > Exit", "C-M-w", True),
    ("Edit > Undo", "C-M-z", True), ("Edit > Redo", "C-M-y", True), ("Edit > Cut", "C-M-x", True),
    ("Edit > Copy", "C-M-c", True), ("Edit > Paste", "C-M-v", True), ("Edit > Select All", "C-M-a", True),
    ("Edit > Find...", "C-M-f", False), ("Edit > Replace...", "C-M-h", False),
    ("Edit > Go to Line...", "C-M-g", False),
    ("View > Line Numbers", "", True), ("View > Line Ending Marks", "", True),
    ("View > Whitespace Marks", "", True), ("View > Wrap > None", "", True), ("View > Wrap > Window", "", True),
    ("View > Wrap > Fixed", "", True), ("View > Wrap > Max", "", True), ("View > Zoom In", "C-x C-+", True),
    ("View > Zoom Out", "C-x C--", True), ("View > Reset Zoom", "C-x C-0", True),
]
M3B_CONTEXT = ["Undo", "Redo", "-", "Cut", "Copy", "Paste", "Select All"]


class M3bRun:
    """Helpers of the M3b checks: screenshots and the state of one app."""

    def __init__(self, app, out: pathlib.Path):
        self.app = app
        self.shots = out / "shots"
        self.shot_names = []

    def shot(self, name: str):
        self.shot_names.append(name)
        return self.app.capture(self.shots / name)

    def state(self) -> dict:
        return self.app.state()

    def cell(self, row: int, column: int) -> tuple[int, int]:
        return self.app.cell_point(row, column)

    def items(self) -> dict:
        return {item.path: item for item in self.app.menu_bar()}


def pmt(state: dict) -> str:
    return (f"point={state.get('point')} mark={state.get('mark')} active={state.get('mark_active')} "
            f"top={state.get('top_line')}/{state.get('top_row')}")


def wheel_lines() -> int:
    lines = wt.UINT(3)
    d.user32.SystemParametersInfoW(0x0068, 0, ctypes.byref(lines), 0)  # SPI_GETWHEELSCROLLLINES
    return lines.value


def run_m3b_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C3]-[C6] and [C10] of the M3b packet on the real app (headless: posted
    mouse messages, the pending menus of the state, WM_COMMAND)."""
    work = out / "files_m3b"
    work.mkdir(parents=True, exist_ok=True)
    settings = work / "settings.json"
    wrap10 = {"wrap": {"mode": "fixed", "column": 10}}
    settings.write_bytes(settings_json(wrap10))
    doc = work / "m3b.txt"
    lines = ["hello word",            # line 1, offset 0: exactly 10 cells
             "a + b",                 # line 2, offset 11
             "あいう",                # line 3, offset 17: full width
             "0123456789abcdefghij",  # line 4, offset 21: two rows
             "漢字かな"]              # line 5, offset 42
    lines += [f"line{i:02d}" for i in range(6, 81)]
    original = "\n".join(lines) + "\n"
    doc.write_bytes(original.encode("utf-8"))
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    run = M3bRun(app, out)
    try:
        record("INFO", "M3b input mode", app.use_input())
        time.sleep(0.4)
        s0 = run.state()
        record("INFO", "M3b grid", f"line height {s0['line_height']}px cell {s0['cell_width']}px text left "
               f"{s0['text_left']}px rows {s0['text_rows']} mode line top {s0['mode_line_top']}px "
               f"wheel lines {wheel_lines()}")
        run_m3b_mouse(app, run, original)
        run_m3b_strip(app, run, original)
        run_m3b_context(app, run, original, settings, wrap10)
        run_m3b_menu_bar(app, run, settings, wrap10)
    finally:
        app.kill()
    record("INFO", "[C5] screenshots", ", ".join(run.shot_names))
    for name, reason in [
        ("[C6] F10 and a mouse click open the real menu loop (look, mnemonics, Esc)",
         "a real menu loop shows on the screen; headless records the menu instead"),
        ("[C6] Alt+Space opens the system menu", "the system menu shows on the screen; headless swallows SC_KEYMENU"),
        ("[C4] the real right-click popup (look, position at the mouse)",
         "TrackPopupMenu shows on the screen; headless records the popup instead"),
        ("[C5] the mouse cursor is an I-beam over text and an arrow elsewhere",
         "WM_SETCURSOR reads the real cursor position (GetCursorPos)"),
    ]:
        needs_visible(name, reason)


def run_m3b_mouse(app, run: M3bRun, original: str):
    keys = app.keys
    # ---- clicks (decision B5) ----
    cases = [
        ("click on ASCII (row 0, cell 3)", 0, 3, 3, "m3b_01_click_ascii.png"),
        ("click on the left half of a full-width character (row 2, cell 2: 'い')", 2, 2, 18,
         "m3b_02_click_wide_left.png"),
        ("click on the right half of a full-width character (row 2, cell 3: 'い')", 2, 3, 18,
         "m3b_03_click_wide_right.png"),
        ("click right of the line end (row 1, cell 8)", 1, 8, 16, "m3b_04_click_past_end.png"),
        ("click in the reserved last cell of a full row (row 0, cell 10: the line end)", 0, 10, 10,
         "m3b_05_click_reserved_end.png"),
        ("click in the reserved last cell of a wrapped row (row 3, cell 10: before its last character)", 3, 10,
         30, "m3b_06_click_reserved_wrapped.png"),
        ("click on a continuation row (row 4, cell 2)", 4, 2, 33, "m3b_07_click_continuation.png"),
        ("click in the line-number gutter (row 4: the row start)", 4, -1, 31, "m3b_08_click_gutter.png"),
    ]
    for name, row, column, expected, shot in cases:
        x, y = run.cell(row, column)
        app.click(x, y)
        time.sleep(0.15)
        state = run.state()
        run.shot(shot)
        check(f"[C5] {name}", state["point"] == str(expected) and state["mark_active"] == "0",
              f"posted at ({x}, {y}): {pmt(state)} expected point {expected} ({shot})")

    # Below the last line: the end of the buffer (the last line recentred).
    s = run.state()
    x, y = run.cell(int(s["text_rows"]) - 1, 2)
    keys("M-> C-l")
    time.sleep(0.2)
    app.click(x, y)
    time.sleep(0.15)
    state = run.state()
    run.shot("m3b_09_click_below_last_line.png")
    check("[C5] click below the last line goes to the end of the buffer",
          state["point"] == str(len(original)), f"posted at ({x}, {y}): {pmt(state)} length {len(original)} "
          "(m3b_09_click_below_last_line.png)")

    # ---- drag ----
    keys("M-<")
    time.sleep(0.2)
    x0, y0 = run.cell(0, 2)
    x1, y1 = run.cell(2, 2)
    app.mouse(d.WM_LBUTTONDOWN, x0, y0, d.MK_LBUTTON)
    app.mouse(d.WM_MOUSEMOVE, (x0 + x1) // 2, (y0 + y1) // 2, d.MK_LBUTTON)
    app.mouse(d.WM_MOUSEMOVE, x1, y1, d.MK_LBUTTON)
    time.sleep(0.15)
    during = run.state()
    run.shot("m3b_10_drag_rows.png")
    app.mouse(d.WM_LBUTTONUP, x1, y1)
    state = run.state()
    check("[C5] drag selection across rows: mark at the press, point follows, region shown live",
          during["mark"] == "2" and during["point"] == "18" and during["mark_active"] == "1" and
          during["dragging"] == "1" and state["dragging"] == "0" and state["point"] == "18",
          f"during: {pmt(during)} dragging={during['dragging']}; after release: {pmt(state)} "
          "(m3b_10_drag_rows.png: highlighted from 'llo word' to 'あ')")

    # Beyond the bottom: the view scrolls on a timer while held, stops on release.
    keys("M-<")
    time.sleep(0.2)
    s = run.state()
    x, y = run.cell(0, 0)
    below = int(float(s["mode_line_top"])) + 4
    app.mouse(d.WM_LBUTTONDOWN, x, y, d.MK_LBUTTON)
    app.mouse(d.WM_MOUSEMOVE, x, below, d.MK_LBUTTON)
    time.sleep(0.5)
    top_a = int(run.state()["top_line"])
    time.sleep(0.4)
    held = run.state()
    run.shot("m3b_11_drag_autoscroll.png")
    app.mouse(d.WM_LBUTTONUP, x, below)
    released = run.state()
    time.sleep(0.5)
    later = run.state()
    check("[C5] a drag below the text scrolls by rows while held and stops on release",
          0 < top_a < int(held["top_line"]) and held["mark"] == "0" and held["mark_active"] == "1" and
          released["top_line"] == later["top_line"] and later["dragging"] == "0" and
          later["point"] == released["point"],
          f"top 0 -> {top_a} (0.5 s) -> {held['top_line']} (0.9 s), released at {released['top_line']}, "
          f"0.5 s later {later['top_line']}; {pmt(later)} (m3b_11_drag_autoscroll.png)")

    # The negative: dragging from inside the selection makes a new one.
    keys("M-< C-x h")
    time.sleep(0.2)
    x0, y0 = run.cell(0, 2)
    x1, y1 = run.cell(1, 1)
    app.mouse(d.WM_LBUTTONDOWN, x0, y0, d.MK_LBUTTON)
    app.mouse(d.WM_MOUSEMOVE, x1, y1, d.MK_LBUTTON)
    app.mouse(d.WM_LBUTTONUP, x1, y1)
    time.sleep(0.15)
    state = run.state()
    run.shot("m3b_12_drag_inside_selection.png")
    check("[C5] (negative) a drag from inside the selection starts a new selection and moves no text",
          state["mark"] == "2" and state["point"] == "12" and state["mark_active"] == "1" and
          state["text"] == original and state["modified"] == "0",
          f"{pmt(state)} text unchanged={state['text'] == original} modified={state['modified']} "
          "(m3b_12_drag_inside_selection.png)")

    # ---- double-click ----
    for name, row, column, mark, point, shot in [
            ("on a word selects the word ('hello')", 0, 1, 0, 5, "m3b_13_double_click_word.png"),
            ("on a non-word character selects that character ('+')", 1, 2, 13, 14,
             "m3b_14_double_click_symbol.png"),
            ("on kanji selects the kanji word (as M-f)", 5, 1, 42, 44, "m3b_15_double_click_kanji.png")]:
        x, y = run.cell(row, column)
        app.double_click(x, y)
        time.sleep(0.15)
        state = run.state()
        run.shot(shot)
        check(f"[C5] double-click {name}", state["mark"] == str(mark) and state["point"] == str(point) and
              state["mark_active"] == "1", f"{pmt(state)} expected {mark}..{point} ({shot})")

    # ---- Shift+click ----
    x, y = run.cell(0, 0)
    app.click(x, y)
    x, y = run.cell(0, 5)
    app.click(x, y, shift=True)
    time.sleep(0.15)
    state = run.state()
    run.shot("m3b_16_shift_click_no_mark.png")
    check("[C5] Shift+click without an active mark extends from the old point",
          state["mark"] == "0" and state["point"] == "5" and state["mark_active"] == "1",
          f"{pmt(state)} (m3b_16_shift_click_no_mark.png)")
    keys("M-< C-SPC C-f C-f")
    x, y = run.cell(1, 2)
    app.click(x, y, shift=True)
    time.sleep(0.15)
    state = run.state()
    run.shot("m3b_17_shift_click_mark.png")
    check("[C5] Shift+click with an active mark extends from that mark",
          state["mark"] == "0" and state["point"] == "13" and state["mark_active"] == "1",
          f"{pmt(state)} (m3b_17_shift_click_mark.png)")

    # ---- wheel ----
    lines = wheel_lines()
    # The screen rows of the file: line 4 has two (wrap at 10), the others one.
    text_lines = original.split("\n")
    rows = [(i, r) for i, line in enumerate(text_lines) for r in range(2 if i == 3 else 1)]
    starts = [sum(len(t) + 1 for t in text_lines[:i]) + 10 * r for i, r in rows]
    keys("M-<")
    time.sleep(0.2)
    app.wheel(-120)
    down = run.state()
    run.shot("m3b_18_wheel_down.png")
    app.wheel(120)
    up = run.state()
    step = -(120 // (2 * lines)) if 0 < lines <= 60 else -60
    app.wheel(step)
    half = run.state()
    app.wheel(step)
    whole = run.state()
    run.shot("m3b_19_wheel_partial.png")
    ok = 0 < lines <= 60
    expected_top = rows[lines] if ok else None
    check("[C5] wheel down / up scroll by the rows of a notch; point moves only to stay visible",
          ok and (int(down["top_line"]), int(down["top_row"])) == expected_top and
          down["point"] == str(starts[lines]) and up["top_line"] == "0" and up["point"] == down["point"],
          f"SPI_GETWHEELSCROLLLINES={lines}: down top={down['top_line']} point={down['point']}; "
          f"up top={up['top_line']} point={up['point']} (m3b_18_wheel_down.png)")
    check("[C5] wheel: partial deltas accumulate into a row",
          ok and half["top_line"] == "0" and whole["top_line"] == "1",
          f"two deltas of {step}: top {up['top_line']} -> {half['top_line']} -> {whole['top_line']} "
          "(m3b_19_wheel_partial.png)")

    # ---- the undo group, and clicks that do nothing ----
    keys("M-<")
    app.type("XY")
    x, y = run.cell(1, 0)
    app.click(x, y)
    app.type("Z")
    keys("C-/")
    time.sleep(0.2)
    state = run.state()
    run.shot("m3b_20_click_undo_group.png")
    ok = state["text"].startswith("XYhello word\na + b")
    keys("C-/")
    time.sleep(0.2)
    restored = run.state()["text"] == original
    check("[C5] a click ends the typing undo group (type, click, type, C-/ undoes the second run only)",
          ok and restored, f"after C-/: {state['text'][:22]!r}; second C-/ restores the file: {restored} "
          "(m3b_20_click_undo_group.png)")

    keys("M-< C-f C-f")
    time.sleep(0.2)
    before = run.state()
    top = float(before["mode_line_top"])
    lh = float(before["line_height"])
    x, _ = run.cell(0, 3)
    app.click(x, int(top + lh / 2))
    mode = run.state()
    app.click(x, int(top + lh * 1.5))
    echo = run.state()
    keys("M-x")
    x2, y2 = run.cell(1, 2)
    app.click(x2, y2)
    app.double_click(x2, y2)
    mini = run.state()
    run.shot("m3b_21_click_minibuffer.png")
    keys("C-g")
    check("[C5] clicks in the mode line or the echo area and while the minibuffer is active do nothing",
          mode["point"] == before["point"] == echo["point"] == mini["point"] == "2" and mini["minibuffer"] == "1"
          and mini["mark_active"] == "0",
          f"before {pmt(before)}; mode line {pmt(mode)}; echo area {pmt(echo)}; minibuffer {pmt(mini)} "
          "(m3b_21_click_minibuffer.png)")


def run_m3b_strip(app, run: M3bRun, original: str):
    """R1: client heights that are no multiple of the line height leave a
    blank strip between the last whole row and the mode line. A press,
    double-click or Shift+click there acts on the last whole row and does
    not scroll."""
    text_lines = original.split("\n")
    rows = [(i, r) for i, line in enumerate(text_lines) for r in range(2 if i == 3 else 1)]
    starts = [sum(len(t) + 1 for t in text_lines[:i]) + 10 * r for i, r in rows]
    width, _ = d.client_size(app.hwnd)
    rect = d.window_rect(app.hwnd)
    frame_w = (rect.right - rect.left) - width
    frame_h = (rect.bottom - rect.top) - d.client_size(app.hwnd)[1]
    try:
        for client_height in (305, 310, 315):
            app.resize(width + frame_w, client_height + frame_h)
            time.sleep(0.3)
            app.keys("M-<")
            time.sleep(0.2)
            s = run.state()
            got_height = d.client_size(app.hwnd)[1]
            lh, n = float(s["line_height"]), int(s["text_rows"])
            strip_top, strip_bottom = n * lh, float(s["mode_line_top"])
            if strip_bottom - strip_top < 2:
                record("INFO", f"[R1] client height {got_height}px", f"no strip (rows {n} x {lh}px, mode line top "
                       f"{strip_bottom}px)")
                continue
            y = int((strip_top + strip_bottom) / 2)
            x, _ = run.cell(0, 2)
            last = starts[n - 1]
            app.click(x, y)
            time.sleep(0.15)
            pressed = run.state()
            shot = f"m3b_29_strip_{got_height}.png"
            run.shot(shot)
            app.double_click(x, y)
            time.sleep(0.15)
            double = run.state()
            app.keys("C-g M-<")
            app.click(x, y, shift=True)
            time.sleep(0.15)
            shifted = run.state()
            check(f"[R1] client height {got_height}px: a click in the strip below the last row "
                  f"({strip_top:.0f}-{strip_bottom:.0f}px) acts on the last row and does not scroll",
                  pressed["top_line"] == double["top_line"] == shifted["top_line"] == "0" and
                  pressed["point"] == str(last + 2) and double["mark"] == str(last) and
                  double["mark_active"] == "1" and shifted["mark"] == "0" and shifted["point"] == str(last + 2),
                  f"posted at ({x}, {y}), {n} rows of {lh}px; press: {pmt(pressed)} (expected point {last + 2}); "
                  f"double-click: {pmt(double)}; Shift+click: {pmt(shifted)} ({shot})")
    finally:
        app.resize(rect.right - rect.left, rect.bottom - rect.top)
        time.sleep(0.3)


def answer_menu(app, label: str) -> tuple[dict | None, bool]:
    question = app.wait_question(3.0)
    if question is None:
        return None, False
    return question, app.answer(label)


def run_m3b_context(app, run: M3bRun, original: str, settings: pathlib.Path, wrap10: dict):
    keys = app.keys
    # Right-click inside an active region: point and mark do not move.
    keys("M-< C-SPC C-f C-f C-f")
    time.sleep(0.2)
    before = run.state()
    x, y = run.cell(1, 2)
    app.right_click(x, y)
    question = app.wait_question(3.0)
    pending = run.state()
    run.shot("m3b_22_right_click_region.png")
    items = pending.get("menu_item", [])
    check("[C4] right-click inside the region: a pending context menu at the mouse; point and mark unchanged",
          question is not None and question.get("question") == "context_menu" and
          (before["point"], before["mark"], before["mark_active"]) == (pending["point"], pending["mark"],
                                                                       pending["mark_active"]) == ("3", "0", "1")
          and pending.get("menu_x") == str(x) and pending.get("menu_y") == str(y) and
          [i.split("\t")[0] for i in items] == M3B_CONTEXT,
          f"before {pmt(before)}; pending {pmt(pending)} at ({pending.get('menu_x')}, {pending.get('menu_y')}) "
          f"click ({x}, {y}); items={items}")
    wrong = app.answer("Frobnicate")
    state = run.state()
    check("[C10] a menu answer that names no item is rejected and logged; the menu stays",
          not wrong and state.get("question") == "context_menu" and
          "no button 'Frobnicate'" in state.get("last_rejected", ""),
          f"accepted={wrong} question={state.get('question')} last_rejected={state.get('last_rejected')!r}")
    accepted = app.answer("Copy")
    time.sleep(0.3)
    state = run.state()
    check("[C4] context menu Copy copies the region (private clipboard)",
          accepted and state.get("clipboard_text") == "hel" and state["question"] == "none",
          f"clipboard={state.get('clipboard_text')!r} {pmt(state)}")

    keys("M-< C-SPC C-f C-f C-f")
    app.right_click(*run.cell(0, 1))
    _, accepted = answer_menu(app, "Cut")
    time.sleep(0.3)
    cut = run.state()
    app.right_click(*run.cell(0, 1))
    _, accepted2 = answer_menu(app, "Paste")
    time.sleep(0.3)
    pasted = run.state()
    app.right_click(*run.cell(0, 1))
    _, accepted3 = answer_menu(app, "Undo")
    time.sleep(0.3)
    undone = run.state()
    run.shot("m3b_23_context_undo.png")
    check("[C4] context menu Cut / Paste / Undo run their commands",
          accepted and accepted2 and accepted3 and cut["text"] == original[3:] and
          cut.get("clipboard_text") == "hel" and pasted["text"] == "hel" + original[3:] and
          undone["text"] == original[3:],
          f"cut: {cut['text'][:12]!r} clipboard {cut.get('clipboard_text')!r}; paste: {pasted['text'][:12]!r}; "
          f"undo: {undone['text'][:12]!r}")
    keys("C-/")
    app.right_click(*run.cell(3, 1))
    _, accepted = answer_menu(app, "Select All")
    time.sleep(0.3)
    state = run.state()
    run.shot("m3b_24_context_select_all.png")
    check("[C4] context menu Select All selects the buffer",
          accepted and state["text"] == original and state["mark"] == "0" and
          state["point"] == str(len(original)) and state["mark_active"] == "1", pmt(state))

    # From the keyboard (lParam -1): at the cursor.
    keys("M-< C-n C-f C-f")
    time.sleep(0.3)
    before = run.state()
    app.context_menu()
    question = app.wait_question(3.0)
    pending = run.state()
    app.answer("Cancel")
    check("[C4] a keyboard WM_CONTEXTMENU (lParam -1) opens at the cursor",
          question is not None and pending.get("menu_x") == before["caret_x"] and
          pending.get("menu_y") == before["caret_bottom"] and pending["point"] == before["point"],
          f"menu at ({pending.get('menu_x')}, {pending.get('menu_y')}), cursor bottom-left "
          f"({before['caret_x']}, {before['caret_bottom']})")

    # Forced failures are reported.
    app.fault("menu")
    app.right_click(*run.cell(0, 1))
    time.sleep(0.3)
    created = run.state()
    app.fault("popup")
    app.right_click(*run.cell(0, 1))
    time.sleep(0.3)
    shown = run.state()
    run.shot("m3b_25_context_failure.png")
    check("[C10] a failed context menu creation / TrackPopupMenu is reported in the echo area",
          created["message"].startswith("Cannot create the context menu (error ") and
          shown["message"].endswith(")") and "Cannot show the context menu (error " in shown["message"] and
          shown["question"] == "none",
          f"menu fault: {created['message']!r}; popup fault: {shown['message']!r} (m3b_25_context_failure.png)")

    # With menu_bar: false the context menu still opens.
    apply_settings(settings, {**wrap10, "menu_bar": False})
    state = run.state()
    hidden = d.user32.GetMenu(app.hwnd)
    keys("M-< C-SPC C-f")
    app.right_click(*run.cell(1, 1))
    question, accepted = answer_menu(app, "Copy")
    time.sleep(0.3)
    copied = run.state()
    check("[C4] with menu_bar: false the context menu still works",
          state["menu_bar"] == "0" and not hidden and question is not None and accepted and
          copied.get("clipboard_text") == "h",
          f"menu_bar={state['menu_bar']} GetMenu={hidden} clipboard={copied.get('clipboard_text')!r}")
    apply_settings(settings, wrap10)


def run_m3b_menu_bar(app, run: M3bRun, settings: pathlib.Path, wrap10: dict):
    keys = app.keys
    keys("C-g M-<")
    time.sleep(0.2)
    # [C3] The real HMENU (GetMenu, after WM_INITMENUPOPUP) has the core's
    # texts, key texts, grey states and marks.
    items = run.items()
    titles = [i.path for i in app.menu_bar() if " > " not in i.path]
    problems = []
    for path, key, implemented in M3B_ITEMS:
        item = items.get(path)
        if item is None:
            problems.append(f"missing {path}")
        elif item.key != key or item.enabled != implemented:
            problems.append(f"{path}: key {item.key!r} enabled {item.enabled}")
    marks = {p: items[p].checked for p in ("View > Line Numbers", "View > Line Ending Marks",
                                           "View > Whitespace Marks", "View > Wrap > Fixed", "View > Wrap > Max",
                                           "View > Wrap > None") if p in items}
    radios = [p for p, i in items.items() if i.radio]
    count = len(app.menu_bar())
    check("[C3] the real menu bar (GetMenu after WM_INITMENUPOPUP) has the core's texts, keys, grey items and "
          "marks for File, Edit and View",
          titles == ["File", "Edit", "View"] and not problems and count == 52 and
          marks == {"View > Line Numbers": True, "View > Line Ending Marks": True, "View > Whitespace Marks": True,
                    "View > Wrap > Fixed": True, "View > Wrap > Max": False, "View > Wrap > None": False} and
          len(radios) == 16,
          f"titles={titles} items={count} (3 titles + 49 PLAN rows) problems={problems[:5]} marks={marks} "
          f"radio items={len(radios)} "
          f"(e.g. {items['Edit > Undo'].text!r} {items['Edit > Undo'].key!r})")
    check("[C6] not implemented items are grey in the HMENU",
          all(not items[p].enabled for p, _, implemented in M3B_ITEMS if not implemented and p in items),
          "grey: " + ", ".join(p for p, _, implemented in M3B_ITEMS if not implemented)[:200])

    # Invoking items through the WM_COMMAND id of the real menu.
    app.menu_command(items["View > Line Numbers"].id)
    app.menu_command(items["View > Wrap > None"].id)
    time.sleep(0.3)
    after = run.items()
    state = run.state()
    run.shot("m3b_26_menu_command_view.png")
    check("[C3] WM_COMMAND of View items runs them; the marks follow on the next WM_INITMENUPOPUP",
          not after["View > Line Numbers"].checked and after["View > Wrap > None"].checked and
          not after["View > Wrap > Fixed"].checked and state["message"] == "Wrap: none",
          f"Line Numbers checked={after['View > Line Numbers'].checked} None={after['View > Wrap > None'].checked} "
          f"Fixed={after['View > Wrap > Fixed'].checked} message={state['message']!r} "
          "(m3b_26_menu_command_view.png: no line numbers, no wrap)")
    app.menu_command(items["View > Line Numbers"].id)
    app.menu_command(items["View > Wrap > Fixed"].id)
    # An edit ends the redo history: Redo then fails.
    keys("M-<")
    app.type("Q")
    keys("DEL")
    app.menu_command(items["Edit > Redo"].id)
    time.sleep(0.3)
    from_menu = run.state()["message"]
    keys("M-x")
    app.type("redo")
    keys("RET")
    time.sleep(0.3)
    from_mx = run.state()["message"]
    check("[C10] a menu command that fails shows its message like M-x",
          from_menu == from_mx == "No further redo information",
          f"menu: {from_menu!r}; M-x: {from_mx!r}")
    rejected = int(run.state()["rejected"])
    app.menu_command(items["Edit > Go to Line..."].id)
    time.sleep(0.3)
    state = run.state()
    check("[C10] WM_COMMAND of a grey item runs nothing and is logged",
          int(state["rejected"]) == rejected + 1 and "is disabled" in state["last_rejected"] and
          state["minibuffer"] == "0", f"rejected {rejected} -> {state['rejected']}: {state['last_rejected']!r}")

    # While the minibuffer is active every item is grey.
    keys("M-x")
    time.sleep(0.2)
    grey = run.items()
    keys("C-g")
    check("[C3] while the minibuffer is active every item of the HMENU is grey",
          all(not i.enabled for i in grey.values() if not i.separator and i.id != 0 and " > " in i.path),
          f"enabled while in the minibuffer: {[p for p, i in grey.items() if i.enabled and ' > ' in p][:5]}")

    # [C6] F10 records a pending menu-bar menu (the items of the real HMENU).
    keys("M-<")
    keys("<f10>")
    question = app.wait_question(3.0)
    pending = run.state()
    real = [i.line() for i in app.menu_bar()]
    check("[C6] F10 records a pending menu-bar menu with the items of the real HMENU",
          question is not None and pending["question"] == "menu_bar" and pending["menu_item"] == real and
          pending["menu_loops"] == "0", f"question={pending.get('question')} items={len(pending['menu_item'])} "
          f"same as GetMenu={pending['menu_item'] == real} menu_loops={pending['menu_loops']}")
    app.answer("Edit > Select All")
    time.sleep(0.3)
    state = run.state()
    check("[C6] answering the pending menu bar runs the item through WM_COMMAND",
          state["question"] == "none" and state["mark"] == "0" and state["mark_active"] == "1" and
          int(state["point"]) > 100, pmt(state))

    # Alt+F is forward-word; Alt alone enters no menu.
    keys("M-<")
    keys("M-f")
    time.sleep(0.2)
    alt_f = run.state()
    app._post_key_raw(d.VK_MENU)
    time.sleep(0.3)
    alt = run.state()
    check("[C6] posted Alt+F runs forward-word and enters no menu; Alt alone enters no menu",
          alt_f["point"] == "5" and alt_f["question"] == alt["question"] == "none" and
          alt["menu_loops"] == "0" and alt["last_keymenu"] in ("", "keymenu 0"),
          f"after M-f {pmt(alt_f)}; after Alt: question={alt['question']} menu_loops={alt['menu_loops']} "
          f"keymenu requests={alt['keymenu_count']} last={alt['last_keymenu']!r}")

    # menu_bar: false hides the bar; F10 says so; turning it on restores it
    # with the same top line.
    keys("M-< C-v")
    time.sleep(0.2)
    top0 = run.state()["top_line"]
    apply_settings(settings, {**wrap10, "menu_bar": False})
    off = run.state()
    hidden = d.user32.GetMenu(app.hwnd)
    keys("<f10>")
    time.sleep(0.3)
    f10 = run.state()
    run.shot("m3b_27_menu_bar_hidden.png")
    apply_settings(settings, wrap10)
    on = run.state()
    shown = d.user32.GetMenu(app.hwnd)
    run.shot("m3b_28_menu_bar_back.png")
    check("[C6] menu_bar: false removes the bar (GetMenu NULL) and F10 shows 'Menu bar is hidden'; back on, "
          "the bar returns with the top line unchanged",
          off["menu_bar"] == "0" and not hidden and f10["message"] == "Menu bar is hidden" and
          f10["question"] == "none" and on["menu_bar"] == "1" and bool(shown) and
          off["top_line"] == on["top_line"] == top0 != "0",
          f"off: GetMenu={hidden} top={off['top_line']}; F10 message={f10['message']!r}; on: GetMenu={shown} "
          f"top={on['top_line']} (was {top0}) (m3b_27/m3b_28)")

    # A failed HMENU creation is reported.
    apply_settings(settings, {**wrap10, "menu_bar": False})
    app.fault("menu")
    apply_settings(settings, wrap10)
    failed = run.state()
    apply_settings(settings, {**wrap10, "menu_bar": False})
    apply_settings(settings, wrap10)
    again = run.state()
    check("[C10] a failed menu bar creation is reported in the echo area; the next reload builds it",
          failed["menu_bar"] == "0" and "Cannot create the menu bar (error " in failed["message"] and
          again["menu_bar"] == "1", f"message={failed['message']!r} menu_bar {failed['menu_bar']} -> "
          f"{again['menu_bar']}")
    # Shift+F10 (posted) goes to DefWindowProc and the context menu.
    keys("S-<f10>")
    question = app.wait_question(2.0)
    if question is not None:
        app.answer("Cancel")
    record("INFO", "[C4] posted Shift+F10", f"question={question and question.get('question')} "
           "(DefWindowProc makes WM_CONTEXTMENU of Shift+F10 when the key is not bound)")


def check_clipboard_attribution(exe: pathlib.Path):
    """R3 self-check of d.clipboard_verdict with made-up sampler records (the
    real clipboard is never written here: the user may be at the desktop)."""
    target = os.path.normcase(str(exe))
    edge = os.path.normcase(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe")

    def change(owner_image, geditor_alive, pid=1234):
        return {"time": 1.0, "sequence": 10, "owner_pid": pid, "owner_image": owner_image,
                "geditor_alive": geditor_alive}

    cases = {
        "no change": ([], "PASS"),
        "the user copies in Edge while geditor runs": ([change(edge, True)], "INFO"),
        "geditor owns the clipboard": ([change(target, True)], "FAIL"),
        "geditor owns it after the run's windows closed": ([change(target, False)], "FAIL"),
        "unknown owner while a geditor of the run is open": ([change("", True, 0)], "FAIL"),
        "unknown owner while no geditor is open": ([change("", False, 0)], "INFO"),
        "Edge, then geditor": ([change(edge, True), change(target, True)], "FAIL"),
    }
    got = {name: d.clipboard_verdict(changes, target)[0] for name, (changes, _) in cases.items()}
    check("[R3] clipboard attribution: FAIL only for a geditor owner (or an unknown owner while geditor "
          "is open), INFO with the owner's name otherwise (made-up owners)",
          all(got[name] == want for name, (_, want) in cases.items()) and
          "msedge.exe" in d.clipboard_verdict([change(edge, True)], target)[1],
          f"verdicts={got}")


def run_m3b_visible_checks(exe: pathlib.Path, out: pathlib.Path):
    """--visible: the real menu loop (F10), the system menu (Alt+Space) and the
    real right-click popup, captured with PrintWindow. They need an unlocked
    desktop where geditor can take the foreground (SendInput)."""
    work = out / "files_m3b_visible"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = no_settings(out)
    doc = work / "v.txt"
    doc.write_bytes(b"hello world\nsecond line\n")
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    names = ["[C6] F10 and a mouse click open the real menu loop (look, mnemonics, Esc)",
             "[C6] Alt+Space opens the system menu",
             "[C4] the real right-click popup (look, position at the mouse)"]
    try:
        if app.use_input() != "sendinput":
            for name in names:
                record("SKIP", name, "geditor cannot take the foreground (locked desktop or --input post)")
            return

        def menu_window():
            found = d.wait_for(lambda: d.windows_of(app.proc.pid, "#32768"), 3.0)
            time.sleep(0.4)
            return found[0] if found else None

        app.keys("<f10>")
        time.sleep(0.3)
        app.keys("<down>")  # drop the File menu down
        menu = menu_window()
        if menu:
            d.capture(menu, shots / "m3b_v1_f10_file_menu.png")
        app.keys("ESC")
        app.keys("ESC")
        time.sleep(0.3)
        state = app.state()
        check(names[0], bool(menu) and int(state["menu_loops"]) >= 1,
              f"#32768 window={menu} menu_loops={state['menu_loops']} (m3b_v1_f10_file_menu.png: File menu "
              "with key texts and grey items)")
        app.keys("M-SPC")
        menu = menu_window()
        if menu:
            d.capture(menu, shots / "m3b_v2_system_menu.png")
        app.keys("ESC")
        time.sleep(0.3)
        check(names[1], bool(menu), f"#32768 window={menu} last_keymenu={app.state()['last_keymenu']!r} "
              "(m3b_v2_system_menu.png)")
        x, y = app.cell_point(0, 3)
        app.right_click(x, y)
        menu = menu_window()
        if menu:
            d.capture(menu, shots / "m3b_v3_context_menu.png")
        app.keys("ESC")
        time.sleep(0.3)
        state = app.state()
        check(names[2], bool(menu) and state["point"] == "0",
              f"#32768 window={menu} point={state['point']} (m3b_v3_context_menu.png: Undo .. Select All with "
              "key texts)")
    finally:
        app.kill()


# ---- M3c: grapheme clusters and F10 ----

# The cluster samples of the M3c checks: (name, text of the cluster, cells,
# screenshot name).
M3C_CLUSTERS = [
    ("U+263A U+FE0F", "☺️", 2, "m3c_01_vs16.png"),
    ("ZWJ family", "\U0001f468‍\U0001f469‍\U0001f467", 2, "m3c_02_zwj_family.png"),
    ("flag JP", "\U0001f1ef\U0001f1f5", 2, "m3c_03_flag.png"),
    ("keycap #", "#️⃣", 2, "m3c_04_keycap.png"),
    ("U+304B U+3099 (ka + voiced mark)", "が", 2, "m3c_05_ka_voiced.png"),
]
M3C_VISIBLE_F10 = ("[M3c C5] F10 (SendInput press and release) drops the File menu down and it stays; Right moves "
                   "to Edit, Left to File and then View (wrap-around); Esc closes it")


def cell_box(state: dict, row: int, first: int, cells: int) -> tuple[int, int, int, int]:
    """Client pixels (x0, y0, x1, y1) of cells [first, first + cells) of
    screen row `row` (the anti-aliased edges of a glyph stay inside)."""
    lh, cw, left = float(state["line_height"]), float(state["cell_width"]), float(state["text_left"])
    return (int(left + first * cw + 1), int(row * lh), int(left + (first + cells) * cw - 1), int((row + 1) * lh))


def run_m3c_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C4], [C5] (headless part) and [C8] of the M3c packet on the real app:
    clusters drawn in their cells (pixels of a vivid colour scheme), DEL and
    undo by cluster, the cursor after a cluster, a failed shaping and a
    cluster without glyphs, and F10 of the headless state."""
    work = out / "files_m3c"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    base = {"colors": VIVID, "line_numbers": False, "eol_marks": {"show": False}}
    settings.write_bytes(settings_json(base))
    doc = work / "m3c.txt"
    lines = ["a" + text + "b" for _, text, _, _ in M3C_CLUSTERS] + ["", ""]
    original = "\n".join(lines) + "\n"
    doc.write_bytes(original.encode("utf-8"))
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    names = []
    try:
        record("INFO", "M3c input mode", app.use_input())
        time.sleep(0.4)
        for row, (name, text, cells, shot_name) in enumerate(M3C_CLUSTERS):
            keys = app.keys
            keys("M-<")
            for _ in range(row):
                keys("C-n")
            keys("C-a C-f C-f")
            time.sleep(0.2)
            state = app.state()
            names.append(shot_name)
            shot = app.capture(shots / shot_name)
            lh, cw, left = float(state["line_height"]), float(state["cell_width"]), float(state["text_left"])
            a = count_where(shot, is_red, cell_box(state, row, 0, 1))
            first_cell = count_where(shot, is_red, cell_box(state, row, 1, 1))
            second_cell = count_where(shot, is_red, cell_box(state, row, 2, 1))
            b = count_where(shot, is_red, cell_box(state, row, 3, 1))
            after = count_where(shot, is_red, cell_box(state, row, 4, 6))
            box = cursor_box(shot)
            cursor_x = box[0] if box else -1
            expected_x = left + 3 * cw
            cursor_ok = box is not None and abs(cursor_x - expected_x) <= 2 and row * lh - 1 <= box[1] <= (row + 1) * lh
            line_start = sum(len(l.encode("utf-16-le")) // 2 + 1 for l in lines[:row])
            point_ok = state["point"] == str(line_start + 1 + len(text.encode("utf-16-le")) // 2)
            check(f"[M3c C4] {name} is drawn in {cells} cells next to ASCII, the cursor after it",
                  a > 0 and first_cell > 0 and second_cell > 0 and b > 0 and after == 0 and cursor_ok and point_ok,
                  f"red pixels: 'a' {a}, cluster cells {first_cell} / {second_cell}, 'b' {b}, after 'b' {after}; "
                  f"cursor x={cursor_x} (cell 3 at {expected_x:.1f}), point={state['point']} ({shot_name})")
            keys("DEL")
            time.sleep(0.2)
            deleted = app.state()
            keys("C-/")
            time.sleep(0.2)
            restored = app.state()
            row_text = lambda s: s["text"].split("\n")[row]  # noqa: E731
            check(f"[M3c C4] DEL once removes {name}; one undo restores it",
                  row_text(deleted) == "ab" and row_text(restored) == "a" + text + "b" and
                  restored["point"] == state["point"],
                  f"after DEL {row_text(deleted)!r}, after C-/ {row_text(restored)!r} point={restored['point']}")
        # [C8] A failed shaping, and a cluster that no font has, are drawn.
        row = len(M3C_CLUSTERS)
        app.keys("M-<")
        for _ in range(row):
            app.keys("C-n")
        app.fault("shape")
        app.type("ö \U0010fffd́")
        time.sleep(0.3)
        state = app.state()
        shot = app.capture(shots / "m3c_06_fallbacks.png")
        names.append("m3c_06_fallbacks.png")
        failed_shape = count_where(shot, is_red, cell_box(state, row, 0, 1))
        no_font = count_where(shot, is_red, cell_box(state, row, 2, 1))
        check("[M3c C8] a cluster whose shaping fails is drawn (the glyph of its first character)",
              failed_shape > 0, f"red pixels in the cell of o + U+0308 after 'GFLT' shape: {failed_shape} "
              "(m3c_06_fallbacks.png, cell 0)")
        check("[M3c C8] a cluster that no font has is drawn as the missing-glyph box",
              no_font > 0, f"red pixels in the cell of U+10FFFD + U+0301: {no_font} (m3c_06_fallbacks.png, cell 2)")
        app.keys("C-/")

        # [C10] ESC ESC ESC (keyboard-escape-quit) aborts M-x like C-g.
        app.keys("M-x")
        time.sleep(0.2)
        reading = app.state()
        app.keys("ESC ESC")
        time.sleep(0.2)
        waiting = app.state()
        app.keys("ESC")
        time.sleep(0.3)
        quit_state = app.state()
        shot = app.capture(shots / "m3c_07_escape_quit.png")
        names.append("m3c_07_escape_quit.png")
        check("[M3c C10] posted ESC ESC ESC in M-x aborts the minibuffer with 'Quit' (ESC ESC alone waits)",
              reading["minibuffer"] == "1" and waiting["minibuffer"] == "1" and quit_state["minibuffer"] == "0" and
              quit_state["message"] == "Quit" and
              count_where(shot, is_red, (0, layout(shot)[2], shot[0], shot[1])) > 0,
              f"minibuffer {reading['minibuffer']} -> after ESC ESC {waiting['minibuffer']} -> after the third ESC "
              f"{quit_state['minibuffer']}; message={quit_state['message']!r} (m3c_07_escape_quit.png: 'Quit' "
              "in the echo area)")

        # [R3] An input that starts with a combining mark: after C-a the caret
        # is at the start of the input, right after the prompt "M-x ".
        app.keys("M-x")
        app.type(chr(0x301) + "ab")
        time.sleep(0.2)
        end_state = app.state()
        app.keys("C-a")
        time.sleep(0.3)
        start_state = app.state()
        shot = app.capture(shots / "m3c_08_minibuffer_mark_caret.png")
        names.append("m3c_08_minibuffer_mark_caret.png")
        app.keys("C-g")
        cw = float(start_state["cell_width"])
        # Without line numbers (base settings) the text starts at the margin,
        # which the echo area starts at too.
        margin = float(start_state["text_left"])
        caret_start = float(start_state["caret_x"])
        caret_end = float(end_state["caret_x"])
        check("[M3c R3] minibuffer input starting with U+0301: after C-a the caret is at the input start",
              abs(caret_start - (margin + 4 * cw)) <= 2 and abs(caret_end - (margin + 6 * cw)) <= 2 and
              start_state["minibuffer"] == "1",
              f"caret_x after C-a {caret_start} (expected {margin + 4 * cw:.1f}: prompt 'M-x ' is 4 cells), at the "
              f"end {caret_end} (expected {margin + 6 * cw:.1f}) (m3c_08_minibuffer_mark_caret.png)")

        # [C5] F10 in the headless state: the pending menu bar with File open.
        app.keys("M-<")
        app.keys("<f10>")
        question = app.wait_question(3.0)
        pending = app.state()
        check("[M3c C5] headless F10 records the pending menu_bar menu with File as the open submenu",
              question is not None and pending["question"] == "menu_bar" and pending.get("menu_open") == "File" and
              pending["menu_loops"] == "0",
              f"question={pending.get('question')} menu_open={pending.get('menu_open')!r} "
              f"menu_loops={pending['menu_loops']}")
        app.answer("Cancel")
        time.sleep(0.2)
        # Alt+F, Alt alone and Alt+Space as in M3b.
        app.keys("M-<")
        app.keys("M-f")
        time.sleep(0.2)
        alt_f = app.state()
        app._post_key_raw(d.VK_MENU)
        time.sleep(0.3)
        alt = app.state()
        app.keys("M-SPC")
        time.sleep(0.3)
        alt_space = app.state()
        check("[M3c C5] Alt+F is forward-word, Alt alone enters no menu, Alt+Space goes to the system menu "
              "request (swallowed headless) as in M3b",
              alt_f["point"] == "1" and alt["question"] == alt_space["question"] == "none" and
              alt["menu_loops"] == alt_space["menu_loops"] == "0" and alt_space["last_keymenu"] == "keymenu 32",
              f"after M-f point={alt_f['point']}; after Alt: menu_loops={alt['menu_loops']} "
              f"last={alt['last_keymenu']!r}; after Alt+Space: last={alt_space['last_keymenu']!r} "
              f"question={alt_space['question']}")
        apply_settings(settings, {**base, "menu_bar": False})
        app.keys("<f10>")
        time.sleep(0.3)
        hidden = app.state()
        check("[M3c C5] menu_bar: false: F10 says 'Menu bar is hidden'",
              hidden["message"] == "Menu bar is hidden" and hidden["question"] == "none" and
              hidden["menu_bar"] == "0", f"message={hidden['message']!r} question={hidden['question']}")
    finally:
        app.kill()
    record("INFO", "[M3c C4] screenshots", ", ".join(names))
    needs_visible(M3C_VISIBLE_F10, "the real menu loop needs the screen and SendInput")


def run_m3c_visible_checks(exe: pathlib.Path, out: pathlib.Path):
    """--visible: F10 through SendInput on an unlocked desktop (decision G7)."""
    work = out / "files_m3c_visible"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = no_settings(out)
    doc = work / "v.txt"
    doc.write_bytes(b"hello world\nsecond line\n")
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    try:
        if app.use_input() != "sendinput":
            record("SKIP", M3C_VISIBLE_F10, "geditor cannot take the foreground (locked desktop or --input post)")
            return

        def popup(expected: str, shot: str) -> tuple[bool, str]:
            ok = d.wait_for(lambda: app.state().get("menu_popup") == expected, 3.0) is not None
            time.sleep(0.4)  # the key release and the menu animation
            menus = d.windows_of(app.proc.pid, "#32768")
            state = app.state()
            if menus:
                d.capture(menus[0], shots / shot)
            return (ok and bool(menus) and state.get("menu_popup") == expected and
                    state.get("menu_loop_active") == "1"), \
                f"{expected}: popup={state.get('menu_popup')!r} #32768={menus} ({shot})"

        app.keys("<f10>")  # SendInput: F10 down and up
        file_ok, file_ev = popup("File", "m3c_v1_f10_file.png")
        time.sleep(0.6)
        still = d.windows_of(app.proc.pid, "#32768")
        stays = bool(still) and app.state().get("menu_popup") == "File"
        app.keys("<right>")
        edit_ok, edit_ev = popup("Edit", "m3c_v2_right_edit.png")
        app.keys("<left>")
        back_ok, back_ev = popup("File", "m3c_v3_left_file.png")
        app.keys("<left>")
        view_ok, view_ev = popup("View", "m3c_v4_left_view.png")
        app.keys("ESC")
        time.sleep(0.4)
        after_esc = app.state()
        closed = not d.windows_of(app.proc.pid, "#32768")
        app.keys("ESC")
        time.sleep(0.4)
        after_esc2 = app.state()
        check(M3C_VISIBLE_F10,
              file_ok and stays and edit_ok and back_ok and view_ok and closed and
              after_esc2.get("menu_loop_active") == "0",
              f"{file_ev}; still open 0.6 s after the release={stays}; {edit_ev}; {back_ev}; {view_ev}; "
              f"after Esc: #32768 closed={closed} popup={after_esc.get('menu_popup')!r}; after a second Esc: "
              f"menu loop active={after_esc2.get('menu_loop_active')} popups={after_esc2.get('menu_popups')!r}")
    finally:
        app.kill()


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
    parser.add_argument("--only-m3b", action="store_true",
                        help="run only the M3b menu, context menu and mouse checks")
    parser.add_argument("--only-m3c", action="store_true",
                        help="run only the M3c grapheme cluster and F10 checks")
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
    only = (args.only_retry1 or args.only_m2 or args.only_m3a or args.only_m3b or args.only_m3c or
            args.only_headless or args.only_visible)
    exe, out = args.exe.resolve(), args.out.resolve()
    sampler = None
    if d.HEADLESS:
        sampler = d.ForegroundSampler(exe, 0.02)
        sampler.start()
        # From now on the sampler sees every change of the clipboard.
        sequence0 = d.user32.GetClipboardSequenceNumber()
        check_clipboard_attribution(exe)
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
        if not only or args.only_m3b:
            if d.HEADLESS:
                run_m3b_checks(exe, out)
            else:
                run_m3b_visible_checks(exe, out)
        if not only or args.only_m3c:
            if d.HEADLESS:
                run_m3c_checks(exe, out)
            else:
                run_m3c_visible_checks(exe, out)
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
        # R3: the user may copy something meanwhile; a change counts against
        # geditor only when a geditor process owns the clipboard (or the
        # owner is unknown while a geditor of the run was open).
        verdict, why = d.clipboard_verdict(sampler.clipboard_changes, os.path.normcase(str(exe)))
        if sequence0 != sequence1 and not sampler.clipboard_changes:
            verdict, why = "FAIL", "the sequence number changed but the sampler saw no change"
        check("[C4] the real clipboard was not written by geditor (GetClipboardSequenceNumber, "
              "GetClipboardOwner)", verdict != "FAIL",
              f"sequence number {sequence0} -> {sequence1}: {why} (its text is not read: that would open it)")
        if verdict == "INFO":
            record("INFO", "[C4] the real clipboard changed during the run", why)
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
        check("[M3b C8] no menu or popup menu (#32768) of a geditor process appeared",
              not sampler.menus and sampler.menu_scans > 0,
              f"{sampler.menu_scans} scans of the visible #32768 windows (every 20 ms, in the sampler process), "
              f"geditor menus={sampler.menus[:5]}, visible #32768 of any process={sampler.menus_any_process}")
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
