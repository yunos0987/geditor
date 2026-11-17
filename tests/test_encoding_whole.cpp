// Deferred item K2: Shift_JIS and EUC-JP text is compared with the bytes it
// was read from, and written when saved, stretch by stretch instead of in
// whole-file conversions, and detection no longer holds the buffer of its
// UTF-8 attempt while it tries Shift_JIS. The tests compare every result
// with the whole-file conversions alone
// (set_code_page_whole_conversions_for_testing), for all nine encodings, on
// generated texts with mixed line ends and with bytes or characters that do
// not read, do not write or do not come back the same, with stretch sizes
// from one unit to the default: reading (the text, "!", the first bad byte
// and its line, U+FFFD, the line ends, detection) and saving (the bytes,
// ok, the first character that cannot be written, too_large,
// conversion_failed).

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "encoding.h"
#include "piece_tree.h"
#include "test_harness.h"

using geditor::Codec;
using geditor::DecodedText;
using geditor::EncodedText;
using geditor::Eol;
using geditor::FileFormat;
using geditor::PieceTree;

namespace {

struct Rng {
  uint64_t state;
  explicit Rng(uint64_t seed) : state(seed) {}
  uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  size_t below(size_t n) { return static_cast<size_t>(next() % n); }
};

// The nine encodings of encoding_names().
struct Named {
  const char* name;
  Codec codec;
  bool bom;
};
const Named kNine[] = {
    {"utf-8", Codec::kUtf8, false},         {"utf-8-bom", Codec::kUtf8, true},
    {"utf-16le", Codec::kUtf16Le, false},   {"utf-16le-bom", Codec::kUtf16Le, true},
    {"utf-16be", Codec::kUtf16Be, false},   {"utf-16be-bom", Codec::kUtf16Be, true},
    {"shift_jis", Codec::kShiftJis, false}, {"euc-jp", Codec::kEucJp, false},
    {"iso-2022-jp", Codec::kIso2022Jp, false},
};

// Text pieces: ASCII, Japanese, characters a code page writes differently
// or not at all, the 12 characters that make 932 fail at the end of a
// conversion into exactly its size (encoding.h), lone surrogates.
const wchar_t* const kPieces[] = {
    L"abc", L" ", L"editor", L"\x65E5\x672C\x8A9E", L"\x30C6\x30AD\x30B9\x30C8", L"\xFF83\xFF7D", L"\x2460\x2161",
    L"\x7E8A", L"\x00A5", L"\x203E", L"\t", L"0123456789", L"\x3000", L"\x2015\xFF5E", L"\xD83D\xDE00",
    L"\x00A2", L"\x00AB", L"\x00B7", L"\x3094", L"\x309A", L"\xD800", L"\xDC00", L"\x0080", L"\x008F",
};

std::wstring random_text(Rng& rng, size_t units, bool rare_pieces) {
  // Without the rare pieces, most texts write and come back (the stretch
  // path then runs to the end).
  const size_t common = 14;
  std::wstring s;
  while (s.size() < units) {
    if (rng.below(7) == 0) {
      static const wchar_t* const kEnds[] = {L"\n", L"\r\n", L"\r"};
      s += kEnds[rng.below(3)];
    } else {
      s += kPieces[rng.below(rare_pieces && rng.below(8) == 0 ? std::size(kPieces) : common)];
    }
  }
  return s;
}

std::string encode(UINT cp, const std::wstring& s) {
  if (s.empty()) return {};
  const int n = WideCharToMultiByte(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
  WideCharToMultiByte(cp, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::string bytes_of(const Named& e, const std::wstring& s) {
  switch (e.codec) {
    case Codec::kUtf8: return (e.bom ? std::string("\xEF\xBB\xBF") : std::string()) + encode(CP_UTF8, s);
    case Codec::kUtf16Le:
    case Codec::kUtf16Be: {
      std::string out = e.bom ? std::string(e.codec == Codec::kUtf16Le ? "\xFF\xFE" : "\xFE\xFF") : std::string();
      for (const wchar_t w : s) {
        const char hi = static_cast<char>(w >> 8);
        const char lo = static_cast<char>(w & 0xFF);
        out += e.codec == Codec::kUtf16Le ? std::string{lo, hi} : std::string{hi, lo};
      }
      return out;
    }
    case Codec::kShiftJis: return encode(932, s);
    case Codec::kEucJp: return encode(20932, s);
    case Codec::kIso2022Jp: return encode(50220, s);
  }
  return {};
}

// Bytes that do not read, or read and are written back differently.
const char* const kDefects[] = {"\x81\x20", "\x81", "\xFF", "\x80", "\xA0", "\xED\x40", "\x81\n", "\x8E\x20",
                                "\x8F\xA1\xA1", "\x8F", "\xA1", "\x1B", "\x1B$B\x21", "\x1B(I\x31", "\xC3",
                                "\xE3\x81", "\xF0\x9F\x98", "\x00", "\xD8\x00"};

std::string decode_difference(const DecodedText& a, const DecodedText& b) {
  if (a.status != b.status) return "status";
  if (a.format.codec != b.format.codec || a.format.bom != b.format.bom || a.format.eol != b.format.eol) {
    return "format";
  }
  if (a.format.undecodable != b.format.undecodable) return "undecodable (!)";
  if (a.error_offset != b.error_offset) return "error_offset";
  if (a.error_line != b.error_line) return "error_line";
  if (a.replaced != b.replaced) return "replaced";
  if (a.detection_failed != b.detection_failed) return "detection_failed";
  if (a.text != b.text) return "text";
  if (a.line_feeds != b.line_feeds) return "line_feeds";
  if (a.eol_runs.size() != b.eol_runs.size()) return "eol_runs";
  for (size_t i = 0; i < a.eol_runs.size(); ++i) {
    if (a.eol_runs[i].start != b.eol_runs[i].start || a.eol_runs[i].eol != b.eol_runs[i].eol) return "eol_runs";
  }
  for (size_t i = 0; i < 3; ++i) {
    if (a.eol_counts[i] != b.eol_counts[i]) return "eol_counts";
  }
  if (a.text.capacity() > b.text.capacity() + 16) return "capacity";
  return "";
}

std::string encode_difference(const EncodedText& a, const EncodedText& b) {
  if (a.ok != b.ok) return "ok";
  if (a.bytes != b.bytes) return "bytes";
  if (a.bad_offset != b.bad_offset) return "bad_offset";
  if (a.too_large != b.too_large) return "too_large";
  if (a.conversion_failed != b.conversion_failed) return "conversion_failed";
  if (a.bytes.capacity() > b.bytes.capacity() + 16) return "capacity";
  return "";
}

struct Counter {
  size_t decodes = 0;
  size_t encodes = 0;
  size_t different = 0;
  size_t not_ok = 0;  // saves that refused (a character that cannot be written)
  size_t undecodable = 0;
  void fail(const std::string& what) {
    ++different;
    if (different <= 8) ::test::report_failure(__FILE__, __LINE__, "differs from the whole-file conversions: " + what);
  }
};

constexpr size_t kStretches[] = {1, 2, 3, 7, 16, 61, 0};

void compare_decode(Counter* c, std::string_view bytes, const Named& e, bool detected, const std::string& name) {
  geditor::set_code_page_whole_conversions_for_testing(true);
  geditor::set_code_page_stretch_for_testing(0);
  FileFormat format;
  format.codec = e.codec;
  format.bom = e.bom;
  const DecodedText old = detected ? geditor::decode_detected(bytes) : geditor::decode_as(bytes, format);
  geditor::set_code_page_whole_conversions_for_testing(false);
  for (const size_t stretch : kStretches) {
    geditor::set_code_page_stretch_for_testing(stretch);
    const DecodedText now = detected ? geditor::decode_detected(bytes) : geditor::decode_as(bytes, format);
    ++c->decodes;
    if (now.format.undecodable) ++c->undecodable;
    const std::string why = decode_difference(now, old);
    if (!why.empty()) c->fail(why + " decoding " + name + " stretch " + std::to_string(stretch));
  }
  geditor::set_code_page_stretch_for_testing(0);
}

void compare_encode(Counter* c, const PieceTree& tree, const Named& e, const std::string& name) {
  FileFormat format;
  format.codec = e.codec;
  format.bom = e.bom;
  geditor::set_code_page_whole_conversions_for_testing(true);
  geditor::set_code_page_stretch_for_testing(0);
  const EncodedText old = geditor::encode_text(tree, format);
  geditor::set_code_page_whole_conversions_for_testing(false);
  for (const size_t stretch : kStretches) {
    geditor::set_code_page_stretch_for_testing(stretch);
    const EncodedText now = geditor::encode_text(tree, format);
    ++c->encodes;
    if (!now.ok) ++c->not_ok;
    const std::string why = encode_difference(now, old);
    if (!why.empty()) c->fail(why + " encoding " + name + " stretch " + std::to_string(stretch));
  }
  geditor::set_code_page_stretch_for_testing(0);
}

// A tree of `text` in pieces of random line ends (LF, CRLF, CR) and random
// splits (inserted in random order).
PieceTree random_tree(Rng& rng, const std::wstring& text) {
  PieceTree tree;
  size_t at = 0;
  while (at < text.size()) {
    const size_t n = std::min(text.size() - at, 1 + rng.below(40));
    std::wstring part = text.substr(at, n);
    // The tree keeps line ends as '\n'.
    std::replace(part.begin(), part.end(), L'\r', L'\n');
    const Eol eol = static_cast<Eol>(rng.below(3));
    const size_t where = tree.length() == 0 || rng.below(4) != 0 ? tree.length() : rng.below(tree.length() + 1);
    tree.insert(where, part, eol);
    at += n;
  }
  return tree;
}

}  // namespace

TEST(encoding_whole_decoding_matches_whole_file_conversions) {
  Counter c;
  Rng rng(0x4B32);
  for (const Named& e : kNine) {
    for (int n = 0; n < 60; ++n) {
      std::string bytes = bytes_of(e, random_text(rng, 1 + rng.below(n < 50 ? 300 : 20000), n % 3 == 0));
      const size_t defects = n % 2 == 0 ? 0 : 1 + rng.below(3);
      for (size_t k = 0; k < defects; ++k) {
        const size_t where = rng.below(3);
        const size_t at = where == 0 ? 0 : where == 1 ? bytes.size() : rng.below(bytes.size() + 1);
        bytes.insert(at, kDefects[rng.below(std::size(kDefects))]);
      }
      compare_decode(&c, bytes, e, false, std::string(e.name) + " named #" + std::to_string(n));
      compare_decode(&c, bytes, e, true, std::string(e.name) + " detected #" + std::to_string(n));
    }
  }
  // Fixed cases: empty, one byte, the defects alone and around a line end.
  for (const Named& e : kNine) {
    compare_decode(&c, "", e, false, "empty");
    for (const char* d : kDefects) {
      const std::string s = d;
      compare_decode(&c, s, e, false, "defect");
      compare_decode(&c, "ab\r\n" + s + "\ncd", e, false, "defect in lines");
      compare_decode(&c, "ab\r\n" + s + "\ncd", e, true, "defect in lines, detected");
    }
  }
  std::printf("  [encoding whole] %zu decodings compared (%zu with \"!\"), %zu different\n", c.decodes, c.undecodable,
              c.different);
  CHECK(c.undecodable > 100);
  CHECK_EQ(c.different, size_t{0});
}

TEST(encoding_whole_detection_after_a_long_utf8_prefix_matches) {
  // The UTF-8 attempt stops late (its buffer is kept, not released) or
  // early (released, then sized again when neither UTF-8 nor Shift_JIS
  // fits).
  Counter c;
  Rng rng(0xD7);
  const Named utf8 = kNine[0];
  for (const size_t ascii : {size_t{0}, size_t{3}, size_t{100}, size_t{5000}}) {
    for (const char* tail : {"\x82\xA0", "\x82\xA0\xFF", "\xFF", "\xE3\x81\x82\x82", "\x81\x20\xE3\x81\x82"}) {
      const std::string bytes = std::string(ascii, 'a') + tail + bytes_of(utf8, random_text(rng, 2000, false));
      compare_decode(&c, bytes, utf8, true, "prefix " + std::to_string(ascii));
    }
  }
  std::printf("  [encoding whole] %zu detections compared, %zu different\n", c.decodes, c.different);
  CHECK_EQ(c.different, size_t{0});
}

TEST(encoding_whole_saving_matches_whole_file_conversions) {
  Counter c;
  Rng rng(0x5A7E);
  for (const Named& e : kNine) {
    for (int n = 0; n < 80; ++n) {
      const std::wstring text = random_text(rng, 1 + rng.below(n < 70 ? 300 : 30000), n % 2 == 0);
      const PieceTree tree = random_tree(rng, text);
      compare_encode(&c, tree, e, std::string(e.name) + " #" + std::to_string(n));
    }
    // Each rare piece at the end, at the start, and before a line end.
    for (const wchar_t* p : kPieces) {
      for (const std::wstring& t : {std::wstring(L"ab") + p, p + std::wstring(L"\ncd"),
                                    std::wstring(L"x\n") + p + L"\n" + p}) {
        compare_encode(&c, PieceTree(t), e, std::string(e.name) + " piece");
      }
    }
    compare_encode(&c, PieceTree(), e, std::string(e.name) + " empty");
  }
  std::printf("  [encoding whole] %zu saves compared (%zu refused), %zu different\n", c.encodes, c.not_ok,
              c.different);
  CHECK(c.not_ok > 100);
  CHECK_EQ(c.different, size_t{0});
}
