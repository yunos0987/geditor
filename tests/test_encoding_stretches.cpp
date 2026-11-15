// The code page reading of encoding.cpp after the deferred performance work
// (before M6): decode_detected no longer decodes Shift_JIS as UTF-8 first,
// no conversion asks for its size first, and the fallbacks for files that
// do not give their bytes back as a whole read stretches of 64 KiB in one
// call. The result must be what the reading of HEAD 368313b gave. `head`
// below is a copy of that reading (encoding.cpp at 368313b: mb_to_wide,
// wide_to_mb, raw_dbcs, raw_iso2022jp, raw_codepage and the detection
// order), and every test compares the two on generated inputs: the text,
// the line ends, U+FFFD and its positions, the noted byte and line, "!"
// (undecodable) and the detected encoding.

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "encoding.h"
#include "test_harness.h"
#include "unicode.h"

using geditor::Codec;
using geditor::DecodedText;
using geditor::FileFormat;

namespace {
namespace head {

constexpr wchar_t kReplacement = 0xFFFD;

struct RawText {
  std::wstring text;
  bool bad = false;
  bool replaced = false;
  size_t bad_byte = 0;
  size_t bad_index = 0;

  void note(size_t byte, size_t index, bool replacement) {
    replaced = replaced || replacement;
    if (bad) return;
    bad = true;
    bad_byte = byte;
    bad_index = index;
  }
};

struct CodePage {
  UINT id;
  bool read_flag;
  bool write_flags;
};

constexpr CodePage kShiftJisPage{932, true, true};
constexpr CodePage kEucJpPage{20932, true, true};
constexpr CodePage kIso2022JpPage{50220, false, false};

const CodePage& code_page(Codec codec) {
  if (codec == Codec::kEucJp) return kEucJpPage;
  if (codec == Codec::kIso2022Jp) return kIso2022JpPage;
  return kShiftJisPage;
}

bool mb_to_wide(const CodePage& cp, bool strict, std::string_view bytes, std::wstring* out) {
  out->clear();
  if (bytes.empty()) return true;
  if (bytes.size() > static_cast<size_t>(INT_MAX)) return false;
  const DWORD flags = strict && cp.read_flag ? MB_ERR_INVALID_CHARS : 0;
  const int size = static_cast<int>(bytes.size());
  const int n = MultiByteToWideChar(cp.id, flags, bytes.data(), size, nullptr, 0);
  if (n <= 0) return false;
  out->resize(static_cast<size_t>(n));
  return MultiByteToWideChar(cp.id, flags, bytes.data(), size, out->data(), n) == n;
}

bool wide_to_mb(const CodePage& cp, std::wstring_view text, std::string* out, bool* used_default) {
  out->clear();
  *used_default = false;
  if (text.empty()) return true;
  if (text.size() > static_cast<size_t>(INT_MAX)) return false;
  const DWORD flags = cp.write_flags ? WC_NO_BEST_FIT_CHARS : 0;
  BOOL used = FALSE;
  BOOL* const used_out = cp.write_flags ? &used : nullptr;
  const int size = static_cast<int>(text.size());
  const int n = WideCharToMultiByte(cp.id, flags, text.data(), size, nullptr, 0, nullptr, used_out);
  if (n <= 0) return false;
  size_t capacity = static_cast<size_t>(n) + 16;
  for (;;) {
    capacity = std::min(capacity, static_cast<size_t>(INT_MAX));
    out->resize(capacity);
    used = FALSE;
    const int written = WideCharToMultiByte(cp.id, flags, text.data(), size, out->data(),
                                            static_cast<int>(capacity), nullptr, used_out);
    if (written > 0) {
      out->resize(static_cast<size_t>(written));
      *used_default = used != FALSE;
      return true;
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || capacity >= static_cast<size_t>(INT_MAX)) {
      out->clear();
      return false;
    }
    capacity *= 2;
  }
}

size_t dbcs_unit_length(Codec codec, unsigned char b) {
  if (codec == Codec::kEucJp) {
    if (b == 0x8E) return 2;
    if (b == 0x8F) return 3;
    return b >= 0xA1 && b <= 0xFE ? 2 : 1;
  }
  return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC) ? 2 : 1;
}

void raw_dbcs(std::string_view bytes, Codec codec, RawText* raw) {
  const CodePage& cp = code_page(codec);
  std::wstring w;
  std::string back;
  bool used = false;
  size_t pos = 0;
  while (pos < bytes.size()) {
    const size_t lf = bytes.find('\n', pos);
    const size_t seg_end = lf == std::string_view::npos ? bytes.size() : lf + 1;
    const std::string_view seg = bytes.substr(pos, seg_end - pos);
    if (mb_to_wide(cp, true, seg, &w) && wide_to_mb(cp, w, &back, &used) && back == seg) {
      raw->text += w;
      pos = seg_end;
      continue;
    }
    size_t i = 0;
    while (i < seg.size()) {
      const size_t len = std::min(dbcs_unit_length(codec, static_cast<unsigned char>(seg[i])), seg.size() - i);
      const std::string_view unit = seg.substr(i, len);
      if (mb_to_wide(cp, true, unit, &w) && !w.empty()) {
        if (!(wide_to_mb(cp, w, &back, &used) && back == unit)) raw->note(pos + i, raw->text.size(), false);
        raw->text += w;
        i += len;
      } else {
        raw->note(pos + i, raw->text.size(), true);
        raw->text += kReplacement;
        ++i;
      }
    }
    pos = seg_end;
  }
}

void raw_iso2022jp(std::string_view bytes, RawText* raw) {
  static constexpr std::string_view kEscapes[] = {"\x1b(B", "\x1b(J", "\x1b(I", "\x1b$@", "\x1b$B", "\x1b$(D"};
  const CodePage& cp = kIso2022JpPage;
  std::string_view state;
  std::wstring w;
  std::string back;
  std::string unit;
  bool used = false;
  size_t i = 0;
  while (i < bytes.size()) {
    const unsigned char b = static_cast<unsigned char>(bytes[i]);
    if (b == 0x1B) {
      const auto it = std::find_if(std::begin(kEscapes), std::end(kEscapes),
                                   [&](std::string_view e) { return bytes.substr(i, e.size()) == e; });
      if (it != std::end(kEscapes)) {
        state = *it == kEscapes[0] ? std::string_view() : *it;
        i += it->size();
        continue;
      }
    }
    if (b >= 0x80 || b == 0x1B) {
      raw->note(i, raw->text.size(), true);
      raw->text += kReplacement;
      ++i;
      continue;
    }
    if (state.empty() || b < 0x21 || b == 0x7F) {
      raw->text += static_cast<wchar_t>(b);
      ++i;
      continue;
    }
    const bool pair = state[1] == '$';
    const size_t len = pair ? 2 : 1;
    if (pair && (i + 1 >= bytes.size() || bytes[i + 1] < 0x21 || bytes[i + 1] > 0x7E)) {
      raw->note(i, raw->text.size(), true);
      raw->text += kReplacement;
      ++i;
      continue;
    }
    unit.assign(state);
    unit.append(bytes.substr(i, len));
    if (mb_to_wide(cp, false, unit, &w) && !w.empty()) {
      unit.append("\x1b(B");
      if (!(wide_to_mb(cp, w, &back, &used) && back == unit)) raw->note(i, raw->text.size(), false);
      raw->text += w;
    } else {
      raw->note(i, raw->text.size(), true);
      raw->text += kReplacement;
    }
    i += len;
  }
}

void raw_codepage(std::string_view bytes, Codec codec, RawText* raw) {
  const CodePage& cp = code_page(codec);
  std::string back;
  bool used = false;
  if (mb_to_wide(cp, true, bytes, &raw->text) && wide_to_mb(cp, raw->text, &back, &used) && back == bytes) {
    return;
  }
  raw->text.clear();
  if (codec == Codec::kIso2022Jp) {
    raw_iso2022jp(bytes, raw);
  } else {
    raw_dbcs(bytes, codec, raw);
  }
  if (raw->replaced) return;
  raw->bad = false;
  wide_to_mb(cp, raw->text, &back, &used);
  size_t at = 0;
  while (at < back.size() && at < bytes.size() && back[at] == bytes[at]) ++at;
  if (at == bytes.size() && back.size() == bytes.size()) return;
  std::wstring prefix;
  mb_to_wide(cp, false, bytes.substr(0, std::min(at, bytes.size())), &prefix);
  raw->note(std::min(at, bytes.size()), std::min(prefix.size(), raw->text.size()), false);
}

size_t lines_before(const std::wstring& text, size_t index) {
  size_t lines = 0;
  for (size_t i = 0; i < index && i < text.size(); ++i) {
    if (text[i] == L'\n' || (text[i] == L'\r' && (i + 1 >= text.size() || text[i + 1] != L'\n'))) ++lines;
  }
  return lines;
}

// finish_decoding of 368313b for raw code page text: the line ends are
// split by the (unchanged) UTF-16 path of decode_as, the rest is set as it
// set it. The code pages never give surrogates (checked).
DecodedText finish(RawText&& raw, Codec codec) {
  for (const wchar_t w : raw.text) CHECK(!(w >= 0xD800 && w <= 0xDFFF));
  std::string utf16le;
  utf16le.reserve(raw.text.size() * 2);
  for (const wchar_t w : raw.text) {
    utf16le.push_back(static_cast<char>(static_cast<unsigned>(w) & 0xFF));
    utf16le.push_back(static_cast<char>((static_cast<unsigned>(w) >> 8) & 0xFF));
  }
  FileFormat format;
  format.codec = Codec::kUtf16Le;
  DecodedText d = geditor::decode_as(utf16le, format);
  d.format.codec = codec;
  d.format.bom = false;
  d.format.undecodable = false;
  if (raw.bad) {
    d.format.undecodable = true;
    d.error_offset = raw.bad_byte;
    d.error_line = lines_before(raw.text, raw.bad_index);
    d.replaced = raw.replaced;
  }
  return d;
}

DecodedText decode_as(std::string_view bytes, Codec codec) {
  RawText raw;
  raw_codepage(bytes, codec, &raw);
  return finish(std::move(raw), codec);
}

// decode_detected of 368313b for bytes without a BOM and without NUL bytes
// (the branches before UTF-8 are unchanged and not taken here).
DecodedText decode_detected(std::string_view bytes) {
  FileFormat encoding;
  DecodedText utf8 = geditor::decode_as(bytes, encoding);
  if (!utf8.format.undecodable || utf8.status != geditor::DecodeStatus::kOk) return utf8;
  if (bytes.size() <= static_cast<size_t>(INT_MAX) &&
      MultiByteToWideChar(kShiftJisPage.id, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                          nullptr, 0) > 0) {
    return decode_as(bytes, Codec::kShiftJis);
  }
  utf8.detection_failed = true;
  return utf8;
}

}  // namespace head

