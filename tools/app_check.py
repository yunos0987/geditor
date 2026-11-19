"""Real-app checks of geditor M1, M2, M3a - M3d, M4a, M4b, M5, M6 and the deferred fixes (run by hand; Windows only).

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
menu down (Left / Right / Esc, read from the state's menu_popup). The M4a
checks (--only-m4a) open a file with mixed line ends and look at the colour
of each end-of-line mark, read the File menu's Line Endings and Read Only
marks, save (the bytes of each line end, "<file>~", the external-change
question, a deleted file), make each step of the save fail ('GFLT'
"save-..." / "file-info"), open a file with the read-only attribute, round
trip utf-8 / utf-8-bom with four kinds of line ends, and save through a hard
link and (when it can be created) a symbolic link. The M4b checks
(--only-m4b) open files of each BOM kind, UTF-16LE without BOM and
Shift_JIS by detection, run find-file-with-encoding through the minibuffer
(with completion) and through the menu (F10 answered with 'GANS', into a new
window that gets --encoding), start with --encoding (a wrong one shows "!"),
revert with the question answered No and Yes, stop a Shift_JIS save at an
emoji and read the Encoding for Saving radio of the real HMENU. The M5
checks (--only-m5, headless only) look at the search_match / search_current
pixels of isearch and query-replace (and that none are left after them),
read the grey items of the real HMENU during both modes, run Edit > Find /
Replace / Go to Line through WM_COMMAND, the C-S-f / C-S-g / C-S-h / C-S-r
keys and C-S-s / C-S-z inside isearch, and time a search for a word near
the end of a 100MB file (made once under build/m5_search/). The M6 checks
(--only-m6, headless only) run kills in a row (the private clipboard),
shift selection, a keyboard macro (recorded, run, repeated with e, undone,
with a click that is not recorded), --encoding without a file, a revert
that keeps read-only-mode, the save question of a "!" buffer answered No
and Yes through 'GANS', and the scroll bar of a 10MB single line
(GetScrollInfo, <end>, a posted SB_THUMBTRACK; the file is made once under
build/m6_scroll/). The checks of the deferred fixes after M6
(--only-deferred, headless only) make a font creation fail at the reload
that names a missing family and look for "Font not found" once the font is
in use (E2), post z, C-g and q during a long macro run (E4), and check the
order of input held during a run, the prefix key echo after a run (no
killed timer replayed), input after a right click during a run and a drag
ended by C-g (retries R1, Q1-Q3), a C-g behind a non-client mouse move,
a non-client button before a key and F10 without a menu bar (P1, P2).

Headless (the default; the line "mode headless" says so): every geditor
gets --headless. Its window is outside every monitor and never activated,
keys are posted as WM_KEYDOWN / WM_CHAR with the modifiers of each key in
its lParam bits (M4a fix F2; the keyboard state is never set), the
clipboard checks use geditor's
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
    """Bounding box of cursor-coloured pixels in the text area or echo line.
    Only the columns that hold at least half as many of them as the fullest
    column count: the cursor is a bar, and colour emoji (M3d) have small
    white parts (eyes, teeth) that would otherwise count."""
    w, h, px = shot
    text_bottom, _, mode_bottom = layout(shot)
    y0, y1 = (0, text_bottom) if region == "text" else (mode_bottom, h)
    columns = {}
    for y in range(y0, y1):
        row = px[y * w * 4:(y + 1) * w * 4]
        for x in range(w):
            i = x * 4
            if row[i] == 0xFF and row[i + 1] == 0xFF and row[i + 2] == 0xFF:
                columns.setdefault(x, []).append(y)
    if not columns:
        return None
    fullest = max(len(ys) for ys in columns.values())
    bar = {x: ys for x, ys in columns.items() if 2 * len(ys) >= fullest}
    return min(bar), min(min(ys) for ys in bar.values()), max(bar), max(max(ys) for ys in bar.values())


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
            app.keys("C-k")  # the line end, added to the kill before it (M6 kill append)
            time.sleep(0.2)
            clip_nl = clipboard_read(app)
            check("killed newline is CRLF on the clipboard", clip_nl == "world\r\n",
                  f"clipboard={clip_nl!r} (M6: the second C-k in a row adds to the first)")
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
        # M4a (decision F6): the read-only attribute turns read-only-mode on;
        # C-x C-q turns it off so that the buffer can be edited.
        app.keys("C-x C-q")
        time.sleep(0.2)
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
            how = (f"synthetic WM_SIZE SIZE_MINIMIZED, then SIZE_RESTORED {client[0]}x{client[1]}; the IsIconic "
                   "fallback of visible_lines / visible_columns is not exercised (only --visible minimises)")
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


def is_lit(r, g, b):
    """Not the black background (any colour of a glyph)."""
    return max(r, g, b) > 60


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


def check_headless_keys_ignore_held_ctrl(exe: pathlib.Path, work: pathlib.Path, settings: pathlib.Path):
    """M4a fix F2 ([C7]): while this thread is attached to geditor's input
    with Ctrl held in the shared keyboard state (what the user's real Ctrl
    did to the old driver), a posted plain y inserts y and C-y (Ctrl in the
    lParam bits of the key) yanks. Only this check holds a key
    (d.held_keyboard_state); the driver never touches the keyboard state."""
    app = d.App(exe, [str(work / "f2_keys.txt")], cwd=work, settings=settings)
    try:
        app.use_input()
        stored = app.set_clipboard("Z")
        with d.held_keyboard_state(app.hwnd, [d.VK_CONTROL, 0xA2]):  # VK_LCONTROL
            held = d.user32.GetKeyState(d.VK_CONTROL) < 0
            app.keys("y")
            time.sleep(0.3)
            plain = app.state()
            app.keys("C-y")
            time.sleep(0.3)
            yanked = app.state()
        check("[M4a fix F2] with Ctrl held in the shared keyboard state, a posted plain y inserts y",
              held and plain.get("text") == "y" and plain.get("point") == "1",
              f"Ctrl held={held}; after y text={plain.get('text')!r} point={plain.get('point')} "
              f"message={plain.get('message')!r}")
        check("[M4a fix F2] with Ctrl held in the shared keyboard state, C-y (Ctrl in the key's lParam bits) yanks",
              stored and yanked.get("text") == "yZ" and yanked.get("point") == "2",
              f"clipboard set={stored}; after C-y text={yanked.get('text')!r} point={yanked.get('point')} "
              f"message={yanked.get('message')!r}")
        after = d.user32.GetKeyState(d.VK_CONTROL) < 0
        record("INFO", "[M4a fix F2] keyboard state after the check", f"Ctrl held={after} (expected False)")
    finally:
        app.kill()


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
                           ("MoveWindow", lambda: d.user32.MoveWindow(app.hwnd, 0, 0, 10, 10, True)),
                           ("AttachThreadInput", lambda: d.user32.AttachThreadInput(0, 0, False)),
                           ("SetKeyboardState", lambda: d.user32.SetKeyboardState(None))]:
            try:
                call()
            except d.DriverError:
                refused.append(name)
        check("[C3] the driver refuses SendInput, keybd_event, SetForegroundWindow, BringWindowToTop, "
              "SetActiveWindow, the clipboard, window moves, AttachThreadInput and SetKeyboardState "
              "(M4a fix F2) in headless mode",
              sorted(guarded) == sorted(d._GUARDED) and len(refused) == 10 and
              {"AttachThreadInput", "SetKeyboardState"} <= set(d._GUARDED),
              f"guarded={guarded} refused={refused}")

        # [M4a fix F2] Keys carry their own modifiers (in a window of its own).
        check_headless_keys_ignore_held_ctrl(exe, work, settings)

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
# keys, no settings: the Windows-side key, two spaces, the Emacs key; one key
# when only one exists) and whether the command is implemented now (else grey;
# M3b, set-eol since M4a, the encoding commands since M4b). tests/test_menu.cpp
# holds the same table for the core model.
M3B_ITEMS = [
    ("File > Open...", "C-x C-f", True), ("File > Open with Encoding...", "C-x RET c", True),
    ("File > Reopen with Encoding...", "C-x RET r", True), ("File > Save", "C-S-s  C-x C-s", True),
    ("File > Save As...", "C-x C-w", True),
    *[(f"File > Encoding for Saving > {name}", "", True) for name in (
        "utf-8", "utf-8-bom", "utf-16le", "utf-16le-bom", "utf-16be", "utf-16be-bom", "shift_jis", "euc-jp",
        "iso-2022-jp")],
    # set-eol: implemented in M4a.
    ("File > Line Endings > LF", "", True), ("File > Line Endings > CRLF", "", True),
    ("File > Line Endings > CR", "", True), ("File > Read Only", "C-x C-q", True),
    ("File > Settings", "", True), ("File > Exit", "C-S-w  C-x C-c", True),
    ("Edit > Undo", "C-S-z  C-/", True), ("Edit > Redo", "C-S-y  C-?", True),
    # The Emacs keys of Cut / Copy / Paste / Select All are those of the
    # same-action commands kill-region / kill-ring-save / yank / mark-whole-buffer.
    ("Edit > Cut", "C-S-x  C-w", True), ("Edit > Copy", "C-S-c  M-w", True), ("Edit > Paste", "C-S-v  C-y", True),
    ("Edit > Select All", "C-S-a  C-x h", True),
    # isearch-forward, query-replace and goto-line: implemented in M5.
    ("Edit > Find...", "C-S-f  C-s", True), ("Edit > Replace...", "C-S-h  M-%", True),
    ("Edit > Go to Line...", "C-S-g  M-g g", True),
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
        run_m3b_windows_keys(app, run)
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
    # The key texts of the right-click HMENU (read back from the built menu)
    # are those of the Edit menu items (two keys: Windows side, Emacs).
    expected = dict((p.split(" > ", 1)[1], k) for p, k, _ in M3B_ITEMS if p.startswith("Edit > "))
    got = dict((i.split("\t")[0], i.split("\t")[1]) for i in items if len(i.split("\t")) == 3 and
               not i.startswith("-\t"))
    check("[keys C5/C8] the right-click HMENU shows the two-key texts (Windows side, two spaces, Emacs; "
          "the same-action Emacs keys of Cut / Copy / Paste / Select All)",
          got == {name: expected[name] for name in M3B_CONTEXT if name != "-"} and
          got.get("Undo") == "C-S-z  C-/" and got.get("Cut") == "C-S-x  C-w" and got.get("Copy") == "C-S-c  M-w" and
          got.get("Paste") == "C-S-v  C-y" and got.get("Select All") == "C-S-a  C-x h",
          f"key texts={got}")
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


def run_m3b_windows_keys(app, run: M3bRun):
    """The Windows compatible keys are Ctrl+Shift since 2026-11-15: C-S-a
    selects everything (not C-a), C-S-z undoes, and the old Ctrl+Alt key
    C-M-a is undefined."""
    keys = app.keys
    keys("C-g M-< C-n C-f C-f")
    time.sleep(0.3)
    before = run.state()
    keys("C-S-a")
    time.sleep(0.3)
    selected = run.state()
    check("[keys C5] posted C-S-a runs select-all (not C-a): mark at 0, point at the end",
          before["point"] != "0" and selected["mark_active"] == "1" and selected["mark"] == "0" and
          selected["point"] == str(len(selected["text"])) and selected["text"] == before["text"],
          f"before {pmt(before)}; after {pmt(selected)} length={len(selected['text'])} "
          f"message={selected['message']!r}")
    keys("C-g M-<")
    app.type("Q")
    time.sleep(0.3)
    typed = run.state()
    keys("C-S-z")
    time.sleep(0.3)
    undone = run.state()
    check("[keys C5] posted C-S-z undoes the typed character",
          typed["text"] == "Q" + before["text"] and undone["text"] == before["text"],
          f"typed: {typed['text'][:12]!r}; after C-S-z: {undone['text'][:12]!r} message={undone['message']!r}")
    keys("M-< C-n C-f")
    time.sleep(0.3)
    b = run.state()
    keys("C-M-a")
    time.sleep(0.3)
    a = run.state()
    check("[keys C5] posted C-M-a shows 'C-M-a is undefined' and changes no text, point, mark or modified flag",
          a["message"] == "C-M-a is undefined" and a["text"] == b["text"] and a["point"] == b["point"] and
          a["mark_active"] == b["mark_active"] and a["modified"] == b["modified"],
          f"message={a['message']!r} before {pmt(b)} modified={b['modified']}; after {pmt(a)} "
          f"modified={a['modified']}")


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
          "grey: " + (", ".join(p for p, _, implemented in M3B_ITEMS if not implemented)[:200] or
                      "none (every menu command is implemented since M5)"))

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
    # Since M5 every menu command is implemented: an item is grey while the
    # minibuffer is active (M-x here).
    keys("M-x")
    time.sleep(0.2)
    rejected = int(run.state()["rejected"])
    app.menu_command(items["Edit > Go to Line..."].id)
    time.sleep(0.3)
    state = run.state()
    keys("C-g")
    check("[C10] WM_COMMAND of a grey item runs nothing and is logged",
          int(state["rejected"]) == rejected + 1 and "is disabled" in state["last_rejected"] and
          state["echo"] == "M-x ", f"rejected {rejected} -> {state['rejected']}: {state['last_rejected']!r}; "
          f"echo={state['echo']!r}")

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
                "geditor_alive": geditor_alive, "at": 100.0}

    # Times (time.monotonic()) of input sent to a geditor (d.ACTIONS); the
    # changes above come at 100.0 (deferred fix T3).
    just_before = [50.0, 99.7]   # an input 0.3 s before the change
    long_before = [50.0, 98.5]   # the last input 1.5 s before it
    after_only = [100.4]         # input only after the change
    cases = {
        "no change": ([], None, "PASS"),
        "the user copies in Edge while geditor runs": ([change(edge, True)], just_before, "INFO"),
        "geditor owns the clipboard": ([change(target, True)], long_before, "FAIL"),
        "geditor owns it after the run's windows closed": ([change(target, False)], None, "FAIL"),
        "unknown owner while a geditor of the run is open, 0.3 s after input sent to it":
            ([change("", True, 0)], just_before, "FAIL"),
        "unknown owner while a geditor of the run is open, no input sent to it within 1 s before":
            ([change("", True, 0)], long_before, "INFO"),
        "unknown owner while a geditor of the run is open, input only after the change":
            ([change("", True, 0)], after_only, "INFO"),
        "unknown owner while a geditor is open, no record of input (as before T3)":
            ([change("", True, 0)], None, "FAIL"),
        "unknown owner while no geditor is open": ([change("", False, 0)], just_before, "INFO"),
        "Edge, then geditor": ([change(edge, True), change(target, True)], long_before, "FAIL"),
    }
    got = {name: d.clipboard_verdict(changes, target, actions)[0] for name, (changes, actions, _) in cases.items()}
    check("[R3] clipboard attribution: FAIL only for a geditor owner, or an unknown owner while geditor is open "
          "within 1 s after input sent to it; INFO with the owner's name otherwise (made-up owners and times)",
          all(got[name] == want for name, (_, _, want) in cases.items()) and
          "msedge.exe" in d.clipboard_verdict([change(edge, True)], target, just_before)[1],
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
            # M3d: emoji are drawn in their own colours, so the cluster cells
            # count every lit pixel (black background), not only red ones.
            first_cell = count_where(shot, is_lit, cell_box(state, row, 1, 1))
            second_cell = count_where(shot, is_lit, cell_box(state, row, 2, 1))
            b = count_where(shot, is_red, cell_box(state, row, 3, 1))
            after = count_where(shot, is_lit, cell_box(state, row, 4, 6))
            box = cursor_box(shot)
            cursor_x = box[0] if box else -1
            expected_x = left + 3 * cw
            cursor_ok = box is not None and abs(cursor_x - expected_x) <= 2 and row * lh - 1 <= box[1] <= (row + 1) * lh
            line_start = sum(len(l.encode("utf-16-le")) // 2 + 1 for l in lines[:row])
            point_ok = state["point"] == str(line_start + 1 + len(text.encode("utf-16-le")) // 2)
            check(f"[M3c C4] {name} is drawn in {cells} cells next to ASCII, the cursor after it",
                  a > 0 and first_cell > 0 and second_cell > 0 and b > 0 and after == 0 and cursor_ok and point_ok,
                  f"red pixels: 'a' {a}, 'b' {b}; lit pixels: cluster cells {first_cell} / {second_cell}, "
                  f"after 'b' {after}; "
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


# ---- M3d: colour emoji and the F10 wrap-around ----

# A grey scheme: every colour that is not an emoji's lies in the grey box
# [0x20, 0x60] of each channel (ClearType mixes foreground and background
# channel by channel, so its fringes stay inside it too). A pixel outside the
# box with a clear hue is a colour of a glyph (an emoji).
M3D_GREY = {
    "background": "#202020", "foreground": "#505050", "cursor": "#606060",
    "selection": "#383838", "line_number": "#505050", "line_number_current": "#606060",
    "whitespace": "#404040", "eol": "#404040",
    "mode_line_background": "#303030", "mode_line_foreground": "#606060",
}
M3D_SELECTION = 0x383838
M3D_BACKGROUND = 0x202020

# (name, text, screenshot) of the [C2] samples; each is drawn in 2 cells.
M3D_EMOJI = [
    ("U+2639 U+FE0F", "☹️"),
    ("U+263A U+FE0F", "☺️"),
    ("thumbs up + U+1F3FD", "\U0001f44d\U0001f3fd"),
    ("ZWJ family", "\U0001f468‍\U0001f469‍\U0001f467"),
    ("U+1F600", "\U0001f600"),
    ("U+2705", "✅"),
]
M3D_VISIBLE_F10 = ("[M3d C3] F10 (SendInput) drops File down; Left goes to View, Right from View goes to File "
                   "(wrap-around with one key each); Esc closes the drop-down and a second Esc the menu bar")


def is_emoji_colour(r, g, b):
    """A colour outside the grey box of M3D_GREY with a hue (not grey)."""
    outside = min(r, g, b) < 0x18 or max(r, g, b) > 0x68
    return outside and max(r, g, b) - min(r, g, b) > 40


def is_drawn(r, g, b):
    """Not the background of M3D_GREY (any glyph pixel)."""
    return max(abs(r - 0x20), abs(g - 0x20), abs(b - 0x20)) > 8


def m3d_document(work: pathlib.Path) -> tuple[pathlib.Path, list[str]]:
    """Each sample on its own row "a<emoji>b 漢x", with an ASCII / CJK row
    between them: cells 0 'a', 1-2 the emoji, 3 'b', 5-6 '漢', 7 'x'."""
    lines = []
    for _, text in M3D_EMOJI:
        lines.append("a" + text + "b 漢x")
        lines.append("plain ASCII 漢字")
    doc = work / "m3d.txt"
    doc.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    return doc, lines


def m3d_colour_rows(shot, state: dict, region: bool) -> list[tuple[bool, str]]:
    """[C2] on one screenshot, (verdict, evidence) per sample row: coloured
    pixels in both emoji cells and nowhere else on the row or on the ASCII /
    CJK row below; the ASCII and CJK glyphs are drawn (not background) in one
    colour. With `region`, the selection colour shows inside the emoji cells
    too."""
    rows = []
    w = shot[0]
    for i, (name, _) in enumerate(M3D_EMOJI):
        row = 2 * i
        first = count_where(shot, is_emoji_colour, cell_box(state, row, 1, 1))
        second = count_where(shot, is_emoji_colour, cell_box(state, row, 2, 1))
        # Left of the emoji cells (cell 0 and the margin), right of them (cell
        # 3 to the window edge), and the whole ASCII / CJK row below.
        before_cells = cell_box(state, row, 0, 1)
        after_cells = cell_box(state, row, 3, 1)
        outside = (count_where(shot, is_emoji_colour, (0, before_cells[1], before_cells[2], before_cells[3])) +
                   count_where(shot, is_emoji_colour, (after_cells[0], after_cells[1], w, after_cells[3])))
        below = cell_box(state, row + 1, 0, 1)
        between = count_where(shot, is_emoji_colour, (0, below[1], w, below[3]))
        a_drawn = count_where(shot, is_drawn, cell_box(state, row, 0, 1))
        kanji_drawn = count_where(shot, is_drawn, cell_box(state, row, 5, 2))
        selected = count(shot, M3D_SELECTION, "text") if region else 0
        in_cells = 0
        if region:
            box = cell_box(state, row, 1, 2)
            in_cells = d.count_color(shot[0], shot[1], shot[2], M3D_SELECTION, box)
        row_ok = (first > 0 and second > 0 and outside == 0 and between == 0 and a_drawn > 0 and
                  kanji_drawn > 0 and (not region or in_cells > 0))
        rows.append((row_ok, f"{name}: coloured px in its cells {first}/{second}, elsewhere on its row {outside}, "
                             f"on the ASCII/CJK row below {between}; drawn px 'a' {a_drawn}, '漢' {kanji_drawn}" +
                             (f"; selection px in its cells {in_cells} (whole text area {selected})"
                              if region else "")))
    return rows


def run_m3d_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C2], [C3] (headless part), [C4] (redraw benchmark) and [C5] of the
    M3d packet on the real app, headless."""
    work = out / "files_m3d"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    base = {"colors": M3D_GREY, "line_numbers": False, "eol_marks": {"show": False}}
    settings.write_bytes(settings_json(base))
    doc, lines = m3d_document(work)
    names = []
    app = d.App(exe, [str(doc)], cwd=work, settings=settings, height=600)
    try:
        record("INFO", "M3d input mode", app.use_input())
        time.sleep(0.5)
        app.keys("C-f")  # the cursor before the first emoji: its bar is drawn over it (D2)
        time.sleep(0.2)
        state = app.state()
        shot = app.capture(shots / "m3d_01_colour_emoji.png")
        names.append("m3d_01_colour_emoji.png")
        bar = d.count_color(shot[0], shot[1], shot[2], int(M3D_GREY["cursor"][1:], 16), cell_box(state, 0, 1, 1))
        check("[M3d C2] the bar cursor is drawn over an emoji (cursor colour in its first cell)",
              bar > 0 and state["point"] == "1", f"cursor px in the cell of U+2639 U+FE0F {bar}, point={state['point']} "
              "(m3d_01_colour_emoji.png)")
        record("INFO", "[M3d] colour formats of the render target (software)",
               f"color_support={state.get('color_support')!r} (colr: COLR layers through DrawGlyphRun; svg / bitmap: "
               "ID2D1DeviceContext4; paint: the COLR paint tree through ID2D1DeviceContext7)")
        for (name, _), (row_ok, ev) in zip(M3D_EMOJI, m3d_colour_rows(shot, state, region=False)):
            check(f"[M3d C2] {name} is drawn in colour inside its 2 cells; ASCII and CJK next to it in one colour",
                  row_ok, ev + " (m3d_01_colour_emoji.png)")

        # The same emoji in an active region: selection under them, colours kept.
        app.keys("M-< C-SPC M->")
        time.sleep(0.3)
        region_state = app.state()
        shot = app.capture(shots / "m3d_02_colour_emoji_region.png")
        names.append("m3d_02_colour_emoji_region.png")
        rows = m3d_colour_rows(shot, region_state, region=True)
        check("[M3d C2] in an active region the emoji keep their colours over the selection background",
              all(ok for ok, _ in rows) and region_state["mark_active"] == "1",
              f"mark_active={region_state['mark_active']}; " + "; ".join(ev for _, ev in rows) +
              " (m3d_02_colour_emoji_region.png)")
        app.keys("C-g M-<")
        time.sleep(0.2)
        app.keys("C-f C-b")  # clears "Mark set" of M-<
        time.sleep(0.2)

        # [C5] (D3): colour fails; one colour from then on, said once.
        app.fault("color")
        failed = d.wait_for(lambda: M3D_COLOUR_MESSAGE in app.state()["message"] or None, 3.0)
        state = app.state()
        shot = app.capture(shots / "m3d_03_colour_failed.png")
        names.append("m3d_03_colour_failed.png")
        coloured = sum(count_where(shot, is_emoji_colour, cell_box(state, 2 * i, 1, 2))
                       for i in range(len(M3D_EMOJI)))
        drawn = [count_where(shot, is_drawn, cell_box(state, 2 * i, 1, 2)) for i in range(len(M3D_EMOJI))]
        echo_top = int(float(state["mode_line_top"]) + float(state["line_height"])) + 1
        echo = count_where(shot, is_drawn, (0, echo_top, shot[0], shot[1]))
        app.keys("C-f")
        time.sleep(0.2)
        cleared = app.state()["message"]
        app.fault("color")
        app.keys("C-b")
        time.sleep(0.4)
        again = app.state()
        check("[M3d C5] a failed colour glyph is drawn in one colour and the echo area says so once",
              failed is not None and coloured == 0 and all(n > 0 for n in drawn) and echo > 0 and
              M3D_COLOUR_MESSAGE not in cleared and M3D_COLOUR_MESSAGE not in again["message"] and app.alive(),
              f"message={state['message']!r}; coloured px in the emoji cells {coloured}, drawn px per emoji {drawn}, "
              f"echo px {echo}; after C-f message={cleared!r}; after a second 'GFLT' color and C-b "
              f"message={again['message']!r}, color_support={again.get('color_support')!r} "
              "(m3d_03_colour_failed.png: the emoji in grey)")

        # [C3] headless: the second key of a wrap-around that no menu loop
        # took is dropped (it would move the cursor otherwise); a real Left
        # still moves it.
        app.keys("M-< C-e")
        time.sleep(0.2)
        before = app.state()["point"]
        d.user32.PostMessageW(app.hwnd, d.WM_KEYDOWN, 0x25, 0x20000002)  # VK_LEFT, the wrap marker
        time.sleep(0.3)
        after_marker = app.state()["point"]
        app.keys("<left>")
        time.sleep(0.2)
        after_left = app.state()["point"]
        check("[M3d C3] headless: a queued wrap-around key outside a menu loop is dropped; Left still moves",
              before == after_marker and int(after_left) < int(before),
              f"point at the end of line 1 {before}; after the posted wrap key {after_marker}; after Left "
              f"{after_left}")
        app.keys("<f10>")
        question = app.wait_question(3.0)
        pending = app.state()
        check("[M3d C3] headless F10 still records the pending menu_bar menu with File open (as M3c)",
              question is not None and pending["question"] == "menu_bar" and pending.get("menu_open") == "File" and
              pending["menu_loops"] == "0",
              f"question={pending.get('question')} menu_open={pending.get('menu_open')!r} "
              f"menu_loops={pending['menu_loops']}")
        app.answer("Cancel")
    finally:
        app.kill()
    record("INFO", "[M3d] screenshots", ", ".join(names))
    run_m3d_text_style_check(exe, out)
    run_m3d_benchmark(exe, out)
    needs_visible(M3D_VISIBLE_F10, "the real menu loop needs the screen and SendInput")


