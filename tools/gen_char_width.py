#!/usr/bin/env python3
"""Generate the Unicode tables of geditor from the Unicode Character Database.

Usage:
    python -I tools/gen_char_width.py UCD_DIR [--output PATH]
        [--grapheme-output PATH] [--test-output PATH]

UCD_DIR must contain these files of one UCD version (download them into a new
empty directory, for example from https://www.unicode.org/Public/<version>/ucd/):

    EastAsianWidth.txt
    DerivedCoreProperties.txt
    DerivedGeneralCategory.txt   (from the extracted/ subdirectory)
    GraphemeBreakProperty.txt    (from the auxiliary/ subdirectory)
    GraphemeBreakTest.txt        (from the auxiliary/ subdirectory)
    emoji-data.txt               (from the emoji/ subdirectory)

Outputs:
    src/core/char_width_table.inc   cell widths (the first three files)
    src/core/grapheme_table.inc     Grapheme_Cluster_Break, Extended_Pictographic,
                                    Indic_Conjunct_Break, Emoji_Presentation,
                                    Emoji_Modifier and Emoji (UAX #29 extended grapheme
                                    clusters and the cluster width rules)
    tests/grapheme_break_test.inc   every case of GraphemeBreakTest.txt

Width rules (cells):
    - general category Mn or Me, or Default_Ignorable_Code_Point -> 0
    - East Asian Width W or F -> 2
    - C0 controls other than TAB, and U+007F -> 2 (drawn in caret notation)
    - everything else (including ambiguous A) -> 1

The zero-width rule is applied first, so a wide combining mark is 0.
TAB is listed as 1; the layout code advances it to the next tab stop.
The output is deterministic: the same input files give the same bytes.
"""

import argparse
import os
import re
import sys

MAX_CODE_POINT = 0x10FFFF
FILES = ("EastAsianWidth.txt", "DerivedCoreProperties.txt", "DerivedGeneralCategory.txt")
GRAPHEME_FILES = ("GraphemeBreakProperty.txt", "emoji-data.txt", "DerivedCoreProperties.txt")
TEST_FILE = "GraphemeBreakTest.txt"
VERSION_RE = re.compile(r"^#\s*([A-Za-z]+)-(\d+\.\d+\.\d+)\.txt\s*$")
# emoji-data.txt names no version in its first line, but has "# Version: X.Y.Z".
EMOJI_VERSION_RE = re.compile(r"^#\s*Version:\s*(\d+\.\d+\.\d+)\s*$")
PROPERTY_RE = re.compile(
    r"^([0-9A-Fa-f]{4,6})(?:\.\.([0-9A-Fa-f]{4,6}))?\s*;\s*([A-Za-z_]+)\s*(?:;\s*([A-Za-z_]+))?\s*(?:#|$)")

# Grapheme_Cluster_Break values in the order of GraphemeClass in
# src/core/grapheme.h (Other is 0).
GCB_VALUES = ("Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend",
              "SpacingMark", "L", "V", "T", "LV", "LVT")
INCB_VALUES = {"None": 0, "Linker": 1, "Consonant": 2, "Extend": 3}
EXT_PICT_BIT = 0x10
INCB_SHIFT = 5
EMOJI_PRESENTATION_BIT = 0x80
BLOCK_SHIFT = 7
RANGE_RE = re.compile(r"^([0-9A-Fa-f]{4,6})(?:\.\.([0-9A-Fa-f]{4,6}))?\s*;\s*([A-Za-z_]+)")
MISSING_RE = re.compile(r"^#\s*@missing:\s*([0-9A-Fa-f]{4,6})\.\.([0-9A-Fa-f]{4,6})\s*;\s*([A-Za-z_]+)")


