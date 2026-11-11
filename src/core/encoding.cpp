#include "encoding.h"

#include <bit>
#include <cstring>

#include "unicode.h"

namespace geditor {
namespace {

constexpr uint64_t kOnes = 0x0101010101010101ull;
constexpr uint64_t kHighs = 0x8080808080808080ull;

bool has_byte(uint64_t v, unsigned char b) {
  const uint64_t x = v ^ (kOnes * b);
  return ((x - kOnes) & ~x & kHighs) != 0;
}

bool is_cont(unsigned char b) {
  return (b & 0xC0) == 0x80;
}

}  // namespace

DecodedText decode_utf8(std::string_view bytes) {
  DecodedText result;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
  const unsigned char* end = p + bytes.size();
  if (bytes.size() >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
    result.format.bom = true;
    p += 3;
  }
  const unsigned char* begin = reinterpret_cast<const unsigned char*>(bytes.data());

  // Upper bound of UTF-16 units: one per lead byte, two for 4-byte sequences.
  // Eight bytes at a time: continuation bytes are 10xxxxxx, 4-byte leads
  // 1111xxxx.
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
  if (units >= 0xFFFFFFFFull) {
    result.status = DecodeStatus::kTooLarge;
    return result;
  }
  result.text.resize(units);
  result.line_feeds.reserve(bytes.size() / 64 + 16);
  wchar_t* const out_begin = result.text.data();
  wchar_t* out = out_begin;
  size_t lf_count = 0;
  size_t crlf_count = 0;
  size_t cr_count = 0;

  while (p < end) {
    const unsigned char b = *p;
    if (b < 0x80 && b != '\n' && b != '\r') {
      // ASCII run: 8 bytes at a time, then byte by byte.
      while (end - p >= 8) {
        uint64_t v;
        std::memcpy(&v, p, 8);
        if ((v & kHighs) != 0 || has_byte(v, '\n') || has_byte(v, '\r')) break;
        for (int i = 0; i < 8; ++i) out[i] = static_cast<wchar_t>(p[i]);
        out += 8;
        p += 8;
      }
      while (p < end && *p < 0x80 && *p != '\n' && *p != '\r') *out++ = static_cast<wchar_t>(*p++);
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
    if (b < 0x80) {
      if (b == '\n') {
        result.line_feeds.push_back(static_cast<uint32_t>(out - out_begin));
        ++lf_count;
      } else if (b == '\r') {
        if (p + 1 < end && p[1] == '\n') {
          result.line_feeds.push_back(static_cast<uint32_t>(out - out_begin));
          ++crlf_count;
          *out++ = L'\n';
          p += 2;
          continue;
        }
        ++cr_count;
      }
      *out++ = static_cast<wchar_t>(b);
      ++p;
      continue;
    }
    char32_t c = 0;
    size_t n = 0;
    bool ok = false;
    if (b >= 0xC2 && b <= 0xDF) {
      n = 2;
      ok = end - p >= 2 && is_cont(p[1]);
      if (ok) c = (static_cast<char32_t>(b & 0x1F) << 6) | (p[1] & 0x3F);
    } else if (b >= 0xE0 && b <= 0xEF) {
      n = 3;
      if (end - p >= 3 && is_cont(p[1]) && is_cont(p[2])) {
        ok = (b != 0xE0 || p[1] >= 0xA0) && (b != 0xED || p[1] <= 0x9F);
        c = (static_cast<char32_t>(b & 0x0F) << 12) | (static_cast<char32_t>(p[1] & 0x3F) << 6) |
            (p[2] & 0x3F);
      }
    } else if (b >= 0xF0 && b <= 0xF4) {
      n = 4;
      if (end - p >= 4 && is_cont(p[1]) && is_cont(p[2]) && is_cont(p[3])) {
        ok = (b != 0xF0 || p[1] >= 0x90) && (b != 0xF4 || p[1] <= 0x8F);
        c = (static_cast<char32_t>(b & 0x07) << 18) | (static_cast<char32_t>(p[1] & 0x3F) << 12) |
            (static_cast<char32_t>(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
      }
    }
    if (!ok) {
      result.status = DecodeStatus::kInvalidUtf8;
      result.error_offset = static_cast<size_t>(p - begin);
      result.text.clear();
      result.line_feeds.clear();
      return result;
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
  result.text.resize(static_cast<size_t>(out - out_begin));

  const int kinds = (lf_count > 0 ? 1 : 0) + (crlf_count > 0 ? 1 : 0) + (cr_count > 0 ? 1 : 0);
  if (kinds > 1) {
    result.status = DecodeStatus::kMixedEol;
  } else if (cr_count > 0) {
    result.status = DecodeStatus::kCrEol;
  }
  if (result.status != DecodeStatus::kOk) {
    result.text.clear();
    result.line_feeds.clear();
    return result;
  }
  result.format.eol = crlf_count > 0 ? Eol::kCrLf : Eol::kLf;
  return result;
}

std::string encode_utf8(const PieceTree& tree, const FileFormat& format) {
  std::string out;
  out.reserve(tree.length() + tree.line_feed_count() + 3);
  if (format.bom) out.append("\xEF\xBB\xBF");
  const bool crlf = format.eol == Eol::kCrLf;
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
  tree.for_each_chunk(0, tree.length(), [&](std::wstring_view chunk) {
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
        if (w == L'\n' && crlf) out.push_back('\r');
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

const wchar_t* eol_name(Eol eol) {
  return eol == Eol::kCrLf ? L"CRLF" : L"LF";
}

const wchar_t* encoding_name(const FileFormat& format) {
  return format.bom ? L"UTF-8 BOM" : L"UTF-8";
}

}  // namespace geditor
