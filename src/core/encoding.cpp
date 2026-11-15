#include "encoding.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <climits>
#include <cstring>

#include "commands.h"
#include "unicode.h"

namespace geditor {
namespace {

constexpr uint64_t kHighs = 0x8080808080808080ull;
constexpr wchar_t kReplacement = 0xFFFD;
constexpr size_t kMaxUnits = 0xFFFFFFFFull;  // line_feeds hold uint32_t offsets

bool is_cont(unsigned char b) {
  return (b & 0xC0) == 0x80;
}

// UTF-16 decoded from the bytes, "\r" and "\n" as they are, with the first
// problem: a byte that could not be read (`replaced`: it became U+FFFD) or
// that the text does not give back.
struct RawText {
  std::wstring text;
  bool too_large = false;
  bool bad = false;
  bool replaced = false;  // some byte became U+FFFD
  size_t bad_byte = 0;
  size_t bad_index = 0;  // index in `text` of the first problem

  void note(size_t byte, size_t index, bool replacement) {
    replaced = replaced || replacement;
    if (bad) return;
    bad = true;
    bad_byte = byte;
    bad_index = index;
  }
};

// ---- UTF-8 ----

// Invalid bytes become U+FFFD, one for each maximal subpart of a sequence
// (Unicode 3.9, "U+FFFD substitution of maximal subparts").
void raw_utf8(std::string_view bytes, size_t skip, RawText* raw) {
  const unsigned char* const begin = reinterpret_cast<const unsigned char*>(bytes.data());
  const unsigned char* p = begin + skip;
  const unsigned char* const end = begin + bytes.size();
  // UTF-16 units of valid input: one per lead byte, two for 4-byte
  // sequences. Eight bytes at a time: continuation bytes are 10xxxxxx,
  // 4-byte leads 1111xxxx.
  size_t units = static_cast<size_t>(end - p);
  const unsigned char* q = p;
  for (; end - q >= 8; q += 8) {
    uint64_t v;
    std::memcpy(&v, q, 8);
    const uint64_t cont = v & ~(v << 1) & kHighs;
    const uint64_t lead4 = v & (v << 1) & (v << 2) & (v << 3) & kHighs;
    units -= static_cast<size_t>(std::popcount(cont));
    units += static_cast<size_t>(std::popcount(lead4));
  }
  for (; q < end; ++q) {
    units -= (*q & 0xC0) == 0x80 ? 1 : 0;
    units += *q >= 0xF0 ? 1 : 0;
  }
  if (units >= kMaxUnits) {
    raw->too_large = true;
    return;
  }
  std::wstring& text = raw->text;
  text.resize(units);
  wchar_t* out = text.data();
  while (p < end) {
    const unsigned char b = *p;
    if (b < 0x80) {
      // ASCII run: 8 bytes at a time, then byte by byte.
      while (end - p >= 8) {
        uint64_t v;
        std::memcpy(&v, p, 8);
        if ((v & kHighs) != 0) break;
        for (int i = 0; i < 8; ++i) out[i] = static_cast<wchar_t>(p[i]);
        out += 8;
        p += 8;
      }
      while (p < end && *p < 0x80) *out++ = static_cast<wchar_t>(*p++);
      continue;
    }
    // Common 3-byte sequences (U+1000..U+CFFF, U+E000..U+FFFF) need no
    // range check beyond the continuation bytes.
    if (b >= 0xE1 && b <= 0xEF && b != 0xED && end - p >= 3 && is_cont(p[1]) && is_cont(p[2])) {
      *out++ = static_cast<wchar_t>((static_cast<unsigned>(b & 0x0F) << 12) |
                                    (static_cast<unsigned>(p[1] & 0x3F) << 6) | (p[2] & 0x3F));
      p += 3;
      continue;
    }
    char32_t c = 0;
    size_t n = 0;
    bool ok = false;
    // The range of the second byte after this lead.
    const unsigned char low = b == 0xE0 ? 0xA0 : b == 0xF0 ? 0x90 : 0x80;
    const unsigned char high = b == 0xED ? 0x9F : b == 0xF4 ? 0x8F : 0xBF;
    if (b >= 0xC2 && b <= 0xDF) {
      n = 2;
      ok = end - p >= 2 && is_cont(p[1]);
      if (ok) c = (static_cast<char32_t>(b & 0x1F) << 6) | (p[1] & 0x3F);
    } else if (b >= 0xE0 && b <= 0xEF) {
      n = 3;
      if (end - p >= 3 && is_cont(p[1]) && is_cont(p[2])) {
        ok = p[1] >= low && p[1] <= high;
        c = (static_cast<char32_t>(b & 0x0F) << 12) | (static_cast<char32_t>(p[1] & 0x3F) << 6) |
            (p[2] & 0x3F);
      }
    } else if (b >= 0xF0 && b <= 0xF4) {
      n = 4;
      if (end - p >= 4 && is_cont(p[1]) && is_cont(p[2]) && is_cont(p[3])) {
        ok = p[1] >= low && p[1] <= high;
        c = (static_cast<char32_t>(b & 0x07) << 18) | (static_cast<char32_t>(p[1] & 0x3F) << 12) |
            (static_cast<char32_t>(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      }
    }
    if (!ok) {
      // The maximal subpart: the lead and the continuation bytes that can
      // still follow it.
      size_t sub = 1;
      if (n >= 3 && end - p >= 2 && p[1] >= low && p[1] <= high) {
        sub = 2;
        if (n == 4 && end - p >= 3 && is_cont(p[2])) sub = 3;
      }
      const size_t at = static_cast<size_t>(out - text.data());
      if (!raw->bad) {
        // Invalid input may need more units than counted: one per byte left.
        text.resize(std::max(text.size(), at + static_cast<size_t>(end - p)));
        out = text.data() + at;
      }
      raw->note(static_cast<size_t>(p - begin), at, true);
      *out++ = kReplacement;
      p += sub;
      continue;
    }
    if (c < 0x10000) {
      *out++ = static_cast<wchar_t>(c);
    } else {
      const char32_t v = c - 0x10000;
      *out++ = static_cast<wchar_t>(0xD800 + (v >> 10));
      *out++ = static_cast<wchar_t>(0xDC00 + (v & 0x3FF));
    }
    p += n;
  }
  text.resize(static_cast<size_t>(out - text.data()));
}

// ---- UTF-16 ----

// A lone surrogate and an odd last byte become U+FFFD.
void raw_utf16(std::string_view bytes, size_t skip, bool big_endian, RawText* raw) {
  const size_t size = bytes.size() - skip;
  const size_t count = size / 2;
  const bool odd = size % 2 != 0;
  if (count + (odd ? 1 : 0) >= kMaxUnits) {
    raw->too_large = true;
    return;
  }
  const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data()) + skip;
  std::wstring& text = raw->text;
  text.resize(count + (odd ? 1 : 0));
  wchar_t* out = text.data();
  const int hi = big_endian ? 0 : 1;
  const int lo = 1 - hi;
  for (size_t i = 0; i < count; ++i) {
    out[i] = static_cast<wchar_t>((p[2 * i + hi] << 8) | p[2 * i + lo]);
  }
  for (size_t i = 0; i < count; ++i) {
    const wchar_t w = out[i];
    if (w < 0xD800 || w > 0xDFFF) continue;
    if (is_high_surrogate(w) && i + 1 < count && is_low_surrogate(out[i + 1])) {
      ++i;
      continue;
    }
    raw->note(skip + 2 * i, i, true);
    out[i] = kReplacement;
  }
  if (odd) {
    raw->note(skip + 2 * count, count, true);
    out[count] = kReplacement;
  }
}

// ---- Code pages (Shift_JIS, EUC-JP, ISO-2022-JP) ----

// The flags each code page accepts, measured on Windows 11 (encoding.h).
struct CodePage {
  UINT id;
  bool read_flag;    // MB_ERR_INVALID_CHARS
  bool write_flags;  // WC_NO_BEST_FIT_CHARS and lpUsedDefaultChar
};

constexpr CodePage kShiftJisPage{932, true, true};
constexpr CodePage kEucJpPage{20932, true, true};
constexpr CodePage kIso2022JpPage{50220, false, false};

const CodePage& code_page(Codec codec) {
  if (codec == Codec::kEucJp) return kEucJpPage;
  if (codec == Codec::kIso2022Jp) return kIso2022JpPage;
  return kShiftJisPage;
}

// MultiByteToWideChar. `strict` adds MB_ERR_INVALID_CHARS where the code page
// accepts it; false when the bytes are invalid then, or the call fails.
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

// WideCharToMultiByte with WC_NO_BEST_FIT_CHARS and lpUsedDefaultChar where
// the code page accepts them (`used_default`: a character had no bytes).
// The size query alone is not enough (encoding.h: on 932, when the text ends
// in one of 12 characters, converting into a buffer of exactly the queried
// size fails with ERROR_INSUFFICIENT_BUFFER): the conversion gets room to spare
// and grows it on that error. Not enough alone (50220 accepts no flags):
// every caller reads the bytes back and compares them (encode_text: with
// the text; raw_codepage: with the file). False when the call still fails.
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

// The bytes of one character of a double-byte code page from its first byte.
size_t dbcs_unit_length(Codec codec, unsigned char b) {
  if (codec == Codec::kEucJp) {
    if (b == 0x8E) return 2;
    if (b == 0x8F) return 3;
    return b >= 0xA1 && b <= 0xFE ? 2 : 1;
  }
  return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC) ? 2 : 1;
}

// Shift_JIS and EUC-JP after the whole file failed: line by line (0x0A is
// never part of a double-byte character), and character by character in a
// line that fails. A character that cannot be read becomes U+FFFD (one per
// byte); one that reads but is written back differently is kept and noted.
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

// ISO-2022-JP after the whole file failed: the escape sequences of code
// page 50220 change the state; in a double-byte state two bytes of
// 0x21..0x7E make a character. Bytes of 0x80 and above, unknown escape
// sequences and a half pair become U+FFFD; a character that reads but is
// not written back as the same bytes (followed by ESC ( B) is kept and noted.
void raw_iso2022jp(std::string_view bytes, RawText* raw) {
  static constexpr std::string_view kEscapes[] = {"\x1b(B", "\x1b(J", "\x1b(I", "\x1b$@", "\x1b$B", "\x1b$(D"};
  const CodePage& cp = kIso2022JpPage;
  std::string_view state;  // empty: ASCII
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
  if (bytes.size() > static_cast<size_t>(INT_MAX)) {
    raw->too_large = true;
    return;
  }
  const CodePage& cp = code_page(codec);
  std::string back;
  bool used = false;
  // The whole file at once: it reads and gives the same bytes back.
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
  // Every byte was read: the first problem is the first byte that the text
  // does not give back (for ISO-2022-JP it may lie in an escape sequence or
  // between characters).
  raw->bad = false;
  wide_to_mb(cp, raw->text, &back, &used);
  size_t at = 0;
  while (at < back.size() && at < bytes.size() && back[at] == bytes[at]) ++at;
  if (at == bytes.size() && back.size() == bytes.size()) return;  // the same bytes after all
  std::wstring prefix;
  mb_to_wide(cp, false, bytes.substr(0, std::min(at, bytes.size())), &prefix);
  raw->note(std::min(at, bytes.size()), std::min(prefix.size(), raw->text.size()), false);
}

// ---- Line ends ----

// The 0-based line of index `index` of raw text ("\r\n", "\r" and "\n" each
// end a line).
size_t lines_before(const std::wstring& text, size_t index) {
  size_t lines = 0;
  for (size_t i = 0; i < index && i < text.size(); ++i) {
    if (text[i] == L'\n' || (text[i] == L'\r' && (i + 1 >= text.size() || text[i + 1] != L'\n'))) ++lines;
  }
  return lines;
}

// Splits the line ends of result->text (raw UTF-16) in place: each "\r\n",
// "\r" and "\n" becomes one '\n', with its offset, run and count. Every
// decode_* function ends here (finish_decoding).
void split_line_ends(DecodedText* result) {
  std::wstring& s = result->text;
  wchar_t* const base = s.data();
  const size_t n = s.size();
  result->line_feeds.reserve(n / 64 + 16);
  result->eol_runs.push_back(EolRun{0, Eol::kLf});
  bool any_line_end = false;
  size_t after_last_line_end = 0;
  size_t w = 0;
  auto line_end = [&](Eol kind) {
    result->line_feeds.push_back(static_cast<uint32_t>(w));
    ++result->eol_counts[static_cast<size_t>(kind)];
    if (!any_line_end) {
      result->eol_runs.back().eol = kind;
      any_line_end = true;
    } else if (result->eol_runs.back().eol != kind) {
      result->eol_runs.push_back(EolRun{after_last_line_end, kind});
    }
    after_last_line_end = w + 1;
    base[w++] = L'\n';
  };
  size_t r = 0;
  while (r < n) {
    // Four units at a time while none is below 0x0E ("\r" is 0x0D, "\n" 0x0A).
    while (n - r >= 4) {
      uint64_t v;
      std::memcpy(&v, base + r, 8);
      if (((v - 0x000E000E000E000Eull) & ~v & 0x8000800080008000ull) != 0) break;
      if (w != r) std::memcpy(base + w, &v, 8);
      r += 4;
      w += 4;
    }
    if (r >= n) break;
    const wchar_t c = base[r];
    if (c == L'\n') {
      line_end(Eol::kLf);
      ++r;
    } else if (c == L'\r') {
      if (r + 1 < n && base[r + 1] == L'\n') {
        line_end(Eol::kCrLf);
        r += 2;
      } else {
        line_end(Eol::kCr);
        ++r;
      }
    } else {
      base[w++] = c;
      ++r;
    }
  }
  s.resize(w);
  result->format.eol = default_eol_of(result->eol_counts, Eol::kLf);
}

DecodedText finish_decoding(RawText&& raw, const FileFormat& encoding) {
  DecodedText result;
  result.format.codec = encoding.codec;
  result.format.bom = is_unicode(encoding.codec) && encoding.bom;
  if (raw.too_large || raw.text.size() >= kMaxUnits) {
    result.status = DecodeStatus::kTooLarge;
    return result;
  }
  if (raw.bad) {
    result.format.undecodable = true;
    result.error_offset = raw.bad_byte;
    result.error_line = lines_before(raw.text, raw.bad_index);
    result.replaced = raw.replaced;
  }
  result.text = std::move(raw.text);
  split_line_ends(&result);
  return result;
}

bool starts_with(std::string_view bytes, std::string_view prefix) {
  return bytes.substr(0, prefix.size()) == prefix;
}

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kUtf16LeBom = "\xFF\xFE";
constexpr std::string_view kUtf16BeBom = "\xFE\xFF";

// PLAN 2 "UTF-16 without BOM": NUL bytes (in the first 64 KiB) at odd
// offsets give UTF-16LE (ASCII "a" is 61 00), at even ones UTF-16BE, when
// that side has at least 2 NUL bytes, at least one for every 4 units
// (2-byte pairs) of the sample, and more than 4 times the other side. A few
// NUL bytes in a UTF-8 or ASCII file are not enough; BOM-less UTF-16 of 2 or
// more ASCII characters is (one ASCII character, "a" 00, is read as UTF-8).
bool utf16_by_nul_bias(std::string_view bytes, bool* big_endian) {
  const size_t n = std::min<size_t>(bytes.size(), 65536) & ~size_t{1};
  size_t even = 0;
  size_t odd = 0;
  for (size_t i = 0; i < n; ++i) {
    if (bytes[i] == '\0') ++((i & 1) != 0 ? odd : even);
  }
  const size_t units = n / 2;
  auto biased = [units](size_t side, size_t other) { return side >= 2 && side * 4 >= units && side > 4 * other; };
  if (biased(odd, even)) {
    *big_endian = false;
    return true;
  }
  if (biased(even, odd)) {
    *big_endian = true;
    return true;
  }
  return false;
}

// The codec and BOM of each name of encoding_names() (commands.cpp), in its
// order, and the mode line names.
struct EncodingEntry {
  Codec codec;
  bool bom;
  const wchar_t* display;
};

constexpr EncodingEntry kEncodings[] = {
    {Codec::kUtf8, false, L"UTF-8"},           {Codec::kUtf8, true, L"UTF-8-BOM"},
    {Codec::kUtf16Le, false, L"UTF-16LE"},     {Codec::kUtf16Le, true, L"UTF-16LE-BOM"},
    {Codec::kUtf16Be, false, L"UTF-16BE"},     {Codec::kUtf16Be, true, L"UTF-16BE-BOM"},
    {Codec::kShiftJis, false, L"Shift_JIS"},   {Codec::kEucJp, false, L"EUC-JP"},
    {Codec::kIso2022Jp, false, L"ISO-2022-JP"},
};

size_t encoding_index(const FileFormat& format) {
  const bool bom = is_unicode(format.codec) && format.bom;
  for (size_t i = 0; i < std::size(kEncodings); ++i) {
    if (kEncodings[i].codec == format.codec && kEncodings[i].bom == bom) return i;
  }
  return 0;
}

}  // namespace

bool is_unicode(Codec codec) {
  return codec == Codec::kUtf8 || codec == Codec::kUtf16Le || codec == Codec::kUtf16Be;
}

bool parse_encoding(std::wstring_view name, FileFormat* format) {
  const std::span<const wchar_t* const> names = encoding_names();
  for (size_t i = 0; i < names.size() && i < std::size(kEncodings); ++i) {
    if (name == names[i]) {
      format->codec = kEncodings[i].codec;
      format->bom = kEncodings[i].bom;
      return true;
    }
  }
  return false;
}

const wchar_t* encoding_setting_name(const FileFormat& format) {
  const std::span<const wchar_t* const> names = encoding_names();
  const size_t i = encoding_index(format);
  return i < names.size() ? names[i] : L"utf-8";
}

std::wstring unknown_encoding_message(std::wstring_view name) {
  std::wstring message = L"Unknown encoding: " + std::wstring(name) + L" (use ";
  bool first = true;
  for (const wchar_t* n : encoding_names()) {
    if (!first) message += L", ";
    message += n;
    first = false;
  }
  return message + L")";
}

DecodedText decode_as(std::string_view bytes, const FileFormat& encoding) {
  RawText raw;
  switch (encoding.codec) {
    case Codec::kUtf8:
      raw_utf8(bytes, encoding.bom && starts_with(bytes, kUtf8Bom) ? kUtf8Bom.size() : 0, &raw);
      break;
    case Codec::kUtf16Le:
      raw_utf16(bytes, encoding.bom && starts_with(bytes, kUtf16LeBom) ? kUtf16LeBom.size() : 0, false, &raw);
      break;
    case Codec::kUtf16Be:
      raw_utf16(bytes, encoding.bom && starts_with(bytes, kUtf16BeBom) ? kUtf16BeBom.size() : 0, true, &raw);
      break;
    case Codec::kShiftJis:
    case Codec::kEucJp:
    case Codec::kIso2022Jp:
      raw_codepage(bytes, encoding.codec, &raw);
      break;
  }
  return finish_decoding(std::move(raw), encoding);
}

DecodedText decode_utf8(std::string_view bytes) {
  FileFormat encoding;
  encoding.bom = starts_with(bytes, kUtf8Bom);
  return decode_as(bytes, encoding);
}

DecodedText decode_detected(std::string_view bytes) {
  FileFormat encoding;
  if (starts_with(bytes, kUtf8Bom)) {
    encoding.bom = true;
    return decode_as(bytes, encoding);
  }
  if (starts_with(bytes, kUtf16LeBom) || starts_with(bytes, kUtf16BeBom)) {
    encoding.codec = starts_with(bytes, kUtf16LeBom) ? Codec::kUtf16Le : Codec::kUtf16Be;
    encoding.bom = true;
    return decode_as(bytes, encoding);
  }
  bool big_endian = false;
  if (utf16_by_nul_bias(bytes, &big_endian)) {
    encoding.codec = big_endian ? Codec::kUtf16Be : Codec::kUtf16Le;
    return decode_as(bytes, encoding);
  }
  DecodedText utf8 = decode_as(bytes, encoding);
  if (!utf8.format.undecodable || utf8.status != DecodeStatus::kOk) return utf8;
  // Shift_JIS when code page 932 reads every byte (a size query only).
  if (bytes.size() <= static_cast<size_t>(INT_MAX) &&
      MultiByteToWideChar(kShiftJisPage.id, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                          nullptr, 0) > 0) {
    encoding.codec = Codec::kShiftJis;
    return decode_as(bytes, encoding);
  }
  utf8.detection_failed = true;
  return utf8;
}

Eol default_eol_of(const size_t counts[3], Eol none) {
  const size_t lf = counts[static_cast<size_t>(Eol::kLf)];
  const size_t crlf = counts[static_cast<size_t>(Eol::kCrLf)];
  const size_t cr = counts[static_cast<size_t>(Eol::kCr)];
  if (lf == 0 && crlf == 0 && cr == 0) return none;
  // Ties: CRLF > LF > CR.
  if (crlf >= lf && crlf >= cr) return Eol::kCrLf;
  if (lf >= cr) return Eol::kLf;
  return Eol::kCr;
}

PieceTree make_piece_tree(DecodedText& decoded) {
  return PieceTree(std::move(decoded.text), std::move(decoded.line_feeds), decoded.eol_runs);
}

std::string encode_utf8(const PieceTree& tree, const FileFormat& format) {
  std::string out;
  out.reserve(tree.length() + tree.eol_count(Eol::kCrLf) + 3);
  if (format.bom) out.append("\xEF\xBB\xBF");
  wchar_t pending_high = 0;
  auto put = [&out](char32_t c) {
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (c >> 18)));
      out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  };
  tree.for_each_chunk_eol(0, tree.length(), [&](std::wstring_view chunk, Eol eol) {
    for (const wchar_t w : chunk) {
      if (pending_high != 0) {
        if (is_low_surrogate(w)) {
          put(combine_surrogates(pending_high, w));
          pending_high = 0;
          continue;
        }
        put(0xFFFD);
        pending_high = 0;
      }
      if (w < 0x80) {
        if (w == L'\n') {
          if (eol == Eol::kLf) {
            out.push_back('\n');
          } else if (eol == Eol::kCrLf) {
            out.append("\r\n");
          } else {
            out.push_back('\r');
          }
          continue;
        }
        out.push_back(static_cast<char>(w));
      } else if (is_high_surrogate(w)) {
        pending_high = w;
      } else if (is_low_surrogate(w)) {
        put(0xFFFD);
      } else {
        put(static_cast<char32_t>(w));
      }
    }
    return true;
  });
  if (pending_high != 0) put(0xFFFD);
  return out;
}

