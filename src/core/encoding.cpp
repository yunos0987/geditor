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
  // Line ends: each kind becomes one '\n'. A new run starts right after the
  // last '\n' of the previous kind.
  result.eol_runs.push_back(EolRun{0, Eol::kLf});
  bool any_line_end = false;
  size_t after_last_line_end = 0;
  auto line_end = [&](Eol kind) {
    const size_t at = static_cast<size_t>(out - out_begin);
    result.line_feeds.push_back(static_cast<uint32_t>(at));
    ++result.eol_counts[static_cast<size_t>(kind)];
    if (!any_line_end) {
      result.eol_runs.back().eol = kind;
      any_line_end = true;
    } else if (result.eol_runs.back().eol != kind) {
      result.eol_runs.push_back(EolRun{after_last_line_end, kind});
    }
    after_last_line_end = at + 1;
    *out++ = L'\n';
  };

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
        line_end(Eol::kLf);
        ++p;
      } else if (b == '\r') {
        if (p + 1 < end && p[1] == '\n') {
          line_end(Eol::kCrLf);
          p += 2;
        } else {
          line_end(Eol::kCr);
          ++p;
        }
      } else {
        *out++ = static_cast<wchar_t>(b);
        ++p;
      }
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
      result.eol_runs.clear();
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
  result.format.eol = default_eol_of(result.eol_counts, Eol::kLf);
  return result;
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
  return format.bom ? L"UTF-8 BOM" : L"UTF-8";
}

}  // namespace geditor