// The first difference of two decodings ("" when they are the same).
std::string difference(const DecodedText& a, const DecodedText& b) {
  if (a.status != b.status) return "status";
  if (a.format.codec != b.format.codec) return "codec";
  if (a.format.bom != b.format.bom) return "bom";
  if (a.format.eol != b.format.eol) return "eol";
  if (a.format.undecodable != b.format.undecodable) return "undecodable";
  if (a.error_offset != b.error_offset) {
    return "error_offset " + std::to_string(a.error_offset) + " vs " + std::to_string(b.error_offset);
  }
  if (a.error_line != b.error_line) return "error_line";
  if (a.replaced != b.replaced) return "replaced";
  if (a.detection_failed != b.detection_failed) return "detection_failed";
  if (a.text != b.text) {
    size_t i = 0;
    while (i < a.text.size() && i < b.text.size() && a.text[i] == b.text[i]) ++i;
    return "text at " + std::to_string(i) + " (sizes " + std::to_string(a.text.size()) + " vs " +
           std::to_string(b.text.size()) + ")";
  }
  if (a.line_feeds != b.line_feeds) return "line_feeds";
  if (a.eol_runs.size() != b.eol_runs.size()) return "eol_runs";
  for (size_t i = 0; i < a.eol_runs.size(); ++i) {
    if (a.eol_runs[i].start != b.eol_runs[i].start || a.eol_runs[i].eol != b.eol_runs[i].eol) return "eol_runs";
  }
  for (size_t i = 0; i < 3; ++i) {
    if (a.eol_counts[i] != b.eol_counts[i]) return "eol_counts";
  }
  return "";
}

