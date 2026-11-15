// File encodings: the nine of docs/PLAN.md 2 "encodings that can be named"
// (UTF-8 and UTF-16 with or without BOM, Shift_JIS, EUC-JP, ISO-2022-JP),
// detection, and the line ends. The text inside the editor always uses "\n",
// and each piece of the piece tree keeps the line end (LF / CRLF / CR) that
// its "\n" stand for (docs/PLAN.md 2 "internal line ends"), so mixed files
// are kept line by line. Every encoding is first decoded to UTF-16 with its
// "\r" and "\n" as they are; the line ends are then split on that UTF-16
// text by one function for all encodings.
//
// Shift_JIS, EUC-JP and ISO-2022-JP go through the Windows code pages 932,
// 20932 and 50220 (MultiByteToWideChar / WideCharToMultiByte, kernel32).
// The flags each code page accepts were measured on Windows 11 (M4b):
//   932:   MB_ERR_INVALID_CHARS yes; WC_NO_BEST_FIT_CHARS and
//          lpUsedDefaultChar yes (an unwritable character becomes '?' with
//          lpUsedDefaultChar TRUE). With WC_NO_BEST_FIT_CHARS, 12 characters
//          (U+00A2, U+00A3, U+00AB, U+00AC, U+00AF, U+00B5, U+00B7, U+00B8,
//          U+00BB, U+3094, U+3099, U+309A; flags 0 writes them as best-fit
//          double-byte characters such as 81 91): when one of them is the
//          last character, the size query returns the right count, but the
//          conversion into a buffer of exactly that size fails with
//          ERROR_INSUFFICIENT_BUFFER (one more byte: the same count is
//          written, '?' for the character, lpUsedDefaultChar TRUE).
//   20932: MB_ERR_INVALID_CHARS yes (0x80 and the 3-byte 0x8F sequences
//          still decode, to U+0080 / U+008F); WC_NO_BEST_FIT_CHARS and
//          lpUsedDefaultChar yes.
//   50220: neither (ERROR_INVALID_FLAGS; lpUsedDefaultChar gives
//          ERROR_INVALID_PARAMETER); without them it writes half-width
//          katakana as full-width ones.
// So every read is written back and compared with the bytes, and every
// write is read back and compared with the text, on every code page.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "piece_tree.h"

