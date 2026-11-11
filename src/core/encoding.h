// File encodings. M1 handles UTF-8 with or without BOM and LF / CRLF line
// ends; the text inside the editor always uses "\n".
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "piece_tree.h"

namespace geditor {

enum class Eol { kLf, kCrLf };

struct FileFormat {
  bool bom = false;
  Eol eol = Eol::kLf;
};

enum class DecodeStatus {
  kOk,
  kInvalidUtf8,  // error_offset is the byte offset of the bad sequence
  kMixedEol,     // more than one kind of line end (LF, CRLF, CR)
  kCrEol,        // CR-only line ends
  kTooLarge,     // more than 4G UTF-16 units
};

struct DecodedText {
  DecodeStatus status = DecodeStatus::kOk;
  size_t error_offset = 0;
  FileFormat format;
  std::wstring text;               // "\n" line ends
  std::vector<uint32_t> line_feeds;  // offsets of '\n' in text
};

DecodedText decode_utf8(std::string_view bytes);

// Encodes the whole tree with the format's BOM and line ends. A lone
// surrogate is written as U+FFFD.
std::string encode_utf8(const PieceTree& tree, const FileFormat& format);

const wchar_t* eol_name(Eol eol);
const wchar_t* encoding_name(const FileFormat& format);

}  // namespace geditor