def read_lines(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read().splitlines()


def file_version(lines, path):
    for line in lines[:5]:
        m = VERSION_RE.match(line)
        if m:
            return m.group(2)
    raise SystemExit(f"error: no version header in {path}")


def parse_ranges(lines):
    """Yields (first, last, value) for data lines."""
    for line in lines:
        m = RANGE_RE.match(line)
        if not m:
            continue
        first = int(m.group(1), 16)
        last = int(m.group(2), 16) if m.group(2) else first
        yield first, last, m.group(3)


def parse_missing(lines):
    for line in lines:
        m = MISSING_RE.match(line)
        if m:
            yield int(m.group(1), 16), int(m.group(2), 16), m.group(3)


def build_widths(ucd_dir):
    paths = [os.path.join(ucd_dir, name) for name in FILES]
    contents = []
    versions = set()
    for path in paths:
        if not os.path.isfile(path):
            raise SystemExit(f"error: missing {path}")
        lines = read_lines(path)
        versions.add(file_version(lines, path))
        contents.append(lines)
    if len(versions) != 1:
        raise SystemExit(f"error: the UCD files have different versions: {sorted(versions)}")
    version = versions.pop()
    eaw_lines, core_lines, gc_lines = contents

    eaw = bytearray(b"N" * (MAX_CODE_POINT + 1))
    for first, last, value in parse_missing(eaw_lines):
        code = value.encode("ascii")[0:1]
        for cp in range(first, last + 1):
            eaw[cp] = code[0]
    wide = set()
    for first, last, value in parse_ranges(eaw_lines):
        if value in ("W", "F"):
            wide.update(range(first, last + 1))
        else:
            for cp in range(first, last + 1):
                wide.discard(cp)
    # @missing lines can only make code points wide when their value is W/F.
    for first, last, value in parse_missing(eaw_lines):
        if value in ("W", "F"):
            explicit = set()
            for f2, l2, _ in parse_ranges(eaw_lines):
                explicit.update(range(max(f2, first), min(l2, last) + 1))
            wide.update(cp for cp in range(first, last + 1) if cp not in explicit)

    zero = set()
    for first, last, value in parse_ranges(gc_lines):
        if value in ("Mn", "Me"):
            zero.update(range(first, last + 1))
    for first, last, value in parse_ranges(core_lines):
        if value == "Default_Ignorable_Code_Point":
            zero.update(range(first, last + 1))

    widths = bytearray(b"\x01" * (MAX_CODE_POINT + 1))
    for cp in wide:
        widths[cp] = 2
    for cp in range(0x00, 0x20):
        if cp != 0x09:
            widths[cp] = 2
    widths[0x7F] = 2
    for cp in zero:
        widths[cp] = 0
    return version, widths


def to_ranges(widths):
    ranges = []
    start = 0
    current = widths[0]
    for cp in range(1, MAX_CODE_POINT + 2):
        value = widths[cp] if cp <= MAX_CODE_POINT else None
        if value != current:
            if current != 1:
                ranges.append((start, cp - 1, current))
            start = cp
            current = value
    return ranges


def render(version, ranges):
    out = []
    out.append("// Generated by tools/gen_char_width.py. Do not edit by hand.")
    out.append(f"// Source: Unicode Character Database {version}")
    out.append("// (EastAsianWidth.txt, DerivedCoreProperties.txt, DerivedGeneralCategory.txt).")
    out.append("// Each entry is {first, last, width}; code points not listed have width 1.")
    out.append("")
    out.append(f'constexpr char kUnicodeVersion[] = "{version}";')
    out.append("")
    out.append("constexpr CharWidthRange kCharWidthRanges[] = {")
    for first, last, width in ranges:
        out.append(f"    {{0x{first:06X}, 0x{last:06X}, {width}}},")
    out.append("};")
    out.append("")
    return "\n".join(out)


def any_version(lines, path):
    """The version of a UCD file: in its first line, or a "# Version:" line."""
    for line in lines[:5]:
        m = VERSION_RE.match(line)
        if m:
            return m.group(2)
    for line in lines[:20]:
        m = EMOJI_VERSION_RE.match(line)
        if m:
            return m.group(1)
    raise SystemExit(f"error: no version header in {path}")


def parse_properties(lines):
    """Yields (first, last, property, value) for data lines ("value" is None
    for binary properties)."""
    for line in lines:
        m = PROPERTY_RE.match(line)
        if not m:
            continue
        first = int(m.group(1), 16)
        last = int(m.group(2), 16) if m.group(2) else first
        yield first, last, m.group(3), m.group(4)


def build_grapheme(ucd_dir):
    contents = []
    versions = set()
    for name in GRAPHEME_FILES:
        path = os.path.join(ucd_dir, name)
        if not os.path.isfile(path):
            raise SystemExit(f"error: missing {path}")
        lines = read_lines(path)
        versions.add(any_version(lines, path))
        contents.append(lines)
    if len(versions) != 1:
        raise SystemExit(f"error: the UCD files have different versions: {sorted(versions)}")
    version = versions.pop()
    gcb_lines, emoji_lines, core_lines = contents

    props = bytearray(MAX_CODE_POINT + 1)  # GCB Other, no flags
    for first, last, prop, _ in parse_properties(gcb_lines):
        if prop not in GCB_VALUES:
            raise SystemExit(f"error: unknown Grapheme_Cluster_Break value {prop}")
        value = GCB_VALUES.index(prop)
        for cp in range(first, last + 1):
            props[cp] = (props[cp] & 0xF0) | value
    modifiers = []
    emoji = []
    for first, last, prop, _ in parse_properties(emoji_lines):
        if prop == "Emoji":
            emoji.append((first, last))
        elif prop == "Extended_Pictographic":
            for cp in range(first, last + 1):
                props[cp] |= EXT_PICT_BIT
        elif prop == "Emoji_Presentation":
            for cp in range(first, last + 1):
                props[cp] |= EMOJI_PRESENTATION_BIT
        elif prop == "Emoji_Modifier":
            modifiers.append((first, last))
    for first, last, prop, value in parse_properties(core_lines):
        if prop != "InCB":
            continue
        if value not in INCB_VALUES:
            raise SystemExit(f"error: unknown Indic_Conjunct_Break value {value}")
        for cp in range(first, last + 1):
            props[cp] = (props[cp] & ~(0x3 << INCB_SHIFT) & 0xFF) | (INCB_VALUES[value] << INCB_SHIFT)
    modifiers.sort()
    return version, props, modifiers, merge_ranges(emoji)


def merge_ranges(ranges):
    """Sorted ranges with adjacent and overlapping ones joined."""
    out = []
    for first, last in sorted(ranges):
        if out and first <= out[-1][1] + 1:
            out[-1] = (out[-1][0], max(out[-1][1], last))
        else:
            out.append((first, last))
    return out


def to_stages(values):
    """Two-stage table: stage 1 maps each block of 2**BLOCK_SHIFT code points to
    a block of stage 2 (identical blocks are stored once)."""
    size = 1 << BLOCK_SHIFT
    blocks = {}
    stage1 = []
    stage2 = []
    for start in range(0, MAX_CODE_POINT + 1, size):
        block = tuple(values[start:start + size])
        index = blocks.get(block)
        if index is None:
            index = len(blocks)
            blocks[block] = index
            stage2.extend(block)
        stage1.append(index)
    return stage1, stage2


def render_numbers(values, per_line, width):
    out = []
    for i in range(0, len(values), per_line):
        out.append("    " + " ".join(f"{v:#0{width}x}," for v in values[i:i + per_line]))
    return out


def render_grapheme(version, props, widths, modifiers, emoji):
    # The cell width of char_width_table.inc (the same computation) in bits
    # 8-9, so that the layout reads properties and width with one lookup.
    values = [props[cp] | (widths[cp] << 8) for cp in range(MAX_CODE_POINT + 1)]
    stage1, stage2 = to_stages(values)
    out = []
    out.append("// Generated by tools/gen_char_width.py. Do not edit by hand.")
    out.append(f"// Source: Unicode Character Database {version}")
    out.append("// (GraphemeBreakProperty.txt, emoji-data.txt, DerivedCoreProperties.txt; the")
    out.append("// widths as in char_width_table.inc).")
    out.append("// Value of a code point: bits 0-3 Grapheme_Cluster_Break (GraphemeClass),")
    out.append("// bit 4 Extended_Pictographic, bits 5-6 Indic_Conjunct_Break (0 None, 1 Linker,")
    out.append("// 2 Consonant, 3 Extend), bit 7 Emoji_Presentation, bits 8-9 the cell width")
    out.append("// (char_width). Code point cp has the value")
    out.append("// kGraphemeStage2[(kGraphemeStage1[cp >> kGraphemeBlockShift] << kGraphemeBlockShift)")
    out.append("// + (cp & ((1 << kGraphemeBlockShift) - 1))].")
    out.append("")
    out.append(f'constexpr char kGraphemeUnicodeVersion[] = "{version}";')
    out.append(f"constexpr unsigned kGraphemeBlockShift = {BLOCK_SHIFT};")
    out.append("")
    if max(stage1) > 0xFF:
        raise SystemExit("error: more than 256 distinct blocks; widen kGraphemeStage1")
    out.append("constexpr uint8_t kGraphemeStage1[] = {")
    out.extend(render_numbers(stage1, 16, 4))
    out.append("};")
    out.append("")
    out.append("constexpr uint16_t kGraphemeStage2[] = {")
    out.extend(render_numbers(stage2, 12, 6))
    out.append("};")
    out.append("")
    out.append("// Emoji_Modifier: {first, last}.")
    out.append("constexpr GraphemeRange kEmojiModifierRanges[] = {")
    for first, last in modifiers:
        out.append(f"    {{0x{first:06X}, 0x{last:06X}}},")
    out.append("};")
    out.append("")
    out.append("// Emoji (the characters that U+FE0F can show as emoji): {first, last}.")
    out.append("constexpr GraphemeRange kEmojiRanges[] = {")
    for first, last in emoji:
        out.append(f"    {{0x{first:06X}, 0x{last:06X}}},")
    out.append("};")
    out.append("")
    return "\n".join(out), len(stage1), len(stage2)


def build_tests(ucd_dir):
    path = os.path.join(ucd_dir, TEST_FILE)
    if not os.path.isfile(path):
        raise SystemExit(f"error: missing {path}")
    lines = read_lines(path)
    version = file_version(lines, path)
    cases = []
    for number, line in enumerate(lines, 1):
        data = line.split("#", 1)[0].strip()
        if not data:
            continue
        tokens = data.split()
        cps = []
        breaks = []
        for i, token in enumerate(tokens):
            if i % 2 == 0:
                if token not in ("÷", "×"):
                    raise SystemExit(f"error: {path}:{number}: expected a break mark, got {token!r}")
                breaks.append("1" if token == "÷" else "0")
            else:
                cps.append(int(token, 16))
        if len(breaks) != len(cps) + 1 or not cps:
            raise SystemExit(f"error: {path}:{number}: malformed case")
        cases.append((number, cps, "".join(breaks)))
    return version, cases


def render_tests(version, cases):
    out = []
    out.append(f"// Generated by tools/gen_char_width.py from GraphemeBreakTest-{version}.txt. Do not edit by hand.")
    out.append("// Each case: its line in that file, its code points (an offset into")
    out.append("// kGraphemeBreakCodePoints and a count) and one flag per position from the")
    out.append("// start to the end: '1' where there is a break, '0' where there is none.")
    out.append("")
    out.append(f'constexpr char kGraphemeBreakTestVersion[] = "{version}";')
    out.append("")
    out.append("constexpr char32_t kGraphemeBreakCodePoints[] = {")
    flat = []
    for _, cps, _ in cases:
        flat.extend(cps)
    for i in range(0, len(flat), 8):
        out.append("    " + " ".join(f"0x{cp:06X}," for cp in flat[i:i + 8]))
    out.append("};")
    out.append("")
    out.append("constexpr GraphemeBreakCase kGraphemeBreakCases[] = {")
    index = 0
    for number, cps, breaks in cases:
        out.append(f'    {{{number}, {index}, {len(cps)}, "{breaks}"}},')
        index += len(cps)
    out.append("};")
    out.append("")
    return "\n".join(out)


def write(path, text):
    with open(path, "wb") as f:
        f.write(text.encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ucd_dir", help="directory with the UCD files")
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir)
    default_output = os.path.join(root, "src", "core", "char_width_table.inc")
    default_grapheme = os.path.join(root, "src", "core", "grapheme_table.inc")
    default_test = os.path.join(root, "tests", "grapheme_break_test.inc")
    parser.add_argument("--output", default=os.path.normpath(default_output), help="width table output file")
    parser.add_argument("--grapheme-output", default=os.path.normpath(default_grapheme),
                        help="grapheme property table output file")
    parser.add_argument("--test-output", default=os.path.normpath(default_test),
                        help="GraphemeBreakTest cases output file")
    args = parser.parse_args()

    version, widths = build_widths(args.ucd_dir)
    grapheme_version, props, modifiers, emoji = build_grapheme(args.ucd_dir)
    test_version, cases = build_tests(args.ucd_dir)
    if len({version, grapheme_version, test_version}) != 1:
        raise SystemExit(f"error: the UCD files have different versions: "
                         f"{sorted({version, grapheme_version, test_version})}")

    ranges = to_ranges(widths)
    write(args.output, render(version, ranges))
    print(f"wrote {args.output}: Unicode {version}, {len(ranges)} ranges")
    text, blocks, stage2 = render_grapheme(grapheme_version, props, widths, modifiers, emoji)
    write(args.grapheme_output, text)
    print(f"wrote {args.grapheme_output}: Unicode {grapheme_version}, {blocks} blocks, "
          f"{stage2} values in distinct blocks, {len(modifiers)} modifier ranges, {len(emoji)} emoji ranges")
    write(args.test_output, render_tests(test_version, cases))
    print(f"wrote {args.test_output}: GraphemeBreakTest {test_version}, {len(cases)} cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
