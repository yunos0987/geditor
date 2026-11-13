"""Measure geditor startup time.

Runs `geditor.exe --measure-startup` several times. Each run prints to stderr:

  startup_ms  process creation -> end of the first frame
  entry_ms    process creation -> start of wWinMain (loader, antivirus hold)
  app_ms      start of wWinMain -> end of the first frame

The first run is a warm-up and is not counted. --metric selects which median
is compared with --limit-ms (default: startup).

By default geditor gets --settings=build/measure_startup/settings.json (an
empty "{}" file written by this script), so a measurement never depends on
the user's .geditor.json in %USERPROFILE%. Arguments after "--" replace that
default and are passed to geditor as they are, for example another settings
file:

  python tools/measure_startup.py build/release/geditor.exe -- --settings=build/s.json

Exit code: 0 when the median is within --limit-ms, 1 otherwise, 2 on errors.
"""

import argparse
import pathlib
import re
import statistics
import subprocess
import sys

METRICS = ("startup", "entry", "app")
PATTERN = re.compile(r"startup_ms=([0-9.]+) entry_ms=([0-9.]+) app_ms=([0-9.]+)")


def measure_once(exe: pathlib.Path, timeout_s: float,
                 app_args: list[str] | None = None) -> dict[str, float]:
    result = subprocess.run(
        [str(exe), "--measure-startup", *(app_args or [])],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        timeout=timeout_s,
        check=False,
    )
    text = result.stderr.decode("utf-8", errors="replace")
    match = PATTERN.search(text)
    if match is None:
        raise RuntimeError(f"no startup_ms in output (exit={result.returncode}): {text!r}")
    return {name: float(match.group(i + 1)) for i, name in enumerate(METRICS)}


def default_app_args() -> list[str]:
    """--settings with an empty settings file under build/ (written here)."""
    settings = pathlib.Path(__file__).resolve().parent.parent / "build" / "measure_startup" / "settings.json"
    settings.parent.mkdir(parents=True, exist_ok=True)
    settings.write_bytes(b"{}\n")
    return [f"--settings={settings}"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=pathlib.Path, help="path to geditor.exe")
    parser.add_argument("--runs", type=int, default=10, help="counted runs (default 10)")
    parser.add_argument("--limit-ms", type=float, default=100.0, help="median limit (default 100)")
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds per run (default 10)")
    parser.add_argument("--metric", choices=METRICS, default="startup",
                        help="metric compared with --limit-ms (default startup)")
    argv = sys.argv[1:]
    app_args: list[str] | None = None
    if "--" in argv:
        split = argv.index("--")
        argv, app_args = argv[:split], argv[split + 1:]
    args = parser.parse_args(argv)
    if app_args is None:
        app_args = default_app_args()
    if args.runs < 1:
        print("--runs must be 1 or more", file=sys.stderr)
        return 2

    if not args.exe.is_file():
        print(f"not found: {args.exe}", file=sys.stderr)
        return 2
    try:
        measure_once(args.exe, args.timeout, app_args)  # warm-up
        samples = [measure_once(args.exe, args.timeout, app_args) for _ in range(args.runs)]
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    for name in METRICS:
        values = [s[name] for s in samples]
        print(f"{name}: samples_ms=" + " ".join(f"{v:.1f}" for v in values))
        print(f"{name}: min_ms={min(values):.1f} median_ms={statistics.median(values):.1f}"
              f" max_ms={max(values):.1f}")
    median = statistics.median(s[args.metric] for s in samples)
    ok = median <= args.limit_ms
    print(f"metric={args.metric} limit_ms={args.limit_ms:.1f} median_ms={median:.1f}"
          f" result={'OK' if ok else 'NG'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