namespace {

// UTF-16 with each piece's line end; a lone surrogate is written as U+FFFD
// (as encode_utf8 does).
std::string encode_utf16(const PieceTree& tree, bool bom, bool big_endian) {
  std::string out;
  out.reserve((tree.length() + tree.eol_count(Eol::kCrLf) + 1) * 2);
  auto put = [&out, big_endian](wchar_t w) {
    const char high = static_cast<char>((static_cast<unsigned>(w) >> 8) & 0xFF);
    const char low = static_cast<char>(static_cast<unsigned>(w) & 0xFF);
    out.push_back(big_endian ? high : low);
    out.push_back(big_endian ? low : high);
  };
  if (bom) put(0xFEFF);
  wchar_t pending_high = 0;
  tree.for_each_chunk_eol(0, tree.length(), [&](std::wstring_view chunk, Eol eol) {
    for (const wchar_t w : chunk) {
      if (pending_high != 0) {
        if (is_low_surrogate(w)) {
          put(pending_high);
          put(w);
          pending_high = 0;
          continue;
        }
        put(kReplacement);
        pending_high = 0;
      }
      if (w == L'\n') {
        if (eol != Eol::kLf) put(L'\r');
        if (eol != Eol::kCr) put(L'\n');
      } else if (is_high_surrogate(w)) {
        pending_high = w;
      } else {
        put(is_low_surrogate(w) ? kReplacement : w);
      }
    }
    return true;
  });
  if (pending_high != 0) put(kReplacement);
  return out;
}

// The text with each '\n' written as its piece's line end.
std::wstring text_with_line_ends(const PieceTree& tree) {
  std::wstring out;
  out.reserve(tree.length() + tree.eol_count(Eol::kCrLf));
  tree.for_each_chunk_eol(0, tree.length(), [&](std::wstring_view chunk, Eol eol) {
    if (eol == Eol::kLf) {
      out.append(chunk);
      return true;
    }
    for (const wchar_t w : chunk) {
      if (w != L'\n') {
        out.push_back(w);
      } else if (eol == Eol::kCrLf) {
        out.append(L"\r\n");
      } else {
        out.push_back(L'\r');
      }
    }
    return true;
  });
  return out;
}

// The units of the character (one, or a surrogate pair) at `index`.
size_t char_units(std::wstring_view text, size_t index) {
  return is_high_surrogate(text[index]) && index + 1 < text.size() && is_low_surrogate(text[index + 1]) ? 2 : 1;
}

// The character at `index`, written alone, comes back as itself.
bool writes_back(const CodePage& cp, std::wstring_view text, size_t index) {
  const std::wstring_view c = text.substr(index, char_units(text, index));
  std::string bytes;
  std::wstring back;
  bool used = false;
  return wide_to_mb(cp, c, &bytes, &used) && !used && mb_to_wide(cp, false, bytes, &back) && back == c;
}

// The index of the first character that does not come back when written
// alone; text.size() when there is none.
size_t first_unwritable(const CodePage& cp, std::wstring_view text) {
  for (size_t i = 0; i < text.size(); i += char_units(text, i)) {
    if (!writes_back(cp, text, i)) return i;
  }
  return text.size();
}

// The tree offset of index `index` of text_with_line_ends(tree).
size_t tree_offset_of(const PieceTree& tree, size_t index) {
  size_t offset = 0;
  size_t at = 0;
  tree.for_each_chunk_eol(0, tree.length(), [&](std::wstring_view chunk, Eol eol) {
    for (const wchar_t w : chunk) {
      const size_t units = w == L'\n' && eol == Eol::kCrLf ? 2 : 1;
      if (index < at + units) return false;
      at += units;
      ++offset;
    }
    return true;
  });
  return std::min(offset, tree.length());
}

}  // namespace