std::string hex(std::string_view bytes) {
  std::string s;
  char buf[4];
  for (size_t i = 0; i < bytes.size() && i < 96; ++i) {
    std::snprintf(buf, sizeof(buf), "%02X ", static_cast<unsigned>(static_cast<unsigned char>(bytes[i])));
    s += buf;
  }
  if (bytes.size() > 96) s += "...";
  return s;
}

struct Counter {
  size_t cases = 0;
  size_t different = 0;
};

// Compares the new and the HEAD reading of `bytes` (named `codec`, or
// detected when `detected`) with the stretch sizes of `stretches`.
void compare(Counter* counter, std::string_view bytes, Codec codec, bool detected,
             std::initializer_list<size_t> stretches) {
  const DecodedText old = detected ? head::decode_detected(bytes) : head::decode_as(bytes, codec);
  for (const size_t stretch : stretches) {
    geditor::set_code_page_stretch_for_testing(stretch);
    FileFormat format;
    format.codec = codec;
    const DecodedText now = detected ? geditor::decode_detected(bytes) : geditor::decode_as(bytes, format);
    ++counter->cases;
    const std::string why = difference(now, old);
    if (!why.empty()) {
      ++counter->different;
      if (counter->different <= 5) {
        ::test::report_failure(__FILE__, __LINE__,
                               "differs from HEAD: " + why + " (codec " + std::to_string(static_cast<int>(codec)) +
                                   (detected ? " detected" : " named") + ", stretch " + std::to_string(stretch) +
                                   ", " + std::to_string(bytes.size()) + " bytes: " + hex(bytes) + ")");
      }
    }
  }
  geditor::set_code_page_stretch_for_testing(0);
}

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