namespace geditor {

enum class Codec : uint8_t { kUtf8, kUtf16Le, kUtf16Be, kShiftJis, kEucJp, kIso2022Jp };

struct FileFormat {
  Codec codec = Codec::kUtf8;
  bool bom = false;  // UTF-8 and UTF-16 only
  // The default line end (PLAN 2 "default line end"): new line ends get it.
  Eol eol = Eol::kLf;
  // PLAN 2 "bytes that cannot be read": some bytes became U+FFFD, or the
  // text does not give the bytes of the file back ("!" in the mode line).
  bool undecodable = false;
};

// UTF-8 and UTF-16 (which can write U+FFFD and every other character).
bool is_unicode(Codec codec);

enum class DecodeStatus {
  kOk,
  kTooLarge,  // more than 4G UTF-16 units (2G bytes for the code pages)
};

struct DecodedText {
  DecodeStatus status = DecodeStatus::kOk;
  // format.eol: the most frequent line end, ties CRLF > LF > CR (decision
  // F1); LF when there is none (the caller then uses new_file.eol).
  // format.undecodable: see error_offset.
  FileFormat format;
  // When format.undecodable: the byte offset of the first byte that could
  // not be read (`replaced`: it became U+FFFD) or, when every byte was read,
  // of the first byte that the text does not give back; error_line is its
  // 0-based line.
  size_t error_offset = 0;
  size_t error_line = 0;
  bool replaced = false;
  // decode_detected: neither UTF-8 nor Shift_JIS fits; read as UTF-8.
  bool detection_failed = false;
  std::wstring text;               // "\n" line ends
  std::vector<uint32_t> line_feeds;  // offsets of '\n' in text
  // Where the line end changes: one run per stretch of equal line ends.
  std::vector<EolRun> eol_runs;
  // Line ends of each kind (Eol order: LF, CRLF, CR).
  size_t eol_counts[3] = {0, 0, 0};
};

// UTF-8 with or without a BOM (a BOM is taken off and sets format.bom).
// Invalid bytes become U+FFFD (format.undecodable).
DecodedText decode_utf8(std::string_view bytes);
// Decodes `bytes` in the encoding of `encoding` (codec and bom). A named
// encoding with a BOM takes the BOM off when it is there; one without a BOM
// keeps a BOM in the text (U+FEFF), so that saving gives the same bytes.
DecodedText decode_as(std::string_view bytes, const FileFormat& encoding);
// Detection (PLAN 2 "UTF-16 without BOM", "undetectable bytes"): a BOM
// (UTF-8, UTF-16LE, UTF-16BE); else UTF-16LE / BE without a BOM when NUL
// bytes are biased to the odd / even offsets; else UTF-8 when the bytes are
// valid UTF-8 (ASCII only included); else Shift_JIS when code page 932
// reads every byte; else UTF-8 with U+FFFD (detection_failed).
DecodedText decode_detected(std::string_view bytes);

// Reading Shift_JIS, EUC-JP and ISO-2022-JP that do not give their bytes
// back as a whole goes over stretches of 64 KiB, each read in one call when
// it reads and gives its bytes back, and line by line or character by
// character otherwise (the same result as lines and characters alone; see
// encoding.cpp). Tests only: sets the stretch size (0: 64 KiB), so that
// small inputs cross many stretch boundaries.
void set_code_page_stretch_for_testing(size_t bytes);

// The encoding names of docs/PLAN.md 2 ("utf-8", "utf-8-bom", "utf-16le",
// "utf-16le-bom", "utf-16be", "utf-16be-bom", "shift_jis", "euc-jp",
// "iso-2022-jp"): sets codec and bom; false for any other name.
bool parse_encoding(std::wstring_view name, FileFormat* format);
// The name of parse_encoding for format's codec and bom.
const wchar_t* encoding_setting_name(const FileFormat& format);
// "Unknown encoding: <name> (use utf-8, utf-8-bom, ...)".
std::wstring unknown_encoding_message(std::wstring_view name);

// The default line end for these counts (decision F1): the most frequent,
// ties CRLF > LF > CR; `none` when every count is 0.
Eol default_eol_of(const size_t counts[3], Eol none);

// Builds the piece tree of decoded text (each run of line ends one piece).
PieceTree make_piece_tree(DecodedText& decoded);

// Encodes the whole tree as UTF-8 with the format's BOM and each piece's
// line end. A lone surrogate is written as U+FFFD.
std::string encode_utf8(const PieceTree& tree, const FileFormat& format);

struct EncodedText {
  bool ok = true;
  std::string bytes;
  // !ok: the offset in the tree of the first character that the encoding
  // cannot write as itself (no character, or a similar one: best fit).
  size_t bad_offset = 0;
  bool too_large = false;  // !ok: the text is beyond the code page functions
  // !ok: the conversion call failed and every character comes back when
  // written alone (bad_offset is 0 then).
  bool conversion_failed = false;
};

// Encodes the whole tree in format's codec (and BOM) with each piece's line
// end. UTF-8 and UTF-16 write a lone surrogate as U+FFFD; the code pages
// refuse any character they cannot write back as itself.
EncodedText encode_text(const PieceTree& tree, const FileFormat& format);

// An LF line end right after a CR line end would be written "\r" "\n" and
// read back as one CRLF, losing a line (M4a fix F1). Returns how many such
// LF line ends `tree` has, counting a run of them after one CR ("\r" then
// "\n\n"): each one follows a CR once the one before it is CR. When the
// count is not 0, `pieces` receives the pieces of `tree` with those line
// ends made CR (the same text; other line ends unchanged), else it is
// cleared.
size_t cr_kept_line_ends(const PieceTree& tree, PieceList* pieces);

// "LF" / "CRLF" / "CR".
const wchar_t* eol_name(Eol eol);
// "lf" / "crlf" / "cr" (set-eol and the settings); false for anything else.
bool parse_eol(std::wstring_view name, Eol* eol);
const wchar_t* eol_setting_name(Eol eol);
// The mode line name of format's codec and bom: "UTF-8", "UTF-8-BOM",
// "UTF-16LE", "UTF-16LE-BOM", "UTF-16BE", "UTF-16BE-BOM", "Shift_JIS",
// "EUC-JP", "ISO-2022-JP" (without the "!" of undecodable).
const wchar_t* encoding_name(const FileFormat& format);

}  // namespace geditor