M3D_COLOUR_MESSAGE = "Colour emoji are not available; drawn in one colour"

# Clusters not shown as emoji (text style): drawn in one colour even though
# Segoe UI Emoji has them in colour (user, 2026-11-14).
M3D_TEXT_STYLE = [
    ("U+2639", "☹"),
    ("U+2764", "❤"),
    ("U+2714", "✔"),
    ("U+2600", "☀"),
    ("U+2639 U+FE0E", "☹︎"),
    ("U+1F600 U+FE0E", "\U0001f600︎"),
]


def run_m3d_text_style_check(exe: pathlib.Path, out: pathlib.Path):
    """Text-style symbols and U+FE0E clusters: drawn (not blank) and with no
    coloured pixel anywhere on their rows."""
    work = out / "files_m3d_text"
    work.mkdir(parents=True, exist_ok=True)
    settings = work / "settings.json"
    settings.write_bytes(settings_json({"colors": M3D_GREY, "line_numbers": False, "eol_marks": {"show": False}}))
    doc = work / "m3d_text.txt"
    doc.write_bytes(("\n".join("a" + text + "b" for _, text in M3D_TEXT_STYLE) + "\n").encode("utf-8"))
    app = d.App(exe, [str(doc)], cwd=work, settings=settings, height=400)
    try:
        app.use_input()
        time.sleep(0.5)
        state = app.state()
        shot = app.capture(out / "shots" / "m3d_05_text_style.png")
        rows = []
        ok = True
        for i, (name, _) in enumerate(M3D_TEXT_STYLE):
            box = cell_box(state, i, 0, 1)
            coloured = count_where(shot, is_emoji_colour, (0, box[1], shot[0], box[3]))
            drawn = count_where(shot, is_drawn, cell_box(state, i, 1, 1))
            ok = ok and coloured == 0 and drawn > 0
            rows.append(f"{name}: coloured px on its row {coloured}, drawn px in its first cell {drawn}")
        check("[M3d D1] text-style symbols and U+FE0E clusters are drawn in one colour", ok,
              "; ".join(rows) + " (m3d_05_text_style.png)")
    finally:
        app.kill()


def run_m3d_benchmark(exe: pathlib.Path, out: pathlib.Path):
    """[C4] (D4): frame times of a screen full of emoji (30 rows x 40) and of
    the same screen in ASCII, drawn at once through 'GRDW', with the total
    CPU load meanwhile. In a debug build the bytes that the C runtime
    allocated per frame are compared too."""
    work = out / "files_m3d_bench"
    work.mkdir(parents=True, exist_ok=True)
    settings = work / "settings.json"
    settings.write_bytes(settings_json({"line_numbers": False, "wrap": "none"}))
    cycle = ["\U0001f600", "✅", "\U0001f44d\U0001f3fd", "☺️", "\U0001f468‍\U0001f469‍\U0001f467",
             "\U0001f389", "\U0001f680", "❤️"]
    emoji_doc = work / "emoji.txt"
    emoji_doc.write_bytes("".join("".join(cycle[(r + c) % len(cycle)] for c in range(40)) + "\n"
                                  for r in range(30)).encode("utf-8"))
    ascii_doc = work / "ascii.txt"
    ascii_doc.write_bytes("".join("".join("ab"[(r + c) % 2] * 2 for c in range(40)) + "\n"
                                  for r in range(30)).encode("utf-8"))
    results = {}

    def measure(app, state) -> dict:
        app.redraw(5)  # warm-up: glyphs and colour layers looked up
        before = d.system_times()
        values = app.redraw(60)
        after = d.system_times()
        values["cpu"] = f"{d.cpu_load(before, after):.0f}"
        values["rows"] = state["text_rows"]
        return values

    for name, doc in (("emoji", emoji_doc), ("ascii", ascii_doc)):
        app = d.App(exe, [str(doc)], cwd=work, settings=settings, width=900, height=700)
        try:
            time.sleep(0.5)
            state = app.state()
            results[name] = measure(app, state)
            if name == "emoji":
                # The same screen in one colour (decision D3): the difference
                # is what the colour path costs, in time and in allocations.
                app.fault("color")
                time.sleep(0.3)
                results["emoji in one colour"] = measure(app, state)
                results["emoji in one colour"]["support"] = app.state().get("color_support", "")
        finally:
            app.kill()
    for name, v in results.items():
        record("INFO", f"[M3d C4] redraw of a screen of {name} ({v['rows']} rows x 40 clusters)",
               f"60 frames: median {v['redraw_median_ms']} ms, p95 {v['redraw_p95_ms']} ms, max "
               f"{v['redraw_max_ms']} ms (first of the run {v['redraw_first_ms']} ms), error {v['redraw_error']}, "
               f"C runtime bytes allocated in 59 frames {v['redraw_alloc_bytes']} (-1: release build), total CPU "
               f"load {v['cpu']} %")
    colour, mono, plain = results["emoji"], results["emoji in one colour"], results["ascii"]
    # Debug builds only: the frames in colour allocate no more than the same
    # frames in one colour (deferred fix T2: what is measured; every frame
    # allocates, mostly the core's rows of build_display_row).
    alloc_ok = colour["redraw_alloc_bytes"] == "-1" or int(colour["redraw_alloc_bytes"]) <= int(
        mono["redraw_alloc_bytes"])
    check("[M3d C4] the emoji screen redraws without errors; its frames in colour allocate no more than the "
          "same frames in one colour (debug builds)",
          all(v["redraw_error"] == "none" for v in results.values()) and alloc_ok and "failed" in mono["support"],
          f"errors {[v['redraw_error'] for v in results.values()]}; C runtime bytes in 59 frames: emoji in colour "
          f"{colour['redraw_alloc_bytes']}, the same in one colour {mono['redraw_alloc_bytes']}, ASCII "
          f"{plain['redraw_alloc_bytes']} (-1: release build, not measured); one colour: "
          f"color_support={mono['support']!r}")


def run_m3d_visible_checks(exe: pathlib.Path, out: pathlib.Path):
    """--visible: F10, Left, Right and Esc through SendInput on an unlocked
    desktop (decision D6)."""
    work = out / "files_m3d_visible"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = no_settings(out)
    doc = work / "v.txt"
    doc.write_bytes(b"hello world\nsecond line\n")
    app = d.App(exe, [str(doc)], cwd=work, settings=settings)
    try:
        if app.use_input() != "sendinput":
            record("SKIP", M3D_VISIBLE_F10, "geditor cannot take the foreground (locked desktop or --input post)")
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
                f"{expected}: popup={state.get('menu_popup')!r} popups={state.get('menu_popups')!r} " \
                f"#32768={menus} ({shot})"

        app.keys("<f10>")  # SendInput: F10 down and up
        file_ok, file_ev = popup("File", "m3d_v1_f10_file.png")
        app.keys("<left>")  # one key: through the system menu to View
        view_ok, view_ev = popup("View", "m3d_v2_left_view.png")
        app.keys("<right>")  # one key: through the system menu to File
        back_ok, back_ev = popup("File", "m3d_v3_right_file.png")
        popups = app.state().get("menu_popups", "")
        app.keys("ESC")
        time.sleep(0.4)
        after_esc = app.state()
        closed = not d.windows_of(app.proc.pid, "#32768")
        d.capture(app.hwnd, shots / "m3d_v4_esc.png")
        app.keys("ESC")
        time.sleep(0.4)
        after_esc2 = app.state()
        # One key press each: View right after File (the system menu may open
        # in between), File right after View.
        order = [p for p in popups.split(",") if p and p != "System"]
        order_ok = order[:3] == ["File", "View", "File"]
        check(M3D_VISIBLE_F10,
              file_ok and view_ok and back_ok and order_ok and closed and after_esc2.get("menu_loop_active") == "0",
              f"{file_ev}; {view_ev}; {back_ev}; drop-downs in order without System {order}; after Esc: #32768 "
              f"closed={closed} popup={after_esc.get('menu_popup')!r} (m3d_v4_esc.png); after a second Esc: menu "
              f"loop active={after_esc2.get('menu_loop_active')}")
    finally:
        app.kill()


# ---- M4a: line ends, saving and read-only ----

M4A_COLORS = {
    "background": "#000000", "foreground": "#ff0000", "cursor": "#ffffff", "selection": "#400040",
    "whitespace": "#ffff00", "eol": "#00ff00", "eol_other": "#0000ff",
}
# Line ends of the mixed sample: CRLF twice, LF and CR once: Mixed(CRLF).
M4A_MIXED = b"line1\r\nline2\nline3\rline4\r\nlast"
M4A_LINES = [("line1", "CRLF"), ("line2", "LF"), ("line3", "CR"), ("line4", "CRLF"), ("last", None)]