EncodedText encode_text(const PieceTree& tree, const FileFormat& format) {
  EncodedText result;
  switch (format.codec) {
    case Codec::kUtf8:
      result.bytes = encode_utf8(tree, format);
      return result;
    case Codec::kUtf16Le:
    case Codec::kUtf16Be:
      result.bytes = encode_utf16(tree, format.bom, format.codec == Codec::kUtf16Be);
      return result;
    case Codec::kShiftJis:
    case Codec::kEucJp:
    case Codec::kIso2022Jp:
      break;
  }
  const CodePage& cp = code_page(format.codec);
  const std::wstring text = text_with_line_ends(tree);
  if (text.size() > static_cast<size_t>(INT_MAX)) {
    result.ok = false;
    result.too_large = true;
    return result;
  }
  // Written, then read back: a character without bytes (lpUsedDefaultChar,
  // where the code page reports it) or one written as another one (best
  // fit) does not come back the same.
  bool used = false;
  std::wstring back;
  const bool converted = wide_to_mb(cp, text, &result.bytes, &used) && mb_to_wide(cp, false, result.bytes, &back);
  if (converted && !used && back == text) return result;
  result.ok = false;
  result.bytes.clear();
  // The first character that does not come back: where the read-back text
  // first differs, when that character alone does not come back; else (the
  // conversion call failed, or the difference lies elsewhere) the first one
  // found character by character.
  size_t i = 0;
  if (converted) {
    while (i < text.size() && i < back.size() && text[i] == back[i]) ++i;
    if (i > 0 && i < text.size() && is_low_surrogate(text[i]) && is_high_surrogate(text[i - 1])) --i;
  }
  if (!converted || i >= text.size() || writes_back(cp, text, i)) {
    i = first_unwritable(cp, text);
    if (i >= text.size()) {
      result.conversion_failed = true;
      i = 0;
    }
  }
  result.bad_offset = tree_offset_of(tree, i);
  return result;
}