std::string encode(UINT cp, const std::wstring& s) {
  if (s.empty()) return {};
  const int n = WideCharToMultiByte(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
  WideCharToMultiByte(cp, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

// Japanese and ASCII text with CR, LF and CRLF line ends.
std::wstring random_text(Rng& rng, size_t units) {
  static const wchar_t* const kPieces[] = {
      L"abc", L" ", L"editor", L"\x65E5\x672C\x8A9E", L"\x30C6\x30AD\x30B9\x30C8", L"\x304B\x306A",
      L"\xFF83\xFF7D\xFF84",  // half-width katakana
      L"\x2460\x2161",        // NEC special characters (circled 1, Roman 2)
      L"\x7E8A",              // an IBM extension of 932
      L"\x00A5\x203E",        // yen, overline
      L"\t", L"0123456789", L"\x3000", L"\x2015\xFF5E"};
  static const wchar_t* const kEnds[] = {L"\n", L"\r\n", L"\r"};
  std::wstring s;
  while (s.size() < units) {
    if (rng.below(6) == 0) {
      s += kEnds[rng.below(3)];
    } else {
      s += kPieces[rng.below(std::size(kPieces))];
    }
  }
  return s;
}

// Bytes that a code page cannot read, or reads but writes back differently.
const std::vector<std::string>& defects(Codec codec) {
  static const std::vector<std::string> kShiftJis = {
      "\x81\x20", "\x81", "\xFF", "\x80", "\xA0", "\xFD", "\xED\x40", "\x81\n", "\x82\r", "\xFA\x40", "\x87\x40",
      "\xEF\xFC", "\x85\x40"};
  static const std::vector<std::string> kEucJp = {
      "\x80", "\x8E\x20", "\x8F\xA1\xA1", "\x8F", "\xA1", "\xA1\n", "\xFF\xFF", "\x8E\xE0", "\xA9\xA1", "\xAD\xA1",
      "\x8F\xB0\xA1", "\xFE\xFE"};
  static const std::vector<std::string> kIso2022Jp = {
      "\x80",       "\x1B",           "\x1B$B\x21",           "\x1B$@\x30\x21\x1B(B", "\x1B(J\x5C\x7E\x1B(B",
      "\x1B(I\x31", "\x1B$B\n\x30\x21", "\x1B(X",             "\x1B$(D\x30\x21",      "\x1B$B\x7F\x21",
      "\x1B$B\x2F\x21\x1B(B", "\x1B$B",  "\x1B(B\x1B(B",       "\x1B$B\x21\x80",       "\x1B(I\x0D\x0A\x31"};
  if (codec == Codec::kEucJp) return kEucJp;
  if (codec == Codec::kIso2022Jp) return kIso2022Jp;
  return kShiftJis;
}

UINT page_of(Codec codec) {
  return codec == Codec::kEucJp ? 20932u : codec == Codec::kIso2022Jp ? 50220u : 932u;
}

// Random texts with defects at random places (and at the start and the
// end), each read with stretches of 1 byte up to the default.
void compare_random(Counter* counter, Codec codec, uint64_t seed, size_t count, size_t max_units, bool detected) {
  Rng rng(seed);
  const std::vector<std::string>& bad = defects(codec);
  for (size_t n = 0; n < count; ++n) {
    std::string bytes = encode(page_of(codec), random_text(rng, rng.below(max_units) + 1));
    const size_t defect_count = rng.below(4);
    for (size_t k = 0; k < defect_count; ++k) {
      const size_t where = rng.below(3);
      const size_t at = where == 0 ? 0 : where == 1 ? bytes.size() : rng.below(bytes.size() + 1);
      bytes.insert(at, bad[rng.below(bad.size())]);
    }
    // ISO-2022-JP: sometimes without the final ESC ( B.
    if (codec == Codec::kIso2022Jp && rng.below(3) == 0 && bytes.size() >= 3 &&
        bytes.compare(bytes.size() - 3, 3, "\x1b(B") == 0) {
      bytes.resize(bytes.size() - 3);
    }
    if (detected) std::replace(bytes.begin(), bytes.end(), '\0', '\x01');
    compare(counter, bytes, codec, detected, {1, 2, 3, 7, 16, 61, 0});
  }
}

}  // namespace

TEST(encoding_stretches_fixed_cases_match_head) {
  Counter counter;
  for (const Codec codec : {Codec::kShiftJis, Codec::kEucJp, Codec::kIso2022Jp}) {
    const std::string valid = encode(page_of(codec), L"abc \x65E5\x672C\nx\r\ny\r\x30C6\x30AD\xFF83 z\n");
    compare(&counter, "", codec, false, {1, 0});
    compare(&counter, valid, codec, false, {1, 2, 5, 0});
    compare(&counter, "a\rb\nc\r\nd\r\r\n\n", codec, false, {1, 2, 0});
    for (const std::string& d : defects(codec)) {
      compare(&counter, d, codec, false, {1, 2, 0});
      compare(&counter, d + valid, codec, false, {1, 2, 5, 0});                         // at the start
      compare(&counter, valid + d + valid, codec, false, {1, 2, 5, 11, 0});             // in the middle
      compare(&counter, valid + d, codec, false, {1, 2, 5, 0});                         // at the end
      compare(&counter, valid + d + "\n" + d + "\r\n" + valid, codec, false, {1, 3, 0});  // twice
    }
  }
  // Write-back-only differences named in the contract.
  compare(&counter, "abc\n\xED\x40\ndef", Codec::kShiftJis, false, {1, 4, 0});
  compare(&counter, "abc\n\x1B$@\x30\x21\x1B(B\ndef", Codec::kIso2022Jp, false, {1, 4, 0});
  compare(&counter, "\x1B$B\x30\x21\x30\x22", Codec::kIso2022Jp, false, {1, 2, 0});  // no final ESC ( B
  std::printf("  [encoding stretches] fixed cases: %zu compared, %zu different\n", counter.cases, counter.different);
  CHECK_EQ(counter.different, size_t{0});
}

TEST(encoding_stretches_random_cases_match_head) {
  Counter counter;
  compare_random(&counter, Codec::kShiftJis, 0x5157, 300, 400, false);
  compare_random(&counter, Codec::kEucJp, 0xE0C, 300, 400, false);
  compare_random(&counter, Codec::kIso2022Jp, 0x150, 300, 400, false);
  // Longer texts over the default stretch of 64 KiB.
  compare_random(&counter, Codec::kShiftJis, 0x5158, 4, 150000, false);
  compare_random(&counter, Codec::kEucJp, 0xE0D, 4, 150000, false);
  compare_random(&counter, Codec::kIso2022Jp, 0x151, 3, 60000, false);
  std::printf("  [encoding stretches] random cases: %zu compared, %zu different\n", counter.cases,
              counter.different);
  CHECK_EQ(counter.different, size_t{0});
}

TEST(encoding_stretches_detection_matches_head) {
  Counter counter;
  // Shift_JIS, EUC-JP and ISO-2022-JP bytes with and without defects,
  // UTF-8 text, and random bytes, detected.
  compare_random(&counter, Codec::kShiftJis, 0xD1, 300, 300, true);
  compare_random(&counter, Codec::kEucJp, 0xD2, 150, 300, true);
  compare_random(&counter, Codec::kIso2022Jp, 0xD3, 100, 300, true);
  Rng rng(0xD4);
  for (size_t n = 0; n < 400; ++n) {
    std::string bytes;
    const size_t length = rng.below(64);
    for (size_t k = 0; k < length; ++k) bytes.push_back(static_cast<char>(rng.below(255) + 1));
    if (n % 2 == 0) {
      // Mostly valid UTF-8 with a defect.
      bytes = encode(CP_UTF8, random_text(rng, rng.below(80) + 1));
      if (n % 4 == 0) bytes.insert(rng.below(bytes.size() + 1), 1, static_cast<char>(rng.below(128) + 128));
    }
    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 || bytes.compare(0, 2, "\xFF\xFE") == 0 ||
        bytes.compare(0, 2, "\xFE\xFF") == 0) {
      bytes[0] = 'x';
    }
    compare(&counter, bytes, Codec::kUtf8, true, {0});
  }
  compare(&counter, "", Codec::kUtf8, true, {0});
  std::printf("  [encoding stretches] detection cases: %zu compared, %zu different\n", counter.cases,
              counter.different);
  CHECK_EQ(counter.different, size_t{0});
}

// Every double-byte character of 932 and 20932 (all lead and trail bytes,
// 64 per line) and every character of each ISO-2022-JP state in one run:
// a stretch read in one call gives what lines and characters alone give.
TEST(encoding_stretches_every_character_matches_head) {
  Counter counter;
  {
    std::string sjis;
    int n = 0;
    for (int lead = 0x81; lead <= 0xFC; ++lead) {
      if (lead > 0x9F && lead < 0xE0) continue;
      for (int trail = 0x40; trail <= 0xFC; ++trail) {
        if (trail == 0x7F) continue;
        sjis.push_back(static_cast<char>(lead));
        sjis.push_back(static_cast<char>(trail));
        if (++n % 64 == 0) sjis.push_back('\n');
      }
    }
    for (int b = 0x80; b <= 0xFF; ++b) sjis.push_back(static_cast<char>(b));
    compare(&counter, sjis, Codec::kShiftJis, false, {1, 100, 4096, 0});
  }
  {
    std::string euc;
    int n = 0;
    for (int lead = 0xA1; lead <= 0xFE; ++lead) {
      for (int trail = 0xA1; trail <= 0xFE; ++trail) {
        euc.push_back(static_cast<char>(lead));
        euc.push_back(static_cast<char>(trail));
        if (++n % 64 == 0) euc.push_back('\n');
      }
    }
    for (int b = 0xA1; b <= 0xDF; ++b) euc += std::string("\x8E") + static_cast<char>(b);
    euc += "\n\x8F\xA1\xA1\x8F\xB0\xA1\n\x80\x81\x8D\xA0\xFF";
    compare(&counter, euc, Codec::kEucJp, false, {1, 100, 4096, 0});
  }
  for (const char* escape : {"\x1B$B", "\x1B$@", "\x1B$(D"}) {
    std::string iso = escape;
    for (int first = 0x21; first <= 0x7E; ++first) {
      for (int second = 0x21; second <= 0x7E; ++second) {
        iso.push_back(static_cast<char>(first));
        iso.push_back(static_cast<char>(second));
      }
    }
    compare(&counter, iso + "\x1B(B", Codec::kIso2022Jp, false, {1, 100, 4096, 0});
    compare(&counter, iso, Codec::kIso2022Jp, false, {100, 0});
  }
  for (const char* escape : {"\x1B(I", "\x1B(J"}) {
    std::string iso = escape;
    for (int b = 0x21; b <= 0x7E; ++b) iso.push_back(static_cast<char>(b));
    compare(&counter, iso + "\x1B(B", Codec::kIso2022Jp, false, {1, 7, 0});
  }
  std::printf("  [encoding stretches] every-character cases: %zu compared, %zu different\n", counter.cases,
              counter.different);
  CHECK_EQ(counter.different, size_t{0});
}

// Detection skips the CP 932 read when a lead byte is not followed by a
// trail byte (encoding.cpp shift_jis_units): CP 932 reads none of these
// pairs, nor a lead byte at the end, with MB_ERR_INVALID_CHARS.
TEST(encoding_shift_jis_lead_without_trail_never_reads) {
  size_t pairs = 0;
  size_t read = 0;
  for (int lead = 0x81; lead <= 0xFC; ++lead) {
    if (lead > 0x9F && lead < 0xE0) continue;
    const char alone[] = {'a', static_cast<char>(lead)};
    if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, alone, 2, nullptr, 0) > 0) ++read;
    ++pairs;
    for (int trail = 0; trail <= 0xFF; ++trail) {
      if ((trail >= 0x40 && trail <= 0x7E) || (trail >= 0x80 && trail <= 0xFC)) continue;
      const char pair[] = {static_cast<char>(lead), static_cast<char>(trail), 'a'};
      if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, pair, 3, nullptr, 0) > 0) ++read;
      ++pairs;
    }
  }
  std::printf("  [encoding stretches] %zu lead bytes without a trail byte, %zu read\n", pairs, read);
  CHECK_EQ(read, size_t{0});
}