def wait_state(app, predicate, timeout_s: float = 3.0) -> dict:
    """The state once predicate(state) holds (the last state otherwise)."""
    found = d.wait_for(lambda: (lambda s: s if predicate(s) else None)(app.state()), timeout_s)
    return found if found is not None else app.state()


def temporaries(folder: pathlib.Path) -> list[str]:
    return sorted(p.name for p in folder.iterdir() if ".geditor-tmp" in p.name or ".geditor-old" in p.name)


def lit_mask(shot, box) -> frozenset:
    """The lit pixels of `box` relative to its corner (the shape of a symbol)."""
    w, h, px = shot
    x0, y0, x1, y1 = box
    cells = set()
    for y in range(max(0, y0), min(h, y1)):
        for x in range(max(0, x0), min(w, x1)):
            i = (y * w + x) * 4
            if max(px[i], px[i + 1], px[i + 2]) > 60:
                cells.add((x - x0, y - y0))
    return frozenset(cells)


def menu_items(app) -> dict:
    return {item.path: item for item in app.menu_bar()}


def run_m4a_fix_cr_lf_check(exe: pathlib.Path, work: pathlib.Path, shots: pathlib.Path, settings: pathlib.Path,
                            names: list):
    """M4a fix F1 ([C2]): the user's case. A mixed file "a" CR, "x" LF,
    "b" CRLF; emptying the LF line puts its LF right after the CR. Saving
    writes it as CR (HEAD edef20c wrote b"a\\r\\nb\\r\\n", read back as one
    line less), says how many, and the reopened file has the same lines."""
    path = work / "m4a_fix_cr_lf.txt"
    path.write_bytes(b"a\rx\nb\r\n")
    app = d.App(exe, [str(path)], cwd=work, settings=settings, height=300)
    try:
        app.use_input()
        before = wait_state(app, lambda s: s.get("text") == "a\nx\nb\n")
        app.keys("M-< C-n C-k")
        wait_state(app, lambda s: s.get("text") == "a\n\nb\n")
        app.keys("C-x C-s")
        state = wait_state(app, lambda s: s.get("modified") == "0")
        time.sleep(0.3)
        shot = app.capture(shots / "m4a_05_cr_lf_saved.png")
        names.append("m4a_05_cr_lf_saved.png")
        data = path.read_bytes()
        want_message = f"Wrote {path}; saved 1 LF line end after a CR as CR to keep the lines"
        check("[M4a fix F1] an LF line end right after a CR line end is saved as CR; the echo area gives the count",
              data == b"a\r\rb\r\n" and state.get("message") == want_message and state.get("modified") == "0" and
              state.get("text") == "a\n\nb\n",
              f"lines before={before.get('text')!r}; bytes={data!r} message={state.get('message')!r} "
              f"modified={state.get('modified')}")
        # Marks: after the save the default is that of the saved file, CR (two
        # CR, one CRLF; deferred fix F1: HEAD 6064002 kept the CRLF of the
        # read), so the two CR lines are eol (green) and the CRLF line
        # eol_other (blue).
        marks = []
        for row, (text, kind) in enumerate([("a", "CR"), ("", "CR"), ("b", "CRLF")]):
            box = cell_box(state, row, len(text), 2)
            marks.append((kind, count_where(shot, is_green, box), count_where(shot, is_blue, box)))
        check("[M4a fix F1] after the save the buffer's line ends are those of the file (marks: CR, CR, CRLF) "
              "and its default is the file's (Mixed(CR), as reopening gives)",
              [(g > 0, b > 0) for _, g, b in marks] == [(True, False), (True, False), (False, True)] and
              state.get("eol") == "Mixed(CR)",
              f"(kind, eol px, eol_other px) per row {marks}; eol={state.get('eol')} (m4a_05_cr_lf_saved.png)")
    finally:
        app.kill()
    app = d.App(exe, [str(path)], cwd=work, settings=settings, height=300)
    try:
        app.use_input()
        state = wait_state(app, lambda s: s.get("text") == "a\n\nb\n")
        time.sleep(0.3)
        app.capture(shots / "m4a_06_cr_lf_reopened.png")
        names.append("m4a_06_cr_lf_reopened.png")
        check("[M4a fix F1] reopening the saved file gives the same three lines and an empty last line, and the "
              "same default line end (Mixed(CR))",
              state.get("text") == "a\n\nb\n" and path.read_bytes() == b"a\r\rb\r\n" and
              state.get("eol") == "Mixed(CR)",
              f"text={state.get('text')!r} eol={state.get('eol')} (m4a_06_cr_lf_reopened.png)")
    finally:
        app.kill()


