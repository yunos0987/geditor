"""Measure keystroke-to-frame latency of geditor on large files (M1-5).

Generates (once, under --data-dir) a 100 MB UTF-8 file of about 80
characters per line (ASCII mixed with Japanese) and a 10 MB file holding a
single line. For each file it starts `geditor.exe --measure-input
--settings=<data-dir>/settings.json <file>` (a "{}" settings file of its own,
so the user's .geditor.json in %USERPROFILE% is never read), which prints to
stderr:

  hwnd=0x... open_ms=...   after the first frame (open_ms: request_open time)
  input_ms=...             for each keyboard message: message received ->
                           EndDraw of the frame that shows it

and posts WM_CHAR messages to the window, the same path as typing, one at a
time (the next one after the previous frame). For the single-line file the
cursor is first moved to the end of the line with <end> (WM_KEYDOWN VK_END).

Reports the median and the 95th percentile. Exit code: 0 when the median of
the 100 MB file is within --limit-ms (default 16), 1 otherwise, 2 on errors.
The 10 MB single-line numbers are reported without a limit; with --wrap
window, end_key_ms is the time to scroll from the start to the end of that
line.

--wrap MODE (none / window / fixed / max) writes {"wrap": {"mode": MODE}}
to the settings file; without it the settings file is "{}" (the defaults:
line numbers on, wrap max).

geditor also gets --headless (its window stays off the screen and out of
the foreground; it draws the same frames) unless --visible is given. A build
without --headless must be measured with --visible.

--m4-5 (M4-5, the 100 MB loading of M4b) measures instead: the 100 MB file
above, a 100 MB file holding a single line (single_line_100mb.txt, the
cursor moved to its end with <end> and characters typed there), and a 100 MB
file whose lines end in LF and CRLF by turns (lines_100mb_lf_crlf.txt,
opened only). Limits: open_ms 1000 for the first two and 2000 for the third,
and the median of the typed characters at the end of the single line 16 ms
(one screen redrawn per character; use --wrap window for M4-5). The exit
code is 1 when any is over.
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import math
import pathlib
import random
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True  # no __pycache__ in the work tree

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
VK_END = 0x23

MB = 1024 * 1024
WORDS = ["editor", "piece", "tree", "buffer", "line", "cursor", "undo", "redo",
         "日本語", "テキスト", "編集", "ひらがな"]
TYPED = "the quick brown fox jumps over the lazy dog 日本語の入力 0123456789"


def generate_lines(path: pathlib.Path, target_bytes: int):
    rng = random.Random(1)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        written = 0
        while written < target_bytes:
            parts = []
            chars = 0
            while chars < 80:
                w = rng.choice(WORDS)
                parts.append(w)
                chars += len(w) + 1
            line = " ".join(parts) + "\n"
            f.write(line)
            written += len(line.encode("utf-8"))


def generate_single_line(path: pathlib.Path, target_bytes: int):
    rng = random.Random(2)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        written = 0
        while written < target_bytes:
            w = rng.choice(WORDS) + " "
            f.write(w)
            written += len(w.encode("utf-8"))


def generate_lf_crlf(path: pathlib.Path, target_bytes: int):
    """Lines of about 80 characters ending in LF and CRLF by turns."""
    rng = random.Random(3)
    with path.open("wb") as f:
        written = 0
        index = 0
        while written < target_bytes:
            parts = []
            chars = 0
            while chars < 80:
                w = rng.choice(WORDS)
                parts.append(w)
                chars += len(w) + 1
            line = " ".join(parts).encode("utf-8") + (b"\n" if index % 2 == 0 else b"\r\n")
            f.write(line)
            written += len(line)
            index += 1


def ensure_file(path: pathlib.Path, size: int, generator):
    if path.is_file() and path.stat().st_size >= size:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"generating {path} ...", flush=True)
    generator(path, size)


def read_value(proc: subprocess.Popen, key: str) -> float:
    while True:
        raw = proc.stderr.readline()
        if not raw:
            raise RuntimeError(f"geditor exited before printing {key}")
        line = raw.decode("utf-8", errors="replace").strip()
        for field in line.split():
            if field.startswith(key + "="):
                return float(field.split("=", 1)[1])


def percentile(values: list[float], p: float) -> float:
    ordered = sorted(values)
    rank = max(1, math.ceil(p / 100.0 * len(ordered)))
    return ordered[rank - 1]


def run(exe: pathlib.Path, file: pathlib.Path, extra_args: list[str], chars: int,
        press_end: bool) -> dict:
    proc = subprocess.Popen([str(exe), "--measure-input", *extra_args, str(file)],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    try:
        first = b""
        confirmed = False
        while b"hwnd=" not in first:
            first = proc.stderr.readline()
            if not first:
                raise RuntimeError("geditor exited before its first frame")
            confirmed = confirmed or first.strip() == b"headless=1"
        if "--headless" in extra_args and not confirmed:
            # A build without --headless shows its window instead.
            raise RuntimeError("this build did not confirm --headless (no 'headless=1'); measure it with --visible")
        fields = dict(f.split("=", 1) for f in first.decode().split())
        hwnd = int(fields["hwnd"], 16)
        open_ms = float(fields["open_ms"])
        end_ms = None
        if press_end:
            user32.PostMessageW(hwnd, WM_KEYDOWN, VK_END, 1 | (0x4F << 16) | (1 << 24))
            end_ms = read_value(proc, "input_ms")
            user32.PostMessageW(hwnd, WM_KEYUP, VK_END, 1 | (0x4F << 16) | (1 << 24) | (3 << 30))
        samples = []
        for i in range(chars):
            unit = TYPED[i % len(TYPED)]
            user32.PostMessageW(hwnd, WM_CHAR, ord(unit), 1)
            samples.append(read_value(proc, "input_ms"))
        return {"open_ms": open_ms, "end_ms": end_ms, "samples": samples}
    finally:
        if proc.poll() is None:
            proc.kill()  # this script's own child; the buffer is modified on purpose
        proc.wait(10)


def report(name: str, result: dict, verbose: bool, mode: str) -> float:
    """Every line carries mode=headless|visible: headless frames have no
    wait for the display, so the two are not comparable (AGENTS.md)."""
    s = result["samples"]
    median = statistics.median(s)
    print(f"{name}: mode={mode} open_ms={result['open_ms']:.1f}")
    if result["end_ms"] is not None:
        print(f"{name}: mode={mode} end_key_ms={result['end_ms']:.1f}")
    print(f"{name}: mode={mode} chars={len(s)} median_ms={median:.2f} p95_ms={percentile(s, 95):.2f}"
          f" min_ms={min(s):.2f} max_ms={max(s):.2f}")
    if verbose:
        print(f"{name}: mode={mode} samples_ms=" + " ".join(f"{v:.1f}" for v in s))
    return median


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=pathlib.Path, help="path to geditor.exe")
    parser.add_argument("--data-dir", type=pathlib.Path, default=pathlib.Path("build/measure_input"),
                        help="where the generated files are kept (default build/measure_input)")
    parser.add_argument("--chars", type=int, default=250, help="characters per file (default 250)")
    parser.add_argument("--limit-ms", type=float, default=16.0,
                        help="median limit for the 100 MB file (default 16)")
    parser.add_argument("--samples", action="store_true", help="also print every sample")
    parser.add_argument("--wrap", choices=["none", "window", "fixed", "max"],
                        help="wrap mode written to the settings file (default: the built-in max)")
    parser.add_argument("--visible", action="store_true",
                        help="show the window on the screen (no --headless)")
    parser.add_argument("--m4-5", dest="m4_5", action="store_true",
                        help="M4-5: 100 MB single-line and LF/CRLF files, with open time limits")
    args = parser.parse_args()
    print(f"mode={'visible' if args.visible else 'headless'}", flush=True)
    if not args.exe.is_file():
        print(f"not found: {args.exe}", file=sys.stderr)
        return 2
    big = args.data_dir / "lines_100mb.txt"
    single = args.data_dir / ("single_line_100mb.txt" if args.m4_5 else "single_line_10mb.txt")
    lf_crlf = args.data_dir / "lines_100mb_lf_crlf.txt"
    try:
        ensure_file(big, 100 * MB, generate_lines)
        ensure_file(single, (100 if args.m4_5 else 10) * MB, generate_single_line)
        if args.m4_5:
            ensure_file(lf_crlf, 100 * MB, generate_lf_crlf)
        # A settings file of its own ("{}"): the measurement never depends on
        # (or reads) the user's %USERPROFILE%\.geditor.json.
        settings = args.data_dir / "settings.json"
        if args.wrap:
            settings.write_bytes(b'{"wrap": {"mode": "' + args.wrap.encode() + b'"}}\n')
            print(f"wrap={args.wrap}")
        else:
            settings.write_bytes(b"{}\n")
        settings_arg = f"--settings={settings.resolve()}"
        mode_args = [] if args.visible else ["--headless"]
        # Type in the middle of the 100 MB file.
        lines = run(args.exe, big, [*mode_args, settings_arg, "+500000"], args.chars, press_end=False)
        one = run(args.exe, single, [*mode_args, settings_arg], args.chars, press_end=True)
        mixed = run(args.exe, lf_crlf, [*mode_args, settings_arg], 1, press_end=False) if args.m4_5 else None
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    mode = "visible" if args.visible else "headless"
    median = report("lines_100mb", lines, args.samples, mode)
    single_median = report(single.stem, one, args.samples, mode)
    ok = median <= args.limit_ms
    if args.m4_5:
        print(f"lines_100mb_lf_crlf: mode={mode} open_ms={mixed['open_ms']:.1f}")
        checks = [("lines_100mb open_ms", lines["open_ms"], 1000.0),
                  (f"{single.stem} open_ms", one["open_ms"], 1000.0),
                  (f"{single.stem} median_ms at the end", single_median, args.limit_ms),
                  ("lines_100mb_lf_crlf open_ms", mixed["open_ms"], 2000.0)]
        for name, value, limit in checks:
            fine = value <= limit
            ok = ok and fine
            print(f"M4-5: mode={mode} {name}={value:.1f} limit={limit:.0f} result={'OK' if fine else 'NG'}")
    print(f"mode={mode} limit_ms={args.limit_ms:.1f} median_ms={median:.2f} result={'OK' if ok else 'NG'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