size_t cr_kept_line_ends(const PieceTree& tree, PieceList* pieces) {
  pieces->clear();
  const PieceList in = tree.pieces();
  size_t changed = 0;
  size_t offset = 0;
  bool after_cr = false;  // the text so far ends with a CR line end
  for (const Piece& p : in) {
    if (p.length == 0) {
      pieces->push_back(p);
      continue;
    }
    if (after_cr && p.eol == Eol::kLf && p.line_feeds > 0) {
      // Only the '\n' at the start of the piece follow the CR directly.
      size_t lead = 0;
      while (lead < p.length && tree.char_at(offset + lead) == L'\n') ++lead;
      if (lead > 0) {
        Piece head = p;
        head.length = lead;
        head.line_feeds = lead;
        head.eol = Eol::kCr;
        pieces->push_back(head);
        changed += lead;
        if (lead < p.length) {
          Piece tail = p;
          tail.start += lead;
          tail.length -= lead;
          tail.line_feeds -= lead;
          pieces->push_back(tail);
        }
        // A piece of line ends only: the text still ends with a CR.
        after_cr = lead == p.length;
        offset += p.length;
        continue;
      }
    }
    pieces->push_back(p);
    after_cr = p.eol == Eol::kCr && tree.char_at(offset + p.length - 1) == L'\n';
    offset += p.length;
  }
  if (changed == 0) pieces->clear();
  return changed;
}

const wchar_t* eol_name(Eol eol) {
  switch (eol) {
    case Eol::kCrLf: return L"CRLF";
    case Eol::kCr: return L"CR";
    case Eol::kLf: break;
  }
  return L"LF";
}

const wchar_t* eol_setting_name(Eol eol) {
  switch (eol) {
    case Eol::kCrLf: return L"crlf";
    case Eol::kCr: return L"cr";
    case Eol::kLf: break;
  }
  return L"lf";
}

bool parse_eol(std::wstring_view name, Eol* eol) {
  for (const Eol e : {Eol::kLf, Eol::kCrLf, Eol::kCr}) {
    if (name == eol_setting_name(e)) {
      *eol = e;
      return true;
    }
  }
  return false;
}

const wchar_t* encoding_name(const FileFormat& format) {
  return kEncodings[encoding_index(format)].display;
}

}  // namespace geditor