def run_m4a_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C2] (real app part), [C4], [C5], [C6] (headless capture), [C7] and
    [C10] of the M4a packet on the real app."""
    work = out / "files_m4a"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    settings.write_bytes(settings_json({"colors": M4A_COLORS, "line_numbers": False}))
    names = []

    # [C6]: the three symbols, eol / eol_other and Mixed(CRLF) (M4-8).
    mixed = work / "m4a_mixed.txt"
    mixed.write_bytes(M4A_MIXED)
    app = d.App(exe, [str(mixed)], cwd=work, settings=settings, height=400)
    try:
        record("INFO", "M4a input mode", app.use_input())
        # The cursor stays at the start of the buffer, away from the marks.
        state = wait_state(app, lambda s: s.get("eol") == "Mixed(CRLF)")
        time.sleep(0.3)
        shot = app.capture(shots / "m4a_01_mixed_marks.png")
        names.append("m4a_01_mixed_marks.png")
        rows, masks = [], []
        ok = True
        for row, (text, kind) in enumerate(M4A_LINES):
            box = cell_box(state, row, len(text), 2)
            green, blue = count_where(shot, is_green, box), count_where(shot, is_blue, box)
            masks.append(lit_mask(shot, box))
            want_green = kind == "CRLF"
            want_blue = kind in ("LF", "CR")
            ok = ok and (green > 0) == want_green and (blue > 0) == want_blue
            rows.append(f"row {row} ({kind or 'none'}): eol px {green}, eol_other px {blue}")
        distinct = len({masks[0], masks[1], masks[2]}) == 3 and masks[0] == masks[3] and not masks[4]
        check("[M4a C6] mixed file: CRLF lines (the default) in eol, the LF and CR lines in eol_other",
              ok, "; ".join(rows) + " (m4a_01_mixed_marks.png: green ↵ on rows 1 and 4, blue ↓ and ← on 2 and 3)")
        check("[M4a C6] the three symbols differ (↵ ↓ ←) and no mark after the last line",
              distinct, f"lit px per mark {[len(m) for m in masks]}")
        check("[M4a C6] the mode line says Mixed(CRLF)",
              "UTF-8 Mixed(CRLF)" in state.get("mode_line", "") and state.get("eol_default") == "CRLF",
              f"mode_line={state.get('mode_line')!r}")
        mode_px = count_where(shot, is_lit, (0, int(float(state["mode_line_top"])), shot[0],
                                             int(float(state["mode_line_top"]) + float(state["line_height"]))))
        record("INFO", "[M4a C6] mode line drawn", f"{mode_px} lit px in the mode line row (m4a_01_mixed_marks.png)")

        # [C7]: the real menu bar marks the default and enables these items.
        items = menu_items(app)
        radio = {k: items[f"File > Line Endings > {k}"] for k in ("LF", "CRLF", "CR")}
        enc = [i for p, i in items.items() if p.startswith("File > Encoding for Saving > ")]
        # Since M4b the Encoding for Saving items are enabled too (--only-m4b has their marks).
        check("[M4a C7] File > Line Endings marks CRLF (the default of the mixed file); LF / CRLF / CR, "
              "Read Only and (M4b) the Encoding for Saving items enabled",
              radio["CRLF"].checked and not radio["LF"].checked and not radio["CR"].checked and
              all(i.enabled and i.radio for i in radio.values()) and items["File > Read Only"].enabled and
              not items["File > Read Only"].checked and len(enc) == 9 and all(i.enabled for i in enc),
              "; ".join(i.line() for i in radio.values()) + f"; {items['File > Read Only'].line()}; "
              f"{len(enc)} encoding items, enabled {[i.path for i in enc if i.enabled]}")

        # set-eol from the menu: every mark the default colour; undo brings the mix back.
        app.menu_command(radio["LF"].id)
        state = wait_state(app, lambda s: s.get("eol") == "LF")
        time.sleep(0.3)
        shot = app.capture(shots / "m4a_02_set_eol_lf.png")
        names.append("m4a_02_set_eol_lf.png")
        greens = [count_where(shot, is_green, cell_box(state, r, len(t), 2)) for r, (t, _) in enumerate(M4A_LINES[:4])]
        blues = [count_where(shot, is_blue, cell_box(state, r, len(t), 2)) for r, (t, _) in enumerate(M4A_LINES[:4])]
        items = menu_items(app)
        check("[M4a C7] File > Line Endings > LF runs set-eol: LF everywhere in eol colour, modified, LF marked",
              state.get("eol") == "LF" and state.get("modified") == "1" and all(g > 0 for g in greens) and
              not any(blues) and items["File > Line Endings > LF"].checked,
              f"eol={state.get('eol')} modified={state.get('modified')} message={state.get('message')!r}; "
              f"eol px {greens}, eol_other px {blues} (m4a_02_set_eol_lf.png)")
        app.keys("C-/")
        state = wait_state(app, lambda s: s.get("eol") == "Mixed(CRLF)")
        check("[M4a C3] one undo restores the mixed line ends and the default",
              state.get("eol") == "Mixed(CRLF)" and state.get("modified") == "0",
              f"eol={state.get('eol')} modified={state.get('modified')}")

        # [C3] typed newline and pasted CRLF / CR text get the default (CRLF).
        app.set_clipboard("p\r\nq\rr")
        app.keys("M-> RET C-y")
        state = wait_state(app, lambda s: s.get("modified") == "1" and s.get("text", "").endswith("r"))
        # [C4] save: "<file>~" on the first save, the bytes per line end.
        app.keys("C-x C-s")
        state = wait_state(app, lambda s: s.get("modified") == "0")
        expected = M4A_MIXED + b"\r\np\r\nq\r\nr"
        backup = pathlib.Path(str(mixed) + "~")
        check("[M4a C2/C3] saved: every other line keeps its line end; typed and pasted line ends are CRLF",
              mixed.read_bytes() == expected, f"bytes={mixed.read_bytes()!r} message={state.get('message')!r}")
        check("[M4a C4] the first save makes <file>~ with the old content; no temporary file is left",
              backup.exists() and backup.read_bytes() == M4A_MIXED and not temporaries(work),
              f"{backup.name}={backup.read_bytes() if backup.exists() else None!r} temporaries={temporaries(work)}")
        app.type("2")
        app.keys("C-x C-s")
        state = wait_state(app, lambda s: s.get("modified") == "0")
        check("[M4a C4] the second save does not touch <file>~",
              backup.read_bytes() == M4A_MIXED and mixed.read_bytes() == expected + b"2",
              f"{backup.name}={backup.read_bytes()!r}")

        # [C3] copy writes CRLF to the (private) clipboard whatever each line's own line end.
        app.keys("M-< C-n C-SPC C-n C-n M-w")
        clip = app.clipboard()
        check("[M4a C3] copying the LF and CR lines puts CRLF on the clipboard",
              clip == "line2\r\nline3\r\n", f"clipboard_text={clip!r}")
        app.keys("C-g")

        # [C4] external change: the question, No writes nothing, Yes saves.
        mixed.write_bytes(b"changed elsewhere, a longer text\n")
        app.keys("M->")
        app.type("3")
        app.keys("C-x C-s")
        question = answer(app, "No", timeout_s=3.0)
        state = wait_state(app, lambda s: s.get("question") == "none" and s.get("message") == "Not saved")
        check("[M4a C4] a file changed on disk asks before saving; No writes nothing and says 'Not saved'",
              question is not None and question.get("question") == "yes_no" and
              question.get("instruction") == "m4a_mixed.txt has changed on disk since it was read; save anyway?" and
              mixed.read_bytes() == b"changed elsewhere, a longer text\n" and state.get("modified") == "1",
              f"{describe(question)} message={state.get('message')!r} bytes={mixed.read_bytes()!r}")
        app.keys("C-x C-s")
        question = answer(app, "Yes", timeout_s=3.0)
        state = wait_state(app, lambda s: s.get("modified") == "0")
        check("[M4a C4] Yes saves over the changed file",
              question is not None and mixed.read_bytes() == expected + b"23" and state.get("modified") == "0",
              f"{describe(question)} message={state.get('message')!r}")

        # [C4] a file deleted on disk is saved as new without a question.
        mixed.unlink()
        app.type("4")
        app.keys("C-x C-s")
        state = wait_state(app, lambda s: s.get("modified") == "0")
        check("[M4a C4] a deleted file is saved as new without a question",
              mixed.exists() and mixed.read_bytes() == expected + b"234" and state.get("question") == "none",
              f"message={state.get('message')!r} question={state.get('question')}")
    finally:
        app.kill()

    # [C4] / [C10]: every step of the save failing ('GFLT' "save-..."): the
    # reason in the echo area, the file and <file>~ unchanged, no temporary
    # file, the buffer still modified.
    fault_dir = work / "faults"
    fault_dir.mkdir(exist_ok=True)
    target = fault_dir / "f.txt"
    target.write_bytes(b"original\r\n")
    old_backup = fault_dir / "f.txt~"
    old_backup.write_bytes(b"older backup")
    faults = [
        # M4a fix F5: the backup step names the backup.
        ("save-backup", f"making the backup {target}~: Access is denied"),
        ("save-inspect", "reading the file attributes: Access is denied"),
        ("save-link", "resolving the link: The name of the file cannot be resolved"),
        ("save-temp-create", "creating the temporary file: Access is denied"),
        ("save-temp-write", "writing the temporary file: The disk is full"),
        ("save-flush", "flushing the temporary file: The disk is full"),
        ("save-replace", "replacing the file: Access is denied"),
        ("file-info", "reading the file time: Access is denied"),
    ]
    app = d.App(exe, [str(target)], cwd=fault_dir, settings=settings, height=400)
    try:
        app.use_input()
        app.type("x")
        wait_state(app, lambda s: s.get("modified") == "1")
        for i, (fault, step) in enumerate(faults):
            app.fault(fault)
            app.keys("C-x C-s")
            want = f"Cannot write {target}: {step}"
            state = wait_state(app, lambda s, w=want: s.get("message") == w)
            if i == 0:
                app.capture(shots / "m4a_03_save_failed.png")
                names.append("m4a_03_save_failed.png")
            check(f"[M4a C4/C10] '{fault}' fails the save with the step and the reason; nothing changes",
                  state.get("message") == want and target.read_bytes() == b"original\r\n" and
                  old_backup.read_bytes() == b"older backup" and not temporaries(fault_dir) and
                  state.get("modified") == "1" and state.get("mode_line", "").startswith("-**"),
                  f"message={state.get('message')!r} bytes={target.read_bytes()!r} "
                  f"{old_backup.name}={old_backup.read_bytes()!r} temporaries={temporaries(fault_dir)}"
                  + (" (m4a_03_save_failed.png)" if i == 0 else ""))
        app.keys("C-x C-s")
        state = wait_state(app, lambda s: s.get("modified") == "0")
        check("[M4a C4] after the failures the save works and makes <file>~ (the first successful save)",
              target.read_bytes() == b"xoriginal\r\n" and old_backup.read_bytes() == b"original\r\n" and
              not temporaries(fault_dir), f"message={state.get('message')!r}")
    finally:
        app.kill()

    # A new file: the move step fails, then works; no <file>~.
    new_file = fault_dir / "new.txt"
    app = d.App(exe, [str(new_file)], cwd=fault_dir, settings=settings, height=400)
    try:
        app.use_input()
        app.type("n")
        app.keys("RET")
        app.fault("save-move")
        app.keys("C-x C-s")
        want = f"Cannot write {new_file}: moving the temporary file: Access is denied"
        state = wait_state(app, lambda s: s.get("message") == want)
        check("[M4a C4/C10] a new file: 'save-move' fails with the reason, no file and no temporary file",
              state.get("message") == want and not new_file.exists() and not temporaries(fault_dir) and
              state.get("modified") == "1", f"message={state.get('message')!r}")
        app.keys("C-x C-s")
        state = wait_state(app, lambda s: s.get("modified") == "0")
        check("[M4a C4] the new file is then saved with the default line end (LF) and without <file>~",
              new_file.read_bytes() == b"n\n" and not pathlib.Path(str(new_file) + "~").exists(),
              f"bytes={new_file.read_bytes() if new_file.exists() else None!r}")
    finally:
        app.kill()

    # [C5]: a file with the read-only attribute opens read-only (%%).
    ro = work / "m4a_readonly.txt"
    ro.write_bytes(b"locked\r\n")
    os.chmod(ro, stat.S_IREAD)
    app = d.App(exe, [str(ro)], cwd=work, settings=settings, height=400)
    try:
        app.use_input()
        state = wait_state(app, lambda s: s.get("read_only") == "1")
        items = menu_items(app)
        app.type("x")
        state = wait_state(app, lambda s: s.get("message") == "Buffer is read-only")
        app.capture(shots / "m4a_04_read_only.png")
        names.append("m4a_04_read_only.png")
        check("[M4a C5] the read-only attribute: read-only-mode (%%), Read Only checked, typing refused",
              state.get("read_only") == "1" and state.get("mode_line", "").startswith("-%%") and
              items["File > Read Only"].checked and state.get("message") == "Buffer is read-only" and
              state.get("text") == "locked\n" and state.get("modified") == "0",
              f"mode_line={state.get('mode_line')!r} message={state.get('message')!r} "
              f"{items['File > Read Only'].line()} (m4a_04_read_only.png)")
        app.keys("C-x C-q")
        app.type("x")
        app.keys("C-x C-s")
        want = f"Cannot write {ro}: Access is denied (the file is read-only)"
        state = wait_state(app, lambda s: s.get("message") == want)
        check("[M4a C5] C-x C-q turns it off; saving then fails with the reason and the file is unchanged",
              state.get("message") == want and ro.read_bytes() == b"locked\r\n" and state.get("modified") == "1" and
              state.get("read_only") == "0" and not menu_items(app)["File > Read Only"].checked,
              f"message={state.get('message')!r} bytes={ro.read_bytes()!r}")
    finally:
        app.kill()
        os.chmod(ro, stat.S_IREAD | stat.S_IWRITE)

    # [C2]: utf-8 / utf-8-bom x LF / CRLF / CR / mixed, load -> save in the app.
    combos = []
    for bom in (False, True):
        for kind, ends in (("lf", ["\n"] * 3), ("crlf", ["\r\n"] * 3), ("cr", ["\r"] * 3),
                           ("mixed", ["\n", "\r\n", "\r"])):
            body = f"日本語{ends[0]}😀 x{ends[1]}{ends[2]}last".encode("utf-8")
            data = (b"\xef\xbb\xbf" if bom else b"") + body
            path = work / f"m4a_rt_{'bom' if bom else 'nobom'}_{kind}.txt"
            path.write_bytes(data)
            app = d.App(exe, [str(path)], cwd=work, settings=settings, height=300)
            try:
                app.use_input()
                app.keys("M->")
                app.type("z")
                app.keys("DEL C-x C-s")
                state = wait_state(app, lambda s: s.get("modified") == "0" and s.get("message", "").startswith("Wrote"))
                combos.append((path.name, path.read_bytes() == data, state.get("eol")))
            finally:
                app.kill()
    check("[M4a C2] utf-8 and utf-8-bom x LF / CRLF / CR / mixed: load -> edit -> undo by DEL -> save gives "
          "the same bytes", all(ok for _, ok, _ in combos), "; ".join(f"{n} {'same' if ok else 'DIFFERENT'} ({e})"
                                                                 for n, ok, e in combos))

    run_m4a_fix_cr_lf_check(exe, work, shots, settings, names)

    # [C4] F5: a hard link keeps both names in sync; a symbolic link stays a link.
    links = work / "links"
    links.mkdir(exist_ok=True)
    a, b = links / "a.txt", links / "b.txt"
    a.write_bytes(b"old\n")
    if b.exists():
        b.unlink()
    made = kernel32.CreateHardLinkW(str(b), str(a), None)
    app = d.App(exe, [str(a)], cwd=links, settings=settings, height=300)
    try:
        app.use_input()
        app.type("H")
        # M4a fix F5: the backup copy of the hard-link path failing names the backup.
        app.fault("save-backup")
        app.keys("C-x C-s")
        want = f"Cannot write {a}: making the backup {a}~: Access is denied"
        state = wait_state(app, lambda s: s.get("message") == want)
        check("[M4a fix F5] a hard link: 'save-backup' fails naming the backup; nothing is written",
              state.get("message") == want and a.read_bytes() == b"old\n" and b.read_bytes() == b"old\n" and
              not pathlib.Path(str(a) + "~").exists() and not temporaries(links) and state.get("modified") == "1",
              f"message={state.get('message')!r} a={a.read_bytes()!r} b={b.read_bytes()!r}")
        app.keys("C-x C-s")
        wait_state(app, lambda s: s.get("modified") == "0")
        nlink = os.stat(a).st_nlink
        check("[M4a C4] a hard link: both names get the new content, they stay linked, <file>~ is a copy",
              bool(made) and a.read_bytes() == b"Hold\n" and b.read_bytes() == b"Hold\n" and nlink == 2 and
              pathlib.Path(str(a) + "~").read_bytes() == b"old\n",
              f"links={nlink} a={a.read_bytes()!r} b={b.read_bytes()!r}")
    finally:
        app.kill()
    real, link = links / "real.txt", links / "link.txt"
    real.write_bytes(b"target\n")
    try:
        if link.exists() or link.is_symlink():
            link.unlink()
        os.symlink(real, link)
        symlink_error = None
    except OSError as exc:
        symlink_error = str(exc)
    if symlink_error is not None:
        record("SKIP", "[M4a C4] a symbolic link stays a link with its target updated",
               f"symbolic links cannot be created here ({symlink_error}); needs Developer Mode or "
               "SeCreateSymbolicLinkPrivilege")
    else:
        app = d.App(exe, [str(link)], cwd=links, settings=settings, height=300)
        try:
            app.use_input()
            app.type("S")
            app.keys("C-x C-s")
            wait_state(app, lambda s: s.get("modified") == "0")
            check("[M4a C4] a symbolic link stays a link with its target updated",
                  link.is_symlink() and real.read_bytes() == b"Starget\n" and
                  pathlib.Path(str(real) + "~").read_bytes() == b"target\n", f"target={real.read_bytes()!r}")
        finally:
            app.kill()
    record("INFO", "[M4a] screenshots", ", ".join(names))


# ---- M4b: encodings ----

M4B_TEXT = "日本語テキスト\r\nかな ABC\r\n"
M4B_WANT = M4B_TEXT.replace("\r\n", "\n")


def m4b_open(exe: pathlib.Path, path: pathlib.Path, settings: pathlib.Path, args: list[str] | None = None):
    app = d.App(exe, [*(args or []), str(path)], cwd=path.parent, settings=settings, height=300)
    app.use_input()
    return app


def encoding_marks(app) -> list[str]:
    """The checked items of File > Encoding for Saving in the real HMENU."""
    prefix = "File > Encoding for Saving > "
    return [p[len(prefix):] for p, i in menu_items(app).items() if p.startswith(prefix) and i.checked]


def run_m4b_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C9] of the M4b packet: detection, find-file-with-encoding (minibuffer
    and menu), revert-buffer-with-coding-system, the "!" of the mode line, a
    refused save, --encoding and the Encoding for Saving radio of the real
    HMENU, all on the real app."""
    work = out / "files_m4b"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    settings.write_bytes(settings_json({"line_numbers": False}))
    names = []

    # Detection: each BOM kind, UTF-16LE without BOM, Shift_JIS, UTF-8.
    samples = [
        ("utf-8-bom", b"\xef\xbb\xbf" + M4B_TEXT.encode("utf-8")),
        ("utf-16le-bom", b"\xff\xfe" + M4B_TEXT.encode("utf-16-le")),
        ("utf-16be-bom", b"\xfe\xff" + M4B_TEXT.encode("utf-16-be")),
        ("utf-16le", M4B_TEXT.encode("utf-16-le")),
        ("shift_jis", M4B_TEXT.encode("cp932")),
        ("utf-8", M4B_TEXT.encode("utf-8")),
    ]
    rows = []
    for name, data in samples:
        path = work / f"m4b_detect_{name}.txt"
        path.write_bytes(data)
        app = m4b_open(exe, path, settings)
        try:
            state = wait_state(app, lambda s: s.get("text") == M4B_WANT)
            if name == "shift_jis":
                time.sleep(0.3)
                app.capture(shots / "m4b_01_detect_shift_jis.png")
                names.append("m4b_01_detect_shift_jis.png")
            rows.append((name, state.get("encoding"), state.get("text") == M4B_WANT, encoding_marks(app),
                         state.get("encoding_text")))
        finally:
            app.kill()
    check("[M4b C9] detection: each BOM kind, UTF-16LE without BOM, Shift_JIS and UTF-8 open with their encoding "
          "and the right text; the real HMENU's Encoding for Saving radio marks it",
          all(enc == name and ok and marks == [name] for name, enc, ok, marks, _ in rows),
          "; ".join(f"{n}: encoding={e} text {'ok' if ok else 'WRONG'} radio={m} mode line {t}"
                    for n, e, ok, m, t in rows) + " (m4b_01_detect_shift_jis.png)")

    # find-file-with-encoding through the minibuffer with completion.
    euc = work / "m4b_euc.txt"
    euc.write_bytes(M4B_TEXT.encode("euc_jp"))
    iso = work / "m4b_iso.txt"
    iso.write_bytes(M4B_TEXT.encode("iso2022_jp"))
    start = work / "m4b_start.txt"
    start.write_bytes(b"start\n")
    app = m4b_open(exe, start, settings)
    try:
        app.keys("C-x RET c")
        prompt = wait_state(app, lambda s: s.get("minibuffer") == "1").get("echo")
        app.type("m4b_eu")
        app.keys("TAB")
        completed = wait_state(app, lambda s: s.get("echo", "").endswith("m4b_euc.txt")).get("echo")
        app.keys("RET")
        app.type("eu")
        app.keys("TAB")
        coding = wait_state(app, lambda s: s.get("echo") == "Coding system: euc-jp").get("echo")
        app.keys("RET")
        question = answer(app, "Open in this window", timeout_s=3.0)
        state = wait_state(app, lambda s: s.get("text") == M4B_WANT)
        time.sleep(0.3)
        app.capture(shots / "m4b_02_find_file_with_encoding.png")
        names.append("m4b_02_find_file_with_encoding.png")
        check("[M4b C9] C-x RET c: the file name completes in the minibuffer, the encoding completes (euc -> "
              "euc-jp), and the file opens in EUC-JP",
              prompt == f"Find file: {work}\\" and completed == f"Find file: {euc}" and
              coding == "Coding system: euc-jp" and question is not None and state.get("encoding") == "euc-jp" and
              state.get("text") == M4B_WANT and "  EUC-JP CRLF  " in state.get("mode_line", ""),
              f"prompt={prompt!r} completed={completed!r} coding={coding!r} {describe(question)} "
              f"encoding={state.get('encoding')} mode_line={state.get('mode_line')!r} "
              "(m4b_02_find_file_with_encoding.png)")

        # The menu (F10, answered through 'GANS'), into a new window: the
        # encoding goes with it (detection would read ISO-2022-JP as UTF-8).
        app.keys("<f10>")
        pending = app.wait_question(3.0)
        accepted = app.answer("File > Open with Encoding...")
        menu_prompt = wait_state(app, lambda s: s.get("minibuffer") == "1").get("echo")
        app.type(iso.name)
        app.keys("RET")
        app.type("iso-2022-jp")
        app.keys("RET")
        before = set(d.started_windows())
        question = answer(app, "Open in a new window", timeout_s=3.0)
        new = d.wait_for(lambda: [h for h in d.started_windows() if h not in before], 10.0) or []
        theirs = d.wait_for(lambda: (lambda s: s if s.get("text") == M4B_WANT else None)(d.read_state(new[0])),
                            5.0) if new else None
        theirs = theirs or (d.read_state(new[0]) if new else {})
        check("[M4b C9] File > Open with Encoding... (F10 answered through GANS) reads both arguments; "
              "'Open in a new window' starts a window that reads the file in that encoding (--encoding)",
              pending is not None and pending.get("question") == "menu_bar" and accepted and
              menu_prompt == f"Find file: {euc.parent}\\" and question is not None and bool(new) and
              theirs.get("encoding") == "iso-2022-jp" and theirs.get("text") == M4B_WANT and
              theirs.get("headless") == "1",
              f"pending={pending and pending.get('question')} accepted={accepted} prompt={menu_prompt!r} "
              f"{describe(question)} new windows={len(new)} encoding={theirs.get('encoding')} "
              f"text={'ok' if theirs.get('text') == M4B_WANT else theirs.get('text')!r}")
        for h in new:
            d.user32.PostMessageW(h, d.WM_CLOSE, 0, 0)  # unmodified: closes
        d.wait_for(lambda: not any(d.user32.IsWindow(h) for h in new), 5.0)
    finally:
        app.kill()

    # --encoding at start-up (a wrong one: "!"), then revert with the right
    # one, the question answered No and then Yes.
    sjis = work / "m4b_sjis.txt"
    sjis.write_bytes(M4B_TEXT.encode("cp932"))
    app = m4b_open(exe, sjis, settings, ["--encoding=utf-8"])
    try:
        state = wait_state(app, lambda s: s.get("encoding_text") == "UTF-8!")
        time.sleep(0.3)
        app.capture(shots / "m4b_03_undecodable_mark.png")
        names.append("m4b_03_undecodable_mark.png")
        check("[M4b C9] --encoding=utf-8 reads a Shift_JIS file as UTF-8: U+FFFD, the first byte position and "
              "line in the echo area, and UTF-8! in the mode line",
              state.get("encoding") == "utf-8" and "�" in state.get("text", "") and
              state.get("message") == f"{sjis}: bytes that are not valid UTF-8 were read as U+FFFD "
                                      "(first at byte offset 0, line 1)" and
              "  UTF-8! CRLF  " in state.get("mode_line", ""),
              f"message={state.get('message')!r} mode_line={state.get('mode_line')!r} "
              "(m4b_03_undecodable_mark.png: 'UTF-8!' in the mode line)")
        app.keys("M->")
        app.type("x")
        edited = wait_state(app, lambda s: s.get("modified") == "1")
        app.keys("C-x RET r")
        app.type("shift_jis")
        app.keys("RET")
        question = answer(app, "No", timeout_s=3.0)
        state = wait_state(app, lambda s: s.get("question") == "none" and s.get("message") == "Cancelled")
        check("[M4b C9] revert with unsaved changes asks; No changes nothing",
              question is not None and question.get("question") == "yes_no" and
              question.get("instruction") == "Discard the changes to m4b_sjis.txt and read it again as Shift_JIS?"
              and state.get("text") == edited.get("text") and state.get("modified") == "1" and
              state.get("encoding") == "utf-8" and state.get("point") == edited.get("point"),
              f"{describe(question)} message={state.get('message')!r} encoding={state.get('encoding')}")
        app.keys("C-x RET r")
        app.type("shift_jis")
        app.keys("RET")
        question = answer(app, "Yes", timeout_s=3.0)
        state = wait_state(app, lambda s: s.get("encoding") == "shift_jis" and s.get("text") == M4B_WANT)
        time.sleep(0.3)
        app.capture(shots / "m4b_04_reverted.png")
        names.append("m4b_04_reverted.png")
        check("[M4b C9] Yes reads the file again from disk in Shift_JIS: right text, no '!', unmodified, the "
              "radio on shift_jis",
              question is not None and state.get("text") == M4B_WANT and state.get("encoding_text") == "Shift_JIS"
              and state.get("modified") == "0" and encoding_marks(app) == ["shift_jis"],
              f"{describe(question)} message={state.get('message')!r} mode_line={state.get('mode_line')!r} "
              "(m4b_04_reverted.png)")

        # A character Shift_JIS cannot write: the save stops, the file stays.
        mtime = sjis.stat().st_mtime_ns
        app.keys("M-<")
        app.set_clipboard("\U0001F600")
        app.keys("C-y")
        wait_state(app, lambda s: s.get("modified") == "1")
        app.keys("C-x C-s")
        want = ("Cannot save as Shift_JIS: U+1F600 at line 1, column 0 cannot be written in Shift_JIS; "
                "choose another encoding with set-buffer-file-coding-system")
        state = wait_state(app, lambda s: s.get("message") == want)
        time.sleep(0.3)
        app.capture(shots / "m4b_05_refused_save.png")
        names.append("m4b_05_refused_save.png")
        check("[M4b C9] an emoji stops the Shift_JIS save at its position; the file, its time and the folder "
              "(no temporary file) are unchanged; the buffer stays modified",
              state.get("message") == want and sjis.read_bytes() == M4B_TEXT.encode("cp932") and
              sjis.stat().st_mtime_ns == mtime and not temporaries(work) and state.get("modified") == "1",
              f"message={state.get('message')!r} temporaries={temporaries(work)} (m4b_05_refused_save.png)")

        # Encoding for Saving from the real HMENU (WM_COMMAND), then the save works.
        items = menu_items(app)
        app.menu_command(items["File > Encoding for Saving > utf-16le-bom"].id)
        state = wait_state(app, lambda s: s.get("encoding") == "utf-16le-bom")
        marks = encoding_marks(app)
        app.keys("C-x C-s")
        saved = wait_state(app, lambda s: s.get("modified") == "0")
        data = sjis.read_bytes()
        check("[M4b C9] File > Encoding for Saving > utf-16le-bom: the radio moves, the text stays, the buffer is "
              "modified; the save writes UTF-16LE with BOM",
              marks == ["utf-16le-bom"] and state.get("modified") == "1" and
              "  UTF-16LE-BOM CRLF  " in state.get("mode_line", "") and
              data == b"\xff\xfe" + ("\U0001F600" + M4B_TEXT).encode("utf-16-le") and
              saved.get("message") == f"Wrote {sjis}",
              f"radio={marks} mode_line={state.get('mode_line')!r} bytes={data[:12]!r}... "
              f"message={saved.get('message')!r}")
    finally:
        app.kill()

    # --encoding=euc-jp at start-up; an unknown name is reported.
    app = m4b_open(exe, euc, settings, ["--encoding=euc-jp"])
    try:
        state = wait_state(app, lambda s: s.get("text") == M4B_WANT)
        check("[M4b C9] --encoding=euc-jp at start-up reads the file in EUC-JP",
              state.get("encoding") == "euc-jp" and state.get("text") == M4B_WANT and not state.get("message"),
              f"encoding={state.get('encoding')} message={state.get('message')!r}")
    finally:
        app.kill()
    app = m4b_open(exe, euc, settings, ["--encoding=big5"])
    try:
        state = wait_state(app, lambda s: "Unknown encoding" in s.get("message", ""))
        check("[M4b C9] an unknown --encoding is reported in the echo area (the file is detected)",
              "Unknown encoding: big5 (use utf-8, utf-8-bom, " in state.get("message", "") and
              state.get("encoding") != "big5",
              f"message={state.get('message')!r} encoding={state.get('encoding')}")
    finally:
        app.kill()
    record("INFO", "[M4b] screenshots", ", ".join(names))


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


