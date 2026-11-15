// File encodings. M4a handles UTF-8 with or without BOM; the text inside the
// editor always uses "\n", and each piece of the piece tree keeps the line
// end (LF / CRLF / CR) that its "\n" stand for (docs/PLAN.md 2 "internal line
// ends"), so mixed files are kept line by line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "piece_tree.h"

namespace geditor {

struct FileFormat {
  bool bom = false;
  // The default line end (PLAN 2 "default line end"): new line ends get it.
  Eol eol = Eol::kLf;
};

enum class DecodeStatus {
  kOk,
  kInvalidUtf8,  // error_offset is the byte offset of the bad sequence
  kTooLarge,     // more than 4G UTF-16 units
};

struct DecodedText {
  DecodeStatus status = DecodeStatus::kOk;
  size_t error_offset = 0;
  // format.eol: the most frequent line end, ties CRLF > LF > CR (decision
  // F1); LF when there is none (the caller then uses new_file.eol).
  FileFormat format;
  std::wstring text;               // "\n" line ends
  std::vector<uint32_t> line_feeds;  // offsets of '\n' in text
  // Where the line end changes: one run per stretch of equal line ends.
  std::vector<EolRun> eol_runs;
  // Line ends of each kind (Eol order: LF, CRLF, CR).
  size_t eol_counts[3] = {0, 0, 0};
};

DecodedText decode_utf8(std::string_view bytes);

// The default line end for these counts (decision F1): the most frequent,
// ties CRLF > LF > CR; `none` when every count is 0.
Eol default_eol_of(const size_t counts[3], Eol none);

// Builds the piece tree of decoded text (each run of line ends one piece).
PieceTree make_piece_tree(DecodedText& decoded);

// Encodes the whole tree with the format's BOM and each piece's line end. A
// lone surrogate is written as U+FFFD.
std::string encode_utf8(const PieceTree& tree, const FileFormat& format);

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
const wchar_t* encoding_name(const FileFormat& format);

}  // namespace geditor