// Every control byte (0x00-0x20, 0x7F) and every byte of 0x80-0xDF in each
// of the six ISO-2022-JP states, before, between and after characters of
// the state and next to other escapes, with stretches of 1 byte up to the
// default (retry R1: code page 50220 goes back to ASCII at a NUL in the
// ESC ( I state, which a stretch read in one call must not hide).
TEST(encoding_stretches_iso2022jp_control_bytes_in_every_state_match_head) {
  Counter counter;
  struct State {
    const char* escape;
    const char* chars[3];  // characters of the state
  };
  const State states[] = {
      {"\x1B(B", {"x", "H", "["}},
      {"\x1B(J", {"8", "H", "\\"}},
      {"\x1B(I", {"8", "H", "["}},
      {"\x1B$@", {"\x30\x21", "\x48\x5B", "\x24\x22"}},
      {"\x1B$B", {"\x30\x21", "\x48\x5B", "\x24\x22"}},
      {"\x1B$(D", {"\x30\x21", "\x48\x5B", "\x22\x2F"}},
  };
  // The case of the review, as it was found (HEAD: ... FF78 0000 FF88 FF9B).
  {
    const std::string review("\x6C\x76\x45\x40\x1B\x28\x49\x1B\x28\x49\x1B\x28\x49\x38\x00\x48\x5B", 17);
    compare(&counter, review, Codec::kIso2022Jp, false, {1, 2, 3, 4, 5, 8, 13, 0});
    FileFormat format;
    format.codec = Codec::kIso2022Jp;
    const DecodedText d = geditor::decode_as(review, format);
    CHECK_EQ(d.text, std::wstring(L"lvE@\xFF78\0\xFF88\xFF9B", 8));
  }
  std::vector<int> bytes;
  for (int b = 0x00; b <= 0x20; ++b) bytes.push_back(b);
  bytes.push_back(0x7F);
  for (int b = 0x80; b <= 0xDF; ++b) bytes.push_back(b);
  for (const State& s : states) {
    const std::string e = s.escape;
    const std::string u1 = s.chars[0];
    const std::string u2 = s.chars[1];
    const std::string u3 = s.chars[2];
    for (const int b : bytes) {
      const std::string c(1, static_cast<char>(b));
      const std::string layouts[] = {
          "lvE@" + e + u1 + c + u2 + u3 + "\x1B(B" + "ab",
          e + c + u1 + u2,
          e + u1 + u2 + c,
          e + e + e + u1 + c + u2 + u3 + c + "\n" + u1,
      };
      for (const std::string& input : layouts) {
        compare(&counter, input, Codec::kIso2022Jp, false, {1, 2, 3, 4, 5, 8, 13, 0});
      }
      // Next to an escape to each state.
      for (const State& other : states) {
        const std::string input =
            "ab" + e + u1 + c + c + u2 + other.escape + other.chars[0] + c + other.chars[1] + "\x1B(B" + "z";
        compare(&counter, input, Codec::kIso2022Jp, false, {1, 3, 4, 8, 0});
      }
    }
  }
  std::printf("  [encoding stretches] ISO-2022-JP control bytes x states: %zu compared, %zu different\n",
              counter.cases, counter.different);
  CHECK_EQ(counter.different, size_t{0});
}