# ---- M5: search and replace ----

M5_COLORS = {
    "background": "#000000", "foreground": "#ff0000", "cursor": "#ffffff", "selection": "#400040",
    "whitespace": "#000000", "eol": "#000000", "search_match": "#00ff00", "search_current": "#0000ff",
}
# Row 0: "foo" at 0 and 8; row 1: "foo" at 3 (offsets 15..18); row 2: none.
M5_TEXT = "foo bar foo\nxx foo\nnothing\n"
M5_ROWS = [(0, 0), (0, 8), (1, 3)]  # (screen row, first cell) of each "foo"
# The 100MB file of [M5-5]: lines of 79 ASCII characters and "\n"; the word
# is on the 100th line from the end and starts with "§", which no other line
# has, so the first key of the search already scans the whole file.
M5_BIG_WORD = "§zebracorn"
M5_BIG_LINES = 1_310_720


def m5_big_file(folder: pathlib.Path) -> tuple[pathlib.Path, int]:
    """The 100MB sample (made once under build/) and the offset of the word."""
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "m5_100mb.txt"
    line = ("the quick brown fox jumps over the lazy dog 0123456789 " + "abcdefghijklmnopqrstuvwxyz")[:79] + "\n"
    target = M5_BIG_LINES - 100
    word_line = M5_BIG_WORD + line[len(M5_BIG_WORD):]
    offset = target * 80
    size = M5_BIG_LINES * 80 + len(M5_BIG_WORD.encode("utf-8")) - len(M5_BIG_WORD)
    if not path.exists() or path.stat().st_size != size:
        block = (line * 4096).encode("ascii")
        with path.open("wb") as f:
            for _ in range(target // 4096):
                f.write(block)
            f.write(line.encode("ascii") * (target % 4096))
            f.write(word_line.encode("utf-8"))
            f.write(line.encode("ascii") * (M5_BIG_LINES - target - 1))
    return path, offset


def m5_highlight_counts(shot, state) -> list[tuple[int, int]]:
    """(green, blue) pixels in the three "foo" cells of M5_TEXT."""
    return [(count_where(shot, is_green, cell_box(state, row, first, 3)),
             count_where(shot, is_blue, cell_box(state, row, first, 3))) for row, first in M5_ROWS]


def m5_text_area_colour(shot, state, pred) -> int:
    lh = float(state["line_height"])
    return count_where(shot, pred, (0, 0, shot[0], int(3 * lh)))


def run_m5_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C5] (headless key-to-found), [C6], [C7], [C9] and [C11] of the M5
    packet on the real app (headless)."""
    work = out / "files_m5"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    settings.write_bytes(settings_json({"colors": M5_COLORS, "line_numbers": False,
                                        "eol_marks": {"show": False}}))
    sample = work / "m5_sample.txt"
    sample.write_bytes(M5_TEXT.encode("utf-8"))
    app = d.App(exe, [str(sample)], cwd=work, settings=settings, height=400)
    try:
        record("INFO", "M5 input mode", app.use_input())
        state = wait_state(app, lambda s: s.get("text") == M5_TEXT)
        # [C11] no pattern compiled before the first search.
        check("[M5 C11] PCRE2 compiles nothing until the first search (search_compiles=0 after start-up)",
              state.get("search_compiles") == "0", f"search_compiles={state.get('search_compiles')!r}")

        # [C9] isearch: every visible match in search_match (green), the
        # current one in search_current (blue).
        app.keys("C-s")
        app.type("foo")
        state = wait_state(app, lambda s: s.get("search_current") == "0-3")
        time.sleep(0.3)
        shot = app.capture(shots / "m5_01_isearch_highlights.png")
        counts = m5_highlight_counts(shot, state)
        bar = (count_where(shot, is_green, cell_box(state, 0, 4, 3)), count_where(shot, is_blue, cell_box(state, 0, 4, 3)))
        check("[M5 C9] isearch: the current match (row 0 col 0) is search_current, the other two matches "
              "search_match, and \"bar\" neither",
              counts[0][1] > 0 and counts[0][0] == 0 and all(g > 0 and b == 0 for g, b in counts[1:]) and
              bar == (0, 0),
              f"(green, blue) px per match {counts}, \"bar\" {bar}; state matches={state.get('search_matches')!r} "
              f"current={state.get('search_current')!r} echo={state.get('echo')!r} (m5_01_isearch_highlights.png)")
        check("[M5 C11] the first search compiled its pattern",
              int(state.get("search_compiles", "0")) > 0, f"search_compiles={state.get('search_compiles')!r}")
        app.keys("C-s")
        state = wait_state(app, lambda s: s.get("search_current") == "8-11")
        time.sleep(0.3)
        shot = app.capture(shots / "m5_02_isearch_next.png")
        counts = m5_highlight_counts(shot, state)
        check("[M5 C9] C-s moves search_current to the second match",
              counts[1][1] > 0 and counts[1][0] == 0 and counts[0][0] > 0 and counts[0][1] == 0,
              f"(green, blue) px per match {counts}; point={state.get('point')} (m5_02_isearch_next.png)")
        # [C7] every menu item grey during isearch.
        items = menu_items(app)
        enabled = [p for p, i in items.items() if i.enabled and not i.separator and " > " in p]
        check("[M5 C7] during isearch every item of the real HMENU is grey", not enabled and len(items) > 10,
              f"{len(items)} items, enabled: {enabled[:5]}")
        app.keys("RET")
        state = wait_state(app, lambda s: s.get("isearch") == "0")
        time.sleep(0.3)
        shot = app.capture(shots / "m5_03_after_isearch.png")
        green, blue = m5_text_area_colour(shot, state, is_green), m5_text_area_colour(shot, state, is_blue)
        items = menu_items(app)
        back = [p for p, _, on in M3B_ITEMS if on and p in items and not items[p].enabled]
        check("[M5 C9] after RET no search_match / search_current pixel is left (negative)",
              green == 0 and blue == 0 and state.get("search_matches") == "" and state.get("search_current") == "none",
              f"green px {green}, blue px {blue}, matches={state.get('search_matches')!r} (m5_03_after_isearch.png)")
        check("[M5 C7] after isearch the items are enabled again", not back, f"still grey: {back[:5]}")

        # [C9] query-replace: the same colours; after it ends nothing.
        app.keys("M-<")
        app.keys("M-%")
        app.type("foo")
        app.keys("RET")
        app.type("X")
        app.keys("RET")
        state = wait_state(app, lambda s: s.get("query_replace") == "1")
        time.sleep(0.3)
        shot = app.capture(shots / "m5_04_query_replace.png")
        counts = m5_highlight_counts(shot, state)
        items = menu_items(app)
        enabled = [p for p, i in items.items() if i.enabled and not i.separator and " > " in p]
        check("[M5 C9] query-replace: the current match search_current, the others search_match",
              counts[0][1] > 0 and counts[0][0] == 0 and all(g > 0 and b == 0 for g, b in counts[1:]),
              f"(green, blue) px per match {counts}; echo={state.get('echo')!r} (m5_04_query_replace.png)")
        check("[M5 C7] during query-replace every item of the real HMENU is grey", not enabled,
              f"enabled: {enabled[:5]}")
        app.type("y")
        state = wait_state(app, lambda s: s.get("search_current") == "6-9")
        app.type("q")
        state = wait_state(app, lambda s: s.get("query_replace") == "0")
        time.sleep(0.3)
        shot = app.capture(shots / "m5_05_after_query_replace.png")
        green, blue = m5_text_area_colour(shot, state, is_green), m5_text_area_colour(shot, state, is_blue)
        items = menu_items(app)
        back = [p for p, _, on in M3B_ITEMS if on and p in items and not items[p].enabled]
        check("[M5 C9] query-replace y then q: one replacement, and no highlight is left (negative)",
              state.get("text") == M5_TEXT.replace("foo", "X", 1) and green == 0 and blue == 0 and
              state.get("message") == "Replaced 1 occurrence",
              f"text={state.get('text')!r} message={state.get('message')!r} green {green} blue {blue} "
              f"(m5_05_after_query_replace.png)")
        check("[M5 C7] after query-replace the items are enabled again", not back, f"still grey: {back[:5]}")
        app.keys("C-/")
        state = wait_state(app, lambda s: s.get("text") == M5_TEXT)
        check("[M5 C2] one undo restores the replaced text", state.get("text") == M5_TEXT,
              f"text={state.get('text')!r}")

        # [C7] Edit > Find / Replace / Go to Line through WM_COMMAND on the real HMENU.
        items = menu_items(app)
        app.menu_command(items["Edit > Find..."].id)
        find_state = wait_state(app, lambda s: s.get("isearch") == "1")
        app.keys("C-g")
        app.menu_command(items["Edit > Replace..."].id)
        replace_state = wait_state(app, lambda s: s.get("echo") == "Query replace: ")
        app.keys("C-g")
        app.menu_command(items["Edit > Go to Line..."].id)
        goto_state = wait_state(app, lambda s: s.get("echo") == "Goto line: ")
        app.type("2")
        app.keys("RET")
        line_state = wait_state(app, lambda s: s.get("point") == "12")
        check("[M5 C7] Edit > Find..., Replace... and Go to Line... are enabled and start isearch, the "
              "query-replace prompt and the goto-line prompt",
              all(items[p].enabled for p in ("Edit > Find...", "Edit > Replace...", "Edit > Go to Line...")) and
              find_state.get("echo") == "I-search: " and replace_state.get("echo") == "Query replace: " and
              goto_state.get("echo") == "Goto line: " and line_state.get("point") == "12",
              f"{items['Edit > Find...'].line()}; find echo={find_state.get('echo')!r}, replace echo="
              f"{replace_state.get('echo')!r}, goto echo={goto_state.get('echo')!r}, point after 2 RET="
              f"{line_state.get('point')}")

        # [C6] the Windows keys through posted Ctrl+Shift keys.
        app.keys("M-<")
        app.keys("C-S-f")
        app.type("bar")
        f_state = wait_state(app, lambda s: s.get("point") == "7")
        app.keys("RET")
        app.keys("C-S-g")
        g_state = wait_state(app, lambda s: s.get("echo") == "Goto line: ")
        app.keys("C-g")
        app.keys("C-S-h")
        h_state = wait_state(app, lambda s: s.get("echo") == "Query replace: ")
        app.keys("C-g")
        app.keys("C-S-r")
        r_state = wait_state(app, lambda s: s.get("echo") == "Query replace: ")
        app.keys("C-g")
        check("[M5 C6] C-S-f searches, C-S-g asks for a line, C-S-h and C-S-r ask what to replace",
              f_state.get("isearch") == "1" and f_state.get("point") == "7" and
              g_state.get("echo") == "Goto line: " and h_state.get("echo") == "Query replace: " and
              r_state.get("echo") == "Query replace: ",
              f"C-S-f: isearch={f_state.get('isearch')} point={f_state.get('point')}; C-S-g echo="
              f"{g_state.get('echo')!r}; C-S-h echo={h_state.get('echo')!r}; C-S-r echo={r_state.get('echo')!r}")
        # [C8] C-S-s in isearch is the next match (not save), C-S-z ends it without undo.
        app.keys("M-<")
        app.keys("C-s")
        app.type("foo")
        app.keys("C-S-s")
        s_state = wait_state(app, lambda s: s.get("point") == "11")
        app.keys("C-S-z")
        z_state = wait_state(app, lambda s: s.get("isearch") == "0")
        check("[M5 C8] in isearch C-S-s goes to the next match (no save) and C-S-z ends isearch without undo",
              s_state.get("point") == "11" and s_state.get("modified") == "0" and
              z_state.get("text") == M5_TEXT and z_state.get("message") == "C-S-z is undefined",
              f"after C-S-s point={s_state.get('point')} echo={s_state.get('echo')!r}; after C-S-z "
              f"message={z_state.get('message')!r}")
    finally:
        app.kill()

    # [C5] a word near the end of a 100MB file, from the key to the state.
    big, offset = m5_big_file(out.parent.parent / "m5_search")
    app = d.App(exe, [str(big)], cwd=work, settings=settings, height=400)
    try:
        loaded = wait_state(app, lambda s: s.get("length") == str(big.stat().st_size - 1), 20.0)
        app.keys("C-s")
        wait_state(app, lambda s: s.get("isearch") == "1")
        start = time.perf_counter()
        app.post_chars("§", pause_s=0)
        state = d.wait_for(lambda: (lambda s: s if s.get("point") == str(offset + 1) else None)(app.state()),
                           10.0, 0.005)
        elapsed = (time.perf_counter() - start) * 1000
        ok = bool(state)
        record("INFO", "[M5 C5] 100MB file: \"§\" typed in isearch (the word on the 100th line from the end) "
               "to point at the match, headless, through a posted WM_CHAR and the state",
               f"{elapsed:.0f} ms incl. state polling every 5 ms (each 'GSTA' writes a file); found={ok}; "
               f"length={loaded.get('length')}")
        check("[M5 C5] the word near the end of the 100MB file is found (headless key to state within 1 s)",
              ok and elapsed < 1000, f"{elapsed:.0f} ms; point={state.get('point') if state else None} "
              f"(want {offset + 1})")
    finally:
        app.kill()


M6_BAD_SJIS = b"a\r\n\x85\x40b"  # 85: a lead byte without a trail byte -> U+FFFD
M6_SCROLL_BYTES = 10 * 1024 * 1024


def m6_single_line(folder: pathlib.Path) -> pathlib.Path:
    """A 10MB single line of ASCII and Japanese words (made once)."""
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "m6_single_line_10mb.txt"
    if not path.exists() or path.stat().st_size < M6_SCROLL_BYTES:
        unit = "editor piece tree 日本語 buffer テキスト line ".encode("utf-8")
        path.write_bytes(unit * (M6_SCROLL_BYTES // len(unit) + 1))
    return path


# A macro run that is still going long after it starts and keeps its text
# short, for the checks that post input during a run and stop it with C-g.
# A run that grows one line fast (100000 runs of 10 characters) is over in
# about 1 s since the deferred performance item K1 (2026-11-17), so those
# checks no longer rely on how fast a run grows: 499 letters are typed and
# 498 of them deleted, then RET (998 events that leave "a\n"). On the
# development PC it made about 225 to 350 runs a second (2026-11-17, with
# both the exe before K1 and after it), so 30000 runs of it last about 85 to
# 130 s and are less than 2% done 1 s in; its text never exceeds
# 2 + 2 * 30000 = 60002 characters, so the state's text (64K characters)
# holds all of it wherever the run stops.
SLOW_MACRO_LETTERS = ("abcdefghij" * 50)[:499]
SLOW_MACRO_EVENTS = 998
SLOW_MACRO_RUNS = 30000
SLOW_MACRO_FULL = 2 + 2 * SLOW_MACRO_RUNS  # the length after the recording and the whole run


def count_keys(count: int) -> str:
    """The keys of a prefix argument of `count`: M- with its first digit,
    then the other digits ("M-3 0 0 0 0" for 30000). C-u is
    backward-kill-line since 2026-11-17 (universal-argument has no default
    key)."""
    digits = str(count)
    return " ".join([f"M-{digits[0]}", *digits[1:]])


def record_slow_macro(app) -> None:
    """Records the slow macro in an empty buffer (which then holds "a\\n")."""
    app.keys("C-x (")
    app.type(SLOW_MACRO_LETTERS, pause_s=0.002)
    app.keys(" ".join(["DEL"] * (len(SLOW_MACRO_LETTERS) - 1)), pause_s=0.002)
    app.keys("RET")
    app.keys("C-x )")
    wait_state(app, lambda s: s.get("macro_events") == str(SLOW_MACRO_EVENTS), 10.0)


def slow_macro_text(text: str) -> bool:
    """`text` is what the recording and a run of the slow macro stopped
    anywhere leave: "a\\n" pairs, then the letters of an unfinished run (a
    prefix of SLOW_MACRO_LETTERS, being typed or deleted)."""
    i = 0
    while text.startswith("a\n", i):
        i += 2
    return i >= 2 and SLOW_MACRO_LETTERS.startswith(text[i:])


def run_deferred_checks(exe: pathlib.Path, out: pathlib.Path):
    """The deferred fixes after M6 that need the real app (headless): E2 (a
    missing font family reported once the font is in use, also when a font
    creation failed at the same reload), E4 (C-g anywhere in the input
    that waits during a macro run), its retries R1 (input order), Q1-Q3
    (no killed timer replayed, input after an opened question dropped, no
    drag left behind) and P1 / P2 (non-client mouse input, F10 without a
    menu bar); and the changes of 2026-11-17: B (non-client clicks during a
    run dropped, C-g behind them), the default keys C-h, C-u and C-M-s; and
    those of 2026-11-18: C-u inside isearch and query-replace, a repeated
    isearch over a cluster of several units, the macro keys C-( / C-) /
    C-S-e; and those of 2026-11-19: Emacs's default keys S-<insert> and
    others."""
    work = out / "files_deferred"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    settings.write_bytes(b"{}\n")
    doc = work / "fonts.txt"
    doc.write_bytes(b"font report\n")
    app = d.App(exe, [str(doc)], cwd=work, settings=settings, height=300)
    try:
        app.use_input()
        wait_state(app, lambda s: s.get("text") == "font report\n")
        # The next font creation fails, at the reload that names a missing
        # family; a later rebuild (text-scale) uses that family.
        app.fault("font")
        apply_settings(settings, {"font": {"family": "NoSuchFontDeferredE2"}})
        reloaded = app.state().get("message", "")
        # The next input rebuilds the font (after_input), now without the
        # failure; then a text-scale rebuild stays silent.
        app.keys("C-a")
        time.sleep(0.3)
        next_key = app.state().get("message", "")
        app.keys("C-x C-+")
        time.sleep(0.3)
        scaled = app.state().get("message", "")
        seen = [m for m in (reloaded, next_key, scaled) if "Font not found: NoSuchFontDeferredE2" in m]
        check("[deferred E2] a missing font.family is reported once the font is in use, also when the font "
              "creation failed at the same reload",
              "Cannot create the font NoSuchFontDeferredE2" in reloaded and len(seen) == 1 and
              seen[0].count("Font not found: NoSuchFontDeferredE2") == 1 and "Font not found" not in scaled,
              f"after the reload with the injected font failure: {reloaded!r}; after the next key (C-a): "
              f"{next_key!r}; after C-x C-+: {scaled!r} (HEAD 6064002 never showed 'Font not found' here)")
    finally:
        app.kill()

    # E4: a key typed during a long run and then C-g: the run stops at once,
    # the key before the C-g is dropped with it, a key after it runs. The
    # slow macro (record_slow_macro) is still running 1 s after it starts.
    app = d.App(exe, [str(work / "long_run.txt")], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("message") == "(New file)")
        record_slow_macro(app)
        app.keys(f"{count_keys(SLOW_MACRO_RUNS)} C-x e", pause_s=0.02)
        time.sleep(1.0)
        app.keys("z")  # typed during the run, before the C-g
        app.keys("C-g")
        posted = time.perf_counter()
        app.keys("q")  # after the C-g
        after = wait_state(app, lambda s: "q" in s.get("text", ""), 20.0)
        stop_s = time.perf_counter() - posted
        text = after.get("text", "")
        length = int(after.get("length", "0"))
        check("[deferred E4] during a long macro run, z then C-g then q: the run stops at once, z is dropped "
              "with the C-g, q is typed after the run",
              text.endswith("q") and text.count("q") == 1 and "z" not in text and slow_macro_text(text[:-1]) and
              2 < length - 1 < SLOW_MACRO_FULL and stop_s < 2.0,
              f"q typed {stop_s:.2f} s after the C-g was posted; length {length - 1} before q, the whole run "
              f"gives {SLOW_MACRO_FULL} (so the run was still going when z and C-g came); text ends "
              f"{text[-12:]!r}, 'z' in the text: {'z' in text}, the text before q is the run's: "
              f"{slow_macro_text(text[:-1])} (HEAD 6064002 saw only the first waiting key, z, and ran to the end)")
    finally:
        app.kill()

    # Retry R1 ([C7]): input waiting during a run keeps its order, keys and
    # mouse alike. A click on the first cell of the top row, then Z: Z goes
    # where the click put point (before R1 the held Z overtook the click).
    lines = work / "order.txt"
    lines.write_bytes(b"first line\nsecond line\n")
    app = d.App(exe, [str(lines)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("text", "").startswith("first line"))
        app.keys("M->")
        app.keys("C-x (")
        app.type("abcdefghij")
        app.keys("RET")
        app.keys("C-x )")
        wait_state(app, lambda s: s.get("macro_events") == "11")
        x, y = app.cell_point(0, 0)
        # The input comes 0.3 s into the run. On the development PC about 15%
        # of a run of 100000 was done by then (2026-11-17), so 200000 leaves
        # more than 90% of the run after it.
        runs = 200000
        final = 23 + 11 + runs * 11 + 1  # the file, the recording, the run, the Z
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        time.sleep(0.3)
        app.click(x, y)
        app.keys("Z")
        # Read after the click and Z were posted: still short of the run's
        # end, so they waited during the run.
        during = int(app.state().get("length", "0"))
        done = wait_state(app, lambda s: s.get("length") == str(final), 120.0)
        # Every line after "first line" / "second line" is "abcdefghij\n"
        # (11 units); the top line's start is known without the text (the
        # state holds 64K characters only).
        top = int(done.get("top_line", "-1"))
        z_at = int(done.get("point", "-1")) - 1
        line_start = 23 + 11 * (top - 2) if top >= 2 else -2
        check("[deferred R1] during a long macro run, a click on the first cell of the top row and then Z: Z is "
              "inserted where the click put point",
              done.get("length") == str(final) and during < final - 1 and z_at == line_start,
              f"length {during} just after the click and Z were posted (final {final}); Z at {z_at} (point - 1), "
              f"start of the "
              f"top line {top} at {line_start} (before R1 the held Z ran before the click: Z at the end, "
              f"{final - 2} before the click moved point)")
    finally:
        app.kill()

    # Retry R1 ([C7]): a click, then C-g, then q during a long run: the run
    # stops at once, the click goes with the C-g, q is typed where the run
    # stopped.
    app = d.App(exe, [str(work / "long_run_click.txt")], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("message") == "(New file)")
        record_slow_macro(app)
        x, y = app.cell_point(0, 0)
        app.keys(f"{count_keys(SLOW_MACRO_RUNS)} C-x e", pause_s=0.02)
        time.sleep(1.0)
        app.click(x, y)  # would put point at the start of the text
        app.keys("C-g")
        posted = time.perf_counter()
        app.keys("q")
        after = wait_state(app, lambda s: "q" in s.get("text", ""), 20.0)
        stop_s = time.perf_counter() - posted
        text = after.get("text", "")
        length = int(after.get("length", "0"))
        check("[deferred R1] during a long macro run, a click, C-g and q: the run stops at once, the click is "
              "dropped with the C-g, q is typed after the run where it stopped",
              text.endswith("q") and text.count("q") == 1 and not text.startswith("q") and
              slow_macro_text(text[:-1]) and 2 < length - 1 < SLOW_MACRO_FULL and stop_s < 2.0,
              f"q typed {stop_s:.2f} s after the C-g was posted; length {length - 1} before q, the whole run "
              f"gives {SLOW_MACRO_FULL} (so the run was still going when the click and C-g came); text starts "
              f"{text[:6]!r} ends {text[-6:]!r} (a click applied would have put q at the start)")
    finally:
        app.kill()

    def macro_app(name: str):
        """A file of two lines and a recorded macro "abcdefghij RET" (11 events)."""
        doc = work / f"{name}.txt"
        doc.write_bytes(b"first line\nsecond line\n")
        a = d.App(exe, [str(doc)], cwd=work, settings=settings, height=300)
        wait_state(a, lambda s: s.get("text", "").startswith("first line"))
        a.keys("M->")
        a.keys("C-x (")
        a.type("abcdefghij")
        a.keys("RET")
        a.keys("C-x )")
        wait_state(a, lambda s: s.get("macro_events") == "11")
        return a

    # Retry 2, Q1 ([C12]): a run longer than the 1 s of the prefix key echo
    # (kPendingKeysTimer, set by the C-x of C-x e) must not replay that
    # timer after the run killed it: the next C-x is echoed after about 1 s,
    # not at once.
    app = macro_app("pending_timer")
    try:
        # 12.7 s of run on the development PC with the state read every 50 ms
        # (2026-11-17), well over the 1 s of the timer.
        runs = 200000
        final = 23 + 11 + runs * 11
        start = time.perf_counter()
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        done = wait_state(app, lambda s: s.get("length") == str(final), 180.0)
        run_s = time.perf_counter() - start
        time.sleep(0.5)
        app.keys("C-x", pause_s=0.0)
        time.sleep(0.2)
        st = app.state()
        early = app.capture(shots / "deferred_q1_early.png")
        time.sleep(1.3)
        late = app.capture(shots / "deferred_q1_late.png")
        app.keys("C-g")
        # The prefix key echo is drawn only (not in the state): the echo area
        # (below the mode line) is one colour without it.
        top = int(float(st["mode_line_top"]) + float(st["line_height"]))

        def echo_colours(shot) -> int:
            w, h, px = shot
            return len({px[(y * w + x) * 4:(y * w + x) * 4 + 3] for y in range(top, h) for x in range(w)})

        early_n, late_n = echo_colours(early), echo_colours(late)
        check("[deferred Q1] after a macro run of more than 1 s, the next prefix key C-x is echoed after about "
              "1 s, not at once (no held WM_TIMER of a killed timer is replayed)",
              done.get("length") == str(final) and run_s > 1.0 and early_n == 1 and late_n > 1,
              f"run {run_s:.1f} s; colours in the echo area 0.2 s after C-x {early_n} (1: empty), 1.5 s after "
              f"{late_n} (deferred_q1_early.png / deferred_q1_late.png)")
    finally:
        app.kill()

    # Retry 2, Q2 ([C13]): a right click during a run, then Z. Replaying the
    # click opens the context menu (a question headless); the input held
    # after it is dropped, as keys during a waiting question are.
    app = macro_app("menu_then_key")
    try:
        runs = 200000  # the input comes 0.3 s in, when less than 10% is done (see [deferred R1] above)
        final = 23 + 11 + runs * 11
        x, y = app.cell_point(0, 2)
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        time.sleep(0.3)
        app.right_click(x, y)
        app.keys("Z")
        during = int(app.state().get("length", "0"))  # after both were posted
        question = app.wait_question(120.0)
        answered = app.answer("Cancel") if question else False
        time.sleep(1.0)
        after = app.state()
        check("[deferred Q2] during a long macro run, a right click and then Z: the context menu opens after the "
              "run, and Z (held after the click) is not typed into the buffer after Cancel",
              question is not None and question.get("question") == "context_menu" and answered and
              during < final and after.get("length") == str(final),
              f"length {during} just after the click and Z were posted; question "
              f"{question and question.get('question')}; "
              f"answered Cancel {answered}; length after {after.get('length')} (the run alone gives {final}; "
              f"{final + 1} means Z was typed)")
    finally:
        app.kill()

    # Retry 3, P1 ([C15] 1): the mouse moved over the scroll bar during a run
    # (a non-client WM_NCMOUSEMOVE, HTVSCROLL) and then C-g: the move is
    # input that is held, so the C-g behind it stops the run at once.
    app = macro_app("nc_move_cg")
    try:
        app.keys("M-2 0 0 0 0 0 C-x e", pause_s=0.02)
        time.sleep(0.5)
        app.mouse(0x00A0, 10, 10, 7)  # WM_NCMOUSEMOVE, wParam HTVSCROLL
        app.keys("C-g")
        posted = time.perf_counter()
        stopped = wait_state(app, lambda s: s.get("message") in ("Quit", "(Type e to repeat macro)"), 120.0)
        stop_s = time.perf_counter() - posted
        full = 23 + 11 + 200000 * 11
        check("[deferred P1] during a long macro run, a non-client mouse move over the scroll bar and then C-g: "
              "the run stops at once",
              stopped.get("message") == "Quit" and stop_s < 2.0 and int(stopped.get("length", "0")) < full,
              f"message {stopped.get('message')!r} {stop_s:.2f} s after the C-g was posted, length "
              f"{stopped.get('length')} (the whole run gives {full})")
    finally:
        app.kill()

    # Retry 3, P1 ([C15] 2), changed 2026-11-17: a non-client button message
    # during a run is taken and dropped (replayed later, a button down could
    # start a modal move, size or scroll loop with a stale button state), so
    # the key after it is held and typed after the run; the click has no
    # effect (before 2026-11-17 the button message stayed in the queue with
    # the key after it, and both ran after the run).
    app = macro_app("nc_button_key")
    try:
        runs = 200000  # the input comes 0.3 s in, when less than 10% is done (see [deferred R1] above)
        final = 23 + 11 + runs * 11
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        time.sleep(0.3)
        app.mouse(0x00A1, 10, 10, 0)  # WM_NCLBUTTONDOWN, wParam HTNOWHERE
        app.mouse(0x00A2, 10, 10, 0)  # WM_NCLBUTTONUP
        app.keys("Z")
        during = int(app.state().get("length", "0"))  # after the buttons and Z were posted
        done = wait_state(app, lambda s: s.get("length") == str(final + 1), 120.0)
        check("[deferred P1] during a long macro run, a non-client button press and release (HTNOWHERE: no visible "
              "effect whether it is kept or dropped) and then Z: the run goes to its end and Z is typed after it",
              during < final and done.get("length") == str(final + 1) and int(done.get("point", "0")) == final + 1,
              f"length {during} just after the buttons and Z were posted; length after {done.get('length')} "
              f"(want {final + 1}), "
              f"point {done.get('point')}")
    finally:
        app.kill()

    # 2026-11-17 (B): a non-client button press and release during a run
    # (the scroll bar; then the title bar and the menu bar) are taken and
    # dropped, so a C-g behind them stops the run at once; no scroll, no
    # move or menu loop happens, during the run or after it.
    def nc_click_then_c_g(name: str, places: list):
        """`places`: (label, hit-test value, screen x, screen y) per click,
        computed from the window; returns (ok, evidence)."""
        a = d.App(exe, [str(work / f"{name}.txt")], cwd=work, settings=settings, height=300)
        try:
            wait_state(a, lambda s: s.get("message") == "(New file)")
            record_slow_macro(a)
            before = a.state()
            rect = wt.RECT()
            d.user32.GetWindowRect(a.hwnd, ctypes.byref(rect))
            client = wt.RECT()
            d.user32.GetClientRect(a.hwnd, ctypes.byref(client))
            origin = wt.POINT(0, 0)
            d.user32.ClientToScreen(a.hwnd, ctypes.byref(origin))
            spots = places(rect, client, origin)
            a.keys(f"{count_keys(SLOW_MACRO_RUNS)} C-x e", pause_s=0.02)
            time.sleep(1.0)
            for _label, hit, x, y in spots:
                a.mouse(0x00A1, x, y, hit)  # WM_NCLBUTTONDOWN, wParam = hit-test value
                a.mouse(0x00A2, x, y, hit)  # WM_NCLBUTTONUP
            a.keys("C-g")
            posted = time.perf_counter()
            stopped = wait_state(a, lambda s: s.get("message") in ("Quit", "(Type e to repeat macro)"), 150.0)
            stop_s = time.perf_counter() - posted
            time.sleep(0.5)
            later = a.state()
            after_rect = wt.RECT()
            d.user32.GetWindowRect(a.hwnd, ctypes.byref(after_rect))
            length = int(stopped.get("length", "0"))
            point = int(stopped.get("point", "-1"))
            text = stopped.get("text", "")
            point_line = text.count("\n", 0, point)
            top = int(stopped.get("top_line", "-1"))
            rows = int(float(stopped.get("text_rows", "0")))
            a.keys("C-/")
            undone = wait_state(a, lambda s: s.get("length") == "2", 10.0)
            ok = (stopped.get("message") == "Quit" and stop_s < 2.0 and 2 < length < SLOW_MACRO_FULL and
                  slow_macro_text(text) and point == length and top <= point_line < top + rows and
                  later.get("top_line") == stopped.get("top_line") and later.get("point") == stopped.get("point") and
                  later.get("scroll_bar") == stopped.get("scroll_bar") and
                  later.get("menu_loops") == before.get("menu_loops") and
                  later.get("keymenu_count") == before.get("keymenu_count") and
                  later.get("question", "none") == "none" and
                  (after_rect.left, after_rect.top, after_rect.right, after_rect.bottom) ==
                  (rect.left, rect.top, rect.right, rect.bottom) and undone.get("text") == "a\n")
            evidence = (f"clicks {[(lb, h, x, y) for lb, h, x, y in spots]}; message {stopped.get('message')!r} "
                        f"{stop_s:.2f} s after the C-g was posted; length {length} (the whole run gives "
                        f"{SLOW_MACRO_FULL}); point {point} (= length: no scroll moved it), its line {point_line} "
                        f"on screen (top {top}, {rows} rows) and top / point / scroll bar the same 0.5 s later "
                        f"({later.get('top_line')} / {later.get('point')} / {later.get('scroll_bar')}); menu_loops "
                        f"{before.get('menu_loops')} -> {later.get('menu_loops')}, keymenu requests "
                        f"{before.get('keymenu_count')} -> {later.get('keymenu_count')}; window rect unchanged "
                        f"{(after_rect.left, after_rect.top) == (rect.left, rect.top)}; one C-/ leaves "
                        f"{undone.get('text')!r} (before 2026-11-17 the C-g waited for the end of the run, about "
                        f"85 to 130 s)")
            return ok, evidence
        finally:
            a.kill()

    def scroll_bar_spot(rect, client, origin):
        # Right of the client area, half way down (HTVSCROLL 7).
        return [("scroll bar", 7, origin.x + client.right + 4, origin.y + client.bottom // 2)]

    def title_and_menu_spots(rect, client, origin):
        # The caption (HTCAPTION 2) near the top edge of the window and the
        # menu bar (HTMENU 5) just above the client area.
        return [("title bar", 2, (rect.left + rect.right) // 2, rect.top + 6),
                ("menu bar", 5, origin.x + 20, origin.y - 4)]

    ok, evidence = nc_click_then_c_g("nc_scroll_cg", scroll_bar_spot)
    check("[B] during a long macro run, a click on the scroll bar (WM_NCLBUTTONDOWN / UP, HTVSCROLL) and then C-g: "
          "the click is dropped and the run stops at once; no scroll and no modal loop", ok, evidence)
    ok, evidence = nc_click_then_c_g("nc_title_menu_cg", title_and_menu_spots)
    check("[B] during a long macro run, clicks on the title bar (HTCAPTION) and the menu bar (HTMENU) and then "
          "C-g: the clicks are dropped and the run stops at once; no move and no menu loop", ok, evidence)

    # [C5] (retry 1): WM_COMMAND and WM_CLOSE during a run still end the
    # holding (unlike the non-client buttons above): they stay at the head of
    # the queue, so the C-g behind them waits for the end of the run and the
    # run reaches its full length. A posted WM_DROPFILES is left out: its
    # HDROP is a handle of the process that made it, which another process
    # cannot hand over in a posted message.
    app = macro_app("command_then_cg")
    try:
        runs = 200000  # the input comes 0.3 s in, when less than 10% is done (see [deferred R1] above)
        final = 23 + 11 + runs * 11
        before = app.state()
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        time.sleep(0.3)
        d.note_action()
        d.user32.PostMessageW(app.hwnd, 0x0111, 0xFFFE, 0)  # WM_COMMAND, an id no menu item has
        app.keys("C-g")
        during = int(app.state().get("length", "0"))  # after WM_COMMAND and C-g were posted
        quit_state = wait_state(app, lambda s: s.get("message") == "Quit", 180.0)
        rejected_before = int(before.get("rejected", "0"))
        check("[C5] during a long macro run, WM_COMMAND (an unknown id) and then C-g: the WM_COMMAND ends the "
              "holding, so the run goes to its full length and Quit shows only after it; the WM_COMMAND runs "
              "after the run (rejected as no menu command)",
              during < final and quit_state.get("message") == "Quit" and quit_state.get("length") == str(final) and
              int(quit_state.get("rejected", "0")) == rejected_before + 1 and
              "WM_COMMAND 65534" in quit_state.get("last_rejected", ""),
              f"length {during} just after WM_COMMAND and C-g were posted; at Quit: message "
              f"{quit_state.get('message')!r}, length {quit_state.get('length')} (the whole run gives {final}); "
              f"rejected requests {rejected_before} -> {quit_state.get('rejected')}, last "
              f"{quit_state.get('last_rejected')!r} (taken and dropped, the C-g would stop the run short of "
              f"{final} and nothing would be rejected)")
    finally:
        app.kill()

    app = macro_app("close_then_cg")
    try:
        runs = 200000
        final = 23 + 11 + runs * 11
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        time.sleep(0.3)
        app.close()  # WM_CLOSE: the buffer is modified, so quit asks whether to save
        app.keys("C-g")
        during = int(app.state().get("length", "0"))  # after WM_CLOSE and C-g were posted
        question = app.wait_question(180.0)
        asked = app.state()
        answered = app.answer("Cancel") if question else False
        time.sleep(0.5)
        after = app.state()
        alive = app.proc.poll() is None
        check("[C5] during a long macro run, WM_CLOSE and then C-g: the WM_CLOSE ends the holding, so the run goes "
              "to its full length and the save_changes question comes only after it (Cancel keeps the window); "
              "the C-g behind it never stops the run",
              during < final and question is not None and question.get("question") == "save_changes" and
              asked.get("length") == str(final) and answered and alive and after.get("length") == str(final) and
              after.get("question", "none") == "none",
              f"length {during} just after WM_CLOSE and C-g were posted; question "
              f"{question and question.get('question')} with length {asked.get('length')} (the whole run gives "
              f"{final}); Cancel answered {answered}, window alive {alive}, length after {after.get('length')} "
              f"(taken and dropped, the C-g would stop the run short of {final} and no question would come; "
              f"a WM_DROPFILES is not posted here: an HDROP cannot be handed over from another process)")
    finally:
        app.kill()

    # Retry 3, P2: F10 during a run when the menu bar could not be made
    # ('GFLT' "menu" at the reload that shows it): nothing opens, so the
    # key held after F10 is typed.
    bar_settings = work / "settings_bar.json"
    bar_settings.write_bytes(settings_json({"menu_bar": True}))
    bar_doc = work / "f10_no_bar.txt"
    bar_doc.write_bytes(b"first line\nsecond line\n")
    app = d.App(exe, [str(bar_doc)], cwd=work, settings=bar_settings, height=300)
    try:
        wait_state(app, lambda s: s.get("text", "").startswith("first line"))
        app.keys("M->")
        app.keys("C-x (")
        app.type("abcdefghij")
        app.keys("RET")
        app.keys("C-x )")
        wait_state(app, lambda s: s.get("macro_events") == "11")
        apply_settings(bar_settings, {"menu_bar": False})
        app.fault("menu")
        apply_settings(bar_settings, {"menu_bar": True})
        no_bar = app.state()
        runs = 200000  # the input comes 0.3 s in, when less than 10% is done (see [deferred R1] above)
        final = int(no_bar.get("length", "0")) + runs * 11
        app.keys(f"{count_keys(runs)} C-x e", pause_s=0.02)
        time.sleep(0.3)
        app.keys("<f10>")
        app.keys("Z")
        during = int(app.state().get("length", "0"))  # after F10 and Z were posted
        done = wait_state(app, lambda s: s.get("length") == str(final + 1), 120.0)
        time.sleep(0.3)
        after = app.state()
        check("[deferred P2] with no menu bar (its creation failed), F10 and then Z during a run: nothing opens, "
              "so Z is typed after the run",
              no_bar.get("menu_bar") == "0" and during < final and after.get("length") == str(final + 1) and
              after.get("question", "none") == "none",
              f"menu_bar {no_bar.get('menu_bar')} message {no_bar.get('message')!r}; length {during} just after F10 "
              f"and Z were posted; length after {after.get('length')} (want {final + 1}); message {after.get('message')!r}")
    finally:
        app.kill()

    # Retry 2, Q3: a drag started before a run, its button release and C-g
    # posted during the run: the release goes with the C-g, and the drag
    # ends too (no mouse state is left behind).
    app = macro_app("drag_cg")
    try:
        st = app.state()
        x0, y0 = app.cell_point(0, 1, st)
        x1, y1 = app.cell_point(1, 4, st)
        app.mouse(d.WM_LBUTTONDOWN, x0, y0, d.MK_LBUTTON)
        app.mouse(0x0200, x1, y1, d.MK_LBUTTON)  # WM_MOUSEMOVE
        pressed = wait_state(app, lambda s: s.get("dragging") == "1")
        # 1000000 runs of 11 events; the release and C-g come 1 s in, when
        # about 2% of it was done on the development PC (2026-11-17).
        full = 23 + 11 + 1000000 * 11
        app.keys("M-1 0 0 0 0 0 0 C-x e", pause_s=0.02)
        time.sleep(1.0)
        app.mouse(d.WM_LBUTTONUP, x1, y1, 0)
        app.keys("C-g")
        stopped = wait_state(app, lambda s: s.get("message") == "Quit", 20.0)
        time.sleep(0.3)
        after = app.state()
        check("[deferred Q3] a drag started before a run, its release and C-g during the run: the run stops and "
              "the drag ends (dragging 0)",
              pressed.get("dragging") == "1" and stopped.get("message") == "Quit" and after.get("dragging") == "0" and
              int(stopped.get("length", "0")) < full,
              f"dragging before the run {pressed.get('dragging')}; message {stopped.get('message')!r}, length "
              f"{stopped.get('length')} (the whole run gives {full}, so it was still going); dragging after the "
              f"stop {after.get('dragging')} (mouse capture is not in the state)")
    finally:
        app.kill()

    # 2026-11-17 (C, D): the default keys C-h (as DEL), C-u
    # (backward-kill-line) and C-M-s (isearch-forward-regexp), posted as the
    # headless keys are (WM_KEYDOWN with the modifier bits in lParam).
    keys_doc = work / "keys_c_h_c_u.txt"
    keys_doc.write_bytes(b"")
    app = d.App(exe, [str(keys_doc)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("length") == "0")
        app.type("abc")
        wait_state(app, lambda s: s.get("text") == "abc")
        app.keys("C-h")
        after_c_h = wait_state(app, lambda s: s.get("text") == "ab")
        check("[C-h] type abc, then C-h: one character is deleted (text ab)",
              after_c_h.get("text") == "ab" and after_c_h.get("point") == "2",
              f"text {after_c_h.get('text')!r} point {after_c_h.get('point')} (two deletions, key down and a "
              f"WM_CHAR, would leave 'a')")
        app.keys("C-/")
        app.keys("C-a C-k")  # an empty line again (the kill is replaced by the next one)
        app.type("abc def")
        wait_state(app, lambda s: s.get("text") == "abc def")
        app.keys("C-u")
        after_c_u = wait_state(app, lambda s: s.get("length") == "0")
        clip = app.clipboard()
        check("[C-u] abc def, then C-u at its end: the line is empty and the private clipboard holds abc def",
              after_c_u.get("text") == "" and clip == "abc def",
              f"text {after_c_u.get('text')!r}; clipboard {clip!r}")
        app.type("x12 y345\n")
        app.keys("M-<")
        app.keys("C-M-s")
        prompt = wait_state(app, lambda s: s.get("isearch") == "1")
        app.type("[0-9]+")
        found = wait_state(app, lambda s: s.get("point") == "3")
        app.keys("RET")
        check("[C-M-s] C-M-s starts a regexp isearch: [0-9]+ moves to the end of 12",
              prompt.get("echo", "").startswith("Regexp I-search: ") and found.get("point") == "3" and
              found.get("isearch_pattern") == "[0-9]+",
              f"echo after C-M-s {prompt.get('echo')!r}; point {found.get('point')} pattern "
              f"{found.get('isearch_pattern')!r}")
    finally:
        app.kill()

    # 2026-11-18 (decisions 5 b and 4 a): C-u inside isearch empties the
    # search string and isearch goes on; C-u inside query-replace does
    # nothing; a repeated isearch moves the cursor on every press over a
    # cluster of several units (a ZWJ family).
    cu_text = "ab foo bar\nfoo a a\n"
    cu_doc = work / "keys_c_u_modes.txt"
    cu_doc.write_bytes(cu_text.encode("utf-8"))
    app = d.App(exe, [str(cu_doc)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("text") == cu_text)
        app.set_clipboard("keep")
        app.keys("C-s")
        app.type("foo")
        searching = wait_state(app, lambda s: s.get("isearch_pattern") == "foo" and s.get("point") == "6")
        app.keys("C-u")
        emptied = wait_state(app, lambda s: s.get("isearch_pattern", "x") == "" or s.get("isearch") != "1")
        clip = app.clipboard()
        check("[C-u isearch] C-s foo, then C-u: isearch goes on with an empty search string, the text and the "
              "private clipboard are unchanged",
              searching.get("point") == "6" and emptied.get("isearch") == "1" and
              emptied.get("isearch_pattern") == "" and emptied.get("text") == cu_text and clip == "keep" and
              emptied.get("point") == "0",
              f"before C-u: point {searching.get('point')} pattern {searching.get('isearch_pattern')!r}; after: "
              f"isearch {emptied.get('isearch')} pattern {emptied.get('isearch_pattern')!r} point "
              f"{emptied.get('point')} echo {emptied.get('echo')!r} text changed {emptied.get('text') != cu_text}; "
              f"clipboard {clip!r}")
        app.keys("C-g")
        wait_state(app, lambda s: s.get("isearch") == "0")
        app.keys("M-%")
        app.type("a")
        app.keys("RET")
        app.type("b")
        app.keys("RET")
        asking = wait_state(app, lambda s: s.get("query_replace") == "1")
        app.keys("C-u")
        time.sleep(0.3)
        after_c_u = app.state()
        clip = app.clipboard()
        check("[C-u query-replace] M-% a RET b RET, then C-u: the text, the point and the private clipboard are "
              "unchanged and query-replace goes on",
              asking.get("query_replace") == "1" and after_c_u.get("query_replace") == "1" and
              after_c_u.get("text") == cu_text and after_c_u.get("point") == asking.get("point") and clip == "keep",
              f"query_replace before {asking.get('query_replace')} after {after_c_u.get('query_replace')}; point "
              f"{asking.get('point')} -> {after_c_u.get('point')}; text changed {after_c_u.get('text') != cu_text}; "
              f"clipboard {clip!r}; echo {after_c_u.get('echo')!r}")
        app.keys("q")
    finally:
        app.kill()

    family = "a\U0001F468‍\U0001F469‍\U0001F467b"  # 1 + 8 + 1 UTF-16 units
    zwj_doc = work / "repeat_clusters.txt"
    zwj_doc.write_bytes(family.encode("utf-8"))
    app = d.App(exe, [str(zwj_doc)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("text") == family)
        app.keys("C-M-s")
        app.type(".")
        first = wait_state(app, lambda s: s.get("isearch_pattern") == "." and s.get("point") == "1")
        points = [first.get("point")]
        echoes = []
        for _ in range(4):
            app.keys("C-s")
            time.sleep(0.2)
            state = app.state()
            points.append(state.get("point"))
            echoes.append(state.get("echo", ""))
            if state.get("echo", "").startswith("Failing"):
                break
        moved = all(points[i] != points[i - 1] for i in range(1, len(points) - 1))
        failed = bool(echoes) and echoes[-1].startswith("Failing") and points[-1] == points[-2]
        check("[repeat clusters] a, a ZWJ family, b: C-M-s . then C-s: the point changes on every press until "
              "Failing",
              moved and failed and points[:3] == ["1", "9", "10"],
              f"points {points} (UTF-16 units; the family is 8 units, 1 to 9); echoes {echoes}")
        app.keys("RET")
    finally:
        app.kill()

    # 2026-11-18: the one-key defaults of the macro commands. C-( and C-)
    # are sent as Ctrl + the key that has "(" / ")" in this layout (with
    # Shift when the layout needs it), C-S-e as Ctrl + Shift + E.
    macro_doc = work / "macro_keys.txt"
    macro_doc.write_bytes(b"")
    app = d.App(exe, [str(macro_doc)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("length") == "0")
        app.keys("C-(")
        recording = wait_state(app, lambda s: s.get("macro_recording") == "1")
        app.type("ab")
        wait_state(app, lambda s: s.get("text") == "ab")
        app.keys("C-)")
        defined = wait_state(app, lambda s: s.get("macro_recording") == "0" and s.get("macro_events") == "2")
        app.keys("C-S-e")
        ran = wait_state(app, lambda s: s.get("text") == "abab")
        app.keys("C-S-e")
        again = wait_state(app, lambda s: s.get("text") == "ababab")
        check("[macro keys] C-( ab C-) records two events (C-) is not one of them); C-S-e runs the macro, and "
              "once more when pressed again",
              recording.get("macro_recording") == "1" and defined.get("macro_recording") == "0" and
              defined.get("macro_events") == "2" and ran.get("text") == "abab" and again.get("text") == "ababab",
              f"after C-(: recording {recording.get('macro_recording')} message {recording.get('message')!r}; "
              f"after C-): recording {defined.get('macro_recording')} events {defined.get('macro_events')} message "
              f"{defined.get('message')!r}; after C-S-e: text {ran.get('text')!r} message {ran.get('message')!r}; "
              f"after the second C-S-e: text {again.get('text')!r}")
    finally:
        app.kill()

    # 2026-11-19: Emacs's default keys S-<insert> / C-<insert> / S-<delete>
    # / C-<delete> / C-DEL / M-<right> / C-@ / C-<digit> / C-M-<digit>,
    # through the real key path (key down with the lParam modifier bits).
    emacs_doc = work / "emacs_keys.txt"
    emacs_doc.write_bytes(b"one two three")
    app = d.App(exe, [str(emacs_doc)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("length") == "13")
        steps = []

        def step(keys: str, ok) -> dict:
            app.keys(keys)
            state = wait_state(app, ok)
            steps.append(f"{keys}: text {state.get('text')!r} point {state.get('point')} clipboard "
                         f"{state.get('clipboard_text')!r}")
            return state

        word = step("M-<right>", lambda s: s.get("point") == "3")
        step("C-@", lambda s: s.get("message") == "Mark set")
        step("C-e", lambda s: s.get("point") == "13")
        copied = step("C-<insert>", lambda s: s.get("clipboard_text") == " two three")
        app.set_clipboard("XY")
        step("C-<home>", lambda s: s.get("point") == "0")
        pasted = step("S-<insert>", lambda s: s.get("text") == "XYone two three")
        step("C-<end>", lambda s: s.get("point") == "15")
        back = step("C-DEL", lambda s: s.get("text") == "XYone two ")
        step("C-<home>", lambda s: s.get("point") == "0")
        forward = step("C-<delete>", lambda s: s.get("text") == " two ")
        step("C-@", lambda s: s.get("message") == "Mark set")
        step("C-e", lambda s: s.get("point") == "5")
        cut = step("S-<delete>", lambda s: s.get("text") == "")
        step("C-3", lambda s: True)
        step("x", lambda s: s.get("text") == "xxx")
        step("C-M-2", lambda s: True)
        digits = step("y", lambda s: s.get("text") == "xxxyy")
        check("[emacs keys] M-<right>, C-@, C-<insert>, S-<insert>, C-DEL, C-<delete>, S-<delete>, C-3 and C-M-2 "
              "do what M-f, C-SPC, M-w, C-y, M-DEL, M-d, C-w, M-3 and M-2 do",
              word.get("point") == "3" and copied.get("clipboard_text") == " two three" and
              pasted.get("text") == "XYone two three" and back.get("text") == "XYone two " and
              forward.get("text") == " two " and cut.get("text") == "" and
              cut.get("clipboard_text") == " two " and digits.get("text") == "xxxyy",
              "; ".join(steps))
    finally:
        app.kill()


def run_m6_checks(exe: pathlib.Path, out: pathlib.Path):
    """[C1]-[C3], [C5] and [C6] (B2, B3, B4 / B5, B6) of the M6 packet on the
    real app (headless)."""
    work = out / "files_m6"
    work.mkdir(parents=True, exist_ok=True)
    shots = out / "shots"
    settings = work / "settings.json"
    settings.write_bytes(settings_json({"line_numbers": False}))
    names = []

    # [C1] kills in a row; [C2] shift selection; [C3] / [C5] a macro.
    text = work / "m6_edit.txt"
    text.write_bytes(b"ab\ncd\nef\n")
    app = d.App(exe, [str(text)], cwd=work, settings=settings, height=400)
    try:
        wait_state(app, lambda s: s.get("text") == "ab\ncd\nef\n")
        app.keys("C-k C-k C-k")
        state = wait_state(app, lambda s: s.get("text") == "\nef\n")
        clip = app.clipboard()
        app.keys("C-n")
        app.keys("C-k")
        moved = wait_state(app, lambda s: s.get("text") == "\n\n")
        clip2 = app.clipboard()
        check("[M6 C1] C-k C-k C-k puts \"ab\", its line end and \"cd\" in the private clipboard as one text; "
              "a kill after a move (C-n) replaces it",
              state.get("text") == "\nef\n" and clip == "ab\r\ncd" and moved.get("text") == "\n\n" and clip2 == "ef",
              f"text={state.get('text')!r} clipboard after 3 kills={clip!r}; after C-n C-k text="
              f"{moved.get('text')!r} clipboard={clip2!r}")
        app.keys("C-/ C-/ C-/ C-/")  # undo the four kills (one each)
        wait_state(app, lambda s: s.get("text") == "ab\ncd\nef\n")
        app.keys("M-<")
        app.keys("S-<right> S-<right>")
        selected = wait_state(app, lambda s: s.get("point") == "2")
        time.sleep(0.3)
        app.capture(shots / "m6_01_shift_select.png")
        names.append("m6_01_shift_select.png")
        app.keys("<right>")
        unselected = wait_state(app, lambda s: s.get("point") == "3")
        check("[M6 C2] S-<right> twice selects two characters from a temporary mark; <right> removes it",
              selected.get("mark_active") == "1" and selected.get("mark") == "0" and
              unselected.get("mark_active") == "0",
              f"after S-<right> x2: point={selected.get('point')} mark={selected.get('mark')} "
              f"active={selected.get('mark_active')}; after <right>: active={unselected.get('mark_active')} "
              "(m6_01_shift_select.png: 'ab' on the selection colour)")

        app.keys("M-<")
        app.keys("C-x (")
        recording = wait_state(app, lambda s: s.get("macro_recording") == "1")
        app.type("x")
        size = app.state()
        click = app.cell_point(1, 0, size)
        app.click(*click)
        app.type("y")
        time.sleep(0.3)
        app.capture(shots / "m6_02_macro_recording.png")
        names.append("m6_02_macro_recording.png")
        app.keys("C-x )")
        defined = wait_state(app, lambda s: s.get("macro_recording") == "0")
        app.keys("M-<")
        app.keys("C-x e")
        ran = wait_state(app, lambda s: s.get("text", "").startswith("xy"))
        app.type("e")
        repeated = wait_state(app, lambda s: s.get("text", "").startswith("xyxy"))
        time.sleep(0.3)
        app.capture(shots / "m6_03_macro_run.png")
        names.append("m6_03_macro_run.png")
        app.keys("C-/")
        undone = wait_state(app, lambda s: s.get("text") == repeated.get("text", "")[2:])
        check("[M6 C3] C-x ( x <click> y C-x ): Def in the mode line while recording, 2 events (the click is not "
              "recorded); C-x e at the start types xy, e again, one C-/ undoes one run",
              recording.get("mode_line", "").endswith("  Def") and defined.get("macro_events") == "2" and
              not defined.get("mode_line", "").endswith("  Def") and ran.get("text", "").startswith("xy") and
              repeated.get("text", "").startswith("xyxy") and undone.get("text") == repeated.get("text", "")[2:],
              f"mode line while recording={recording.get('mode_line')!r}; events={defined.get('macro_events')} "
              f"message={defined.get('message')!r}; after C-x e text={ran.get('text')!r} message="
              f"{ran.get('message')!r}; after e text={repeated.get('text')!r}; after C-/ text={undone.get('text')!r} "
              "(m6_02_macro_recording.png: Def; m6_03_macro_run.png)")
    finally:
        app.kill()

    # Retry R1: kills by a macro run reach the private clipboard as one text.
    # Retry R2: C-g posted during a long run stops it.
    lines = work / "m6_kill_lines.txt"
    lines.write_bytes(b"".join(f"k{i}\n".encode() for i in range(20)))
    app = d.App(exe, [str(lines)], cwd=work, settings=settings, height=400)
    try:
        wait_state(app, lambda s: s.get("text", "").startswith("k0\n"))
        app.keys("C-x ( C-k C-k C-x )")
        app.keys("M-4 C-x e")
        state = wait_state(app, lambda s: s.get("text", "").startswith("k5\n"))
        clip = app.clipboard()
        check("[M6 R1] C-x ( C-k C-k C-x ), M-4 C-x e: the private clipboard holds the 8 kills of the run "
              "as one text",
              clip == "k1\r\nk2\r\nk3\r\nk4\r\n" and state.get("text", "").startswith("k5\n"),
              f"clipboard={clip!r} text starts {state.get('text', '')[:6]!r}")
    finally:
        app.kill()
    app = d.App(exe, [str(work / "m6_long_run.txt")], cwd=work, settings=settings, height=400)
    try:
        wait_state(app, lambda s: s.get("message") == "(New file)")
        record_slow_macro(app)  # still running 1 s after it starts (see record_slow_macro)
        start = time.perf_counter()
        app.keys(f"{count_keys(SLOW_MACRO_RUNS)} C-x e", pause_s=0.02)
        time.sleep(1.0)
        app.keys("C-g")
        posted = time.perf_counter()
        stopped = wait_state(app, lambda s: s.get("message") == "Quit", 20.0)
        stop_s = time.perf_counter() - posted
        length = int(stopped.get("length", "0"))
        app.keys("C-/")
        undone = wait_state(app, lambda s: s.get("length") == "2", 10.0)
        check(f"[M6 R2] a posted C-g stops a long macro run ({SLOW_MACRO_RUNS} runs of {SLOW_MACRO_EVENTS} events) "
              "at once with Quit; one C-/ takes the whole stopped run back",
              stopped.get("message") == "Quit" and 2 < length < SLOW_MACRO_FULL and stop_s < 2.0 and
              undone.get("text") == "a\n" and stopped.get("macro_recording") == "0",
              f"stopped {stop_s:.2f} s after the C-g ({time.perf_counter() - start:.1f} s in all); length when "
              f"stopped={length}, the whole run gives {SLOW_MACRO_FULL} (so it was still going when C-g came); "
              f"message={stopped.get('message')!r}; text after C-/={undone.get('text')!r} (want 'a\\n', the text "
              f"before the run)")
    finally:
        app.kill()

    # B2: --encoding without a file names the encoding of the new buffer.
    app = d.App(exe, ["--encoding=shift_jis"], cwd=work, settings=settings, height=300)
    try:
        state = wait_state(app, lambda s: s.get("encoding") == "shift_jis")
        time.sleep(0.3)
        app.capture(shots / "m6_04_encoding_without_file.png")
        names.append("m6_04_encoding_without_file.png")
        app.type("日本")
        app.keys("C-x C-w")
        wait_state(app, lambda s: s.get("minibuffer") == "1")
        app.type("m6_new.txt")
        app.keys("RET")
        saved = wait_state(app, lambda s: s.get("modified") == "0" and s.get("path", "").endswith("m6_new.txt"))
        written = (work / "m6_new.txt").read_bytes() if (work / "m6_new.txt").exists() else None
        check("[M6 C6 B2] --encoding=shift_jis without a file: the new buffer shows Shift_JIS and saves in it",
              state.get("encoding") == "shift_jis" and "  Shift_JIS " in state.get("mode_line", "") and
              state.get("modified") == "0" and written == "日本".encode("cp932"),
              f"encoding={state.get('encoding')} mode_line={state.get('mode_line')!r} message="
              f"{state.get('message')!r}; saved bytes={written!r} message={saved.get('message')!r} "
              "(m6_04_encoding_without_file.png)")
    finally:
        app.kill()
    app = d.App(exe, ["--encoding=big5"], cwd=work, settings=settings, height=300)
    try:
        state = wait_state(app, lambda s: "Unknown encoding" in s.get("message", ""))
        check("[M6 C6 B2] --encoding=big5 without a file: Unknown encoding in the echo area, UTF-8 kept",
              state.get("message", "").startswith("Unknown encoding: big5") and state.get("encoding") == "utf-8",
              f"message={state.get('message')!r} encoding={state.get('encoding')}")
    finally:
        app.kill()

    # B3: revert-buffer-with-coding-system keeps read-only-mode.
    plain = work / "m6_plain.txt"
    plain.write_bytes(b"plain\n")
    app = d.App(exe, [str(plain)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("text") == "plain\n")
        app.keys("C-x C-q")
        wait_state(app, lambda s: s.get("read_only") == "1")
        app.keys("C-x RET r")
        wait_state(app, lambda s: s.get("minibuffer") == "1")
        app.type("shift_jis")
        app.keys("RET")
        state = wait_state(app, lambda s: s.get("encoding") == "shift_jis")
        check("[M6 C6 B3] C-x C-q, then C-x RET r shift_jis: read again in Shift_JIS and still read-only (%%)",
              state.get("encoding") == "shift_jis" and state.get("read_only") == "1" and
              state.get("mode_line", "").startswith("-%%"),
              f"encoding={state.get('encoding')} read_only={state.get('read_only')} "
              f"mode_line={state.get('mode_line')!r} message={state.get('message')!r}")
    finally:
        app.kill()

    # B4 / B5: a "!" Shift_JIS buffer: No writes nothing; after the U+FFFD is
    # deleted, Yes saves and ends the "!".
    bad = work / "m6_bad_sjis.txt"
    bad.write_bytes(M6_BAD_SJIS)
    app = d.App(exe, ["--encoding=shift_jis", str(bad)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("encoding_text") == "Shift_JIS!")
        mtime = bad.stat().st_mtime_ns
        app.keys("M-< C-n C-d")
        edited = wait_state(app, lambda s: s.get("text") == "a\n@b")
        app.keys("C-x C-s")
        question_no = answer(app, "No", timeout_s=3.0)
        refused = wait_state(app, lambda s: s.get("message") == "Not saved")
        unchanged = bad.read_bytes() == M6_BAD_SJIS and bad.stat().st_mtime_ns == mtime
        app.keys("C-x C-s")
        question = app.wait_question(3.0)
        time.sleep(0.3)
        app.capture(shots / "m6_05_save_question.png")
        names.append("m6_05_save_question.png")
        accepted = app.answer("Yes")
        saved = wait_state(app, lambda s: s.get("modified") == "0")
        check("[M6 C6 B4] a Shift_JIS! buffer without its U+FFFD: the save asks (naming Shift_JIS, the encoding it "
              "was read with); No writes nothing; Yes (GANS) writes the Shift_JIS bytes and ends the '!'",
              edited.get("text") == "a\n@b" and question_no is not None and refused.get("message") == "Not saved"
              and unchanged and question is not None and question.get("question") == "yes_no" and
              question.get("instruction", "").startswith("m6_bad_sjis.txt was read as Shift_JIS with bytes that "
                                                         "are not valid Shift_JIS (first at byte offset 3, line 2;")
              and accepted and bad.read_bytes() == b"a\r\n@b" and saved.get("encoding_text") == "Shift_JIS",
              f"No: {describe(question_no)} message={refused.get('message')!r} file unchanged={unchanged}; "
              f"Yes: {describe(question)} accepted={accepted} bytes={bad.read_bytes()!r} "
              f"encoding_text={saved.get('encoding_text')!r} message={saved.get('message')!r} "
              "(m6_05_save_question.png)")
    finally:
        app.kill()
    bad_utf8 = work / "m6_bad_utf8.txt"
    bad_utf8.write_bytes(b"a\xff\n")
    app = d.App(exe, ["--encoding=utf-8", str(bad_utf8)], cwd=work, settings=settings, height=300)
    try:
        wait_state(app, lambda s: s.get("encoding_text") == "UTF-8!")
        app.keys("C-x RET f")
        wait_state(app, lambda s: s.get("minibuffer") == "1")
        app.type("shift_jis")
        app.keys("RET")
        wait_state(app, lambda s: s.get("encoding_text") == "Shift_JIS!")
        app.keys("C-x C-s")
        question = answer(app, "Yes", timeout_s=3.0)
        stopped = wait_state(app, lambda s: s.get("message", "").startswith("Cannot save as"))
        check("[M6 C6 B5] a UTF-8! buffer switched to Shift_JIS: the question names UTF-8 (read) and Shift_JIS "
              "(save); after Yes the U+FFFD stops the save, the file unchanged",
              question is not None and question.get("instruction", "").startswith(
                  "m6_bad_utf8.txt was read as UTF-8 with bytes that are not valid UTF-8") and
              question.get("instruction", "").endswith("Save as Shift_JIS anyway?") and
              stopped.get("message", "").startswith("Cannot save as Shift_JIS: U+FFFD at line 1, column 1") and
              bad_utf8.read_bytes() == b"a\xff\n",
              f"{describe(question)} message={stopped.get('message')!r} bytes={bad_utf8.read_bytes()!r}")
    finally:
        app.kill()

    # B6: the scroll bar of a 10MB single line (wrap window).
    big = m6_single_line(out.parent.parent / "m6_scroll")
    wrap_settings = work / "settings_wrap.json"
    wrap_settings.write_bytes(settings_json({"line_numbers": False, "wrap": {"mode": "window"}}))
    app = d.App(exe, [str(big)], cwd=work, settings=wrap_settings, height=400)
    try:
        start = wait_state(app, lambda s: s.get("scroll_bar", "none") != "none", 20.0)
        si0 = d.scroll_info(app.hwnd)
        app.keys("<end>")
        ended = wait_state(app, lambda s: s.get("top_row", "0") != "0", 20.0)
        time.sleep(0.3)
        si1 = d.scroll_info(app.hwnd)
        app.capture(shots / "m6_06_scroll_end.png")
        names.append("m6_06_scroll_end.png")
        target = si1.nMax // 2
        d.user32.SendMessageW(app.hwnd, d.WM_VSCROLL, (target << 16) | 5, 0)  # SB_THUMBTRACK
        tracked = wait_state(app, lambda s: s.get("top_row") != ended.get("top_row"), 5.0)
        si2 = d.scroll_info(app.hwnd)
        time.sleep(0.3)
        app.capture(shots / "m6_07_scroll_thumb.png")
        names.append("m6_07_scroll_thumb.png")
        top_end, top_mid = int(ended.get("top_row", "0")), int(tracked.get("top_row", "0"))
        check("[M6 C6 B6] 10MB single line, wrap window: the bar has a range (by offset), its thumb moves to the "
              "end after <end>, and a posted SB_THUMBTRACK to the middle moves the top row there",
              start.get("scroll_bar_unit") == "offset" and si0.nMax > 0 and si0.nPage > 0 and si0.nPos == 0 and
              si1.nPos > si1.nMax // 2 and si1.nPos + si1.nPage >= si1.nMax - 1 and
              0 < top_mid < top_end and abs(si2.nPos - target) <= max(2, si2.nPage),
              f"start: nMax={si0.nMax} nPage={si0.nPage} nPos={si0.nPos} unit={start.get('scroll_bar_unit')}; "
              f"after <end>: nPos={si1.nPos} top_row={top_end}; SB_THUMBTRACK {target}: nPos={si2.nPos} "
              f"top_row={top_mid} (m6_06_scroll_end.png, m6_07_scroll_thumb.png)")
    finally:
        app.kill()
    many = work / "m6_many_lines.txt"
    many.write_bytes(b"line\n" * 300)
    app = d.App(exe, [str(many)], cwd=work, settings=wrap_settings, height=400)
    try:
        state = wait_state(app, lambda s: s.get("scroll_bar", "none") != "none")
        si = d.scroll_info(app.hwnd)
        check("[M6 C6 B6] a file of many lines keeps the bar by logical line (negative)",
              state.get("scroll_bar_unit") == "line" and si.nMax == 300,
              f"unit={state.get('scroll_bar_unit')} nMax={si.nMax} nPage={si.nPage}")
    finally:
        app.kill()
    record("INFO", "[M6] screenshots", ", ".join(names))


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
    parser.add_argument("--only-m3d", action="store_true",
                        help="run only the M3d colour emoji, redraw benchmark and F10 wrap-around checks")
    parser.add_argument("--only-m4a", action="store_true",
                        help="run only the M4a line end, save and read-only checks")
    parser.add_argument("--only-m4b", action="store_true",
                        help="run only the M4b encoding checks")
    parser.add_argument("--only-m5", action="store_true",
                        help="run only the M5 search, replace and goto-line checks")
    parser.add_argument("--only-m6", action="store_true",
                        help="run only the M6 checks (kills, shift selection, macros, B2-B6)")
    parser.add_argument("--only-deferred", action="store_true",
                        help="run only the checks of the deferred fixes after M6 (E2, E4; headless only)")
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
            args.only_m3d or args.only_m4a or args.only_m4b or args.only_m5 or args.only_m6 or
            args.only_deferred or args.only_headless or args.only_visible)
    if args.only_m5 and args.visible:
        parser.error("--only-m5 runs headless only")
    if args.only_m6 and args.visible:
        parser.error("--only-m6 runs headless only")
    if args.only_deferred and args.visible:
        parser.error("--only-deferred runs headless only")
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
        if not only or args.only_m3d:
            if d.HEADLESS:
                run_m3d_checks(exe, out)
            else:
                run_m3d_visible_checks(exe, out)
        if not only or args.only_m4a:
            run_m4a_checks(exe, out)
        if not only or args.only_m4b:
            run_m4b_checks(exe, out)
        if d.HEADLESS and (not only or args.only_m5):
            run_m5_checks(exe, out)
        if d.HEADLESS and (not only or args.only_m6):
            run_m6_checks(exe, out)
        if d.HEADLESS and (not only or args.only_deferred):
            run_deferred_checks(exe, out)
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
        # owner is unknown while a geditor of the run was open, right after
        # input that the run sent to a geditor: T3).
        verdict, why = d.clipboard_verdict(sampler.clipboard_changes, os.path.normcase(str(exe)), d.ACTIONS)
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
