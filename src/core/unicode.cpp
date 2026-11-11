#include "unicode.h"

namespace geditor {

void append_utf16(std::wstring& out, char32_t c) {
  if (c < 0x10000) {
    out.push_back(static_cast<wchar_t>(c));
  } else {
    const char32_t v = c - 0x10000;
    out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
    out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
  }
}

std::wstring to_utf16(char32_t c) {
  std::wstring s;
  append_utf16(s, c);
  return s;
}

char32_t decode_at(std::wstring_view s, size_t i, size_t* units) {
  const wchar_t c = s[i];
  if (is_high_surrogate(c) && i + 1 < s.size() && is_low_surrogate(s[i + 1])) {
    *units = 2;
    return combine_surrogates(c, s[i + 1]);
  }
  *units = 1;
  return static_cast<char32_t>(c);
}

namespace {

bool in(char32_t c, char32_t first, char32_t last) {
  return c >= first && c <= last;
}

bool odd(char32_t c) {
  return (c & 1) != 0;
}

}  // namespace

char32_t to_upper(char32_t c) {
  if (c < 0x80) return in(c, U'a', U'z') ? c - 0x20 : c;
  // Latin-1 Supplement
  if (c == 0xB5) return 0x39C;
  if (in(c, 0xE0, 0xFE) && c != 0xF7) return c - 0x20;
  if (c == 0xFF) return 0x178;
  // Latin Extended-A
  if (in(c, 0x100, 0x17F)) {
    if ((in(c, 0x100, 0x12F) || in(c, 0x132, 0x137) || in(c, 0x14A, 0x177)) && odd(c)) return c - 1;
    if ((in(c, 0x139, 0x148) || in(c, 0x179, 0x17E)) && !odd(c)) return c - 1;
    if (c == 0x131) return 0x49;
    if (c == 0x17F) return 0x53;
    return c;
  }
  // Greek and Coptic (and U+0345, whose upper case is Greek)
  if (c == 0x345) return 0x399;
  if (in(c, 0x370, 0x3FF)) {
    if (c == 0x371 || c == 0x373 || c == 0x377) return c - 1;
    if (in(c, 0x37B, 0x37D)) return c + 0x82;
    if (c == 0x3AC) return 0x386;
    if (in(c, 0x3AD, 0x3AF)) return c - 0x25;
    if (c == 0x3C2) return 0x3A3;
    if (in(c, 0x3B1, 0x3CB)) return c - 0x20;
    if (c == 0x3CC) return 0x38C;
    if (c == 0x3CD || c == 0x3CE) return c - 0x3F;
    if (c == 0x3D0) return 0x392;
    if (c == 0x3D1) return 0x398;
    if (c == 0x3D5) return 0x3A6;
    if (c == 0x3D6) return 0x3A0;
    if (c == 0x3D7) return 0x3CF;
    if (in(c, 0x3D8, 0x3EF) && odd(c)) return c - 1;
    if (c == 0x3F0) return 0x39A;
    if (c == 0x3F1) return 0x3A1;
    if (c == 0x3F2) return 0x3F9;
    if (c == 0x3F3) return 0x37F;
    if (c == 0x3F5) return 0x395;
    if (c == 0x3F8 || c == 0x3FB) return c - 1;
    return c;
  }
  // Cyrillic
  if (in(c, 0x400, 0x4FF)) {
    if (in(c, 0x430, 0x44F)) return c - 0x20;
    if (in(c, 0x450, 0x45F)) return c - 0x50;
    if ((in(c, 0x460, 0x481) || in(c, 0x48A, 0x4BF) || in(c, 0x4D0, 0x4FF)) && odd(c)) return c - 1;
    if (in(c, 0x4C1, 0x4CE) && !odd(c)) return c - 1;
    if (c == 0x4CF) return 0x4C0;
    return c;
  }
  // Latin Extended Additional
  if (in(c, 0x1E00, 0x1EFF)) {
    if ((in(c, 0x1E00, 0x1E95) || in(c, 0x1EA0, 0x1EFF)) && odd(c)) return c - 1;
    if (c == 0x1E9B) return 0x1E60;
    return c;
  }
  // Fullwidth Latin
  if (in(c, 0xFF41, 0xFF5A)) return c - 0x20;
  return c;
}

char32_t to_lower(char32_t c) {
  if (c < 0x80) return in(c, U'A', U'Z') ? c + 0x20 : c;
  // Latin-1 Supplement
  if (in(c, 0xC0, 0xDE) && c != 0xD7) return c + 0x20;
  // Latin Extended-A
  if (in(c, 0x100, 0x17F)) {
    if ((in(c, 0x100, 0x12F) || in(c, 0x132, 0x137) || in(c, 0x14A, 0x177)) && !odd(c)) return c + 1;
    if ((in(c, 0x139, 0x148) || in(c, 0x179, 0x17E)) && odd(c)) return c + 1;
    if (c == 0x130) return 0x69;
    if (c == 0x178) return 0xFF;
    return c;
  }
  // Greek and Coptic
  if (in(c, 0x370, 0x3FF)) {
    if (c == 0x370 || c == 0x372 || c == 0x376) return c + 1;
    if (c == 0x37F) return 0x3F3;
    if (c == 0x386) return 0x3AC;
    if (in(c, 0x388, 0x38A)) return c + 0x25;
    if (c == 0x38C) return 0x3CC;
    if (c == 0x38E || c == 0x38F) return c + 0x3F;
    if (in(c, 0x391, 0x3AB) && c != 0x3A2) return c + 0x20;
    if (c == 0x3CF) return 0x3D7;
    if (in(c, 0x3D8, 0x3EF) && !odd(c)) return c + 1;
    if (c == 0x3F4) return 0x3B8;
    if (c == 0x3F7 || c == 0x3FA) return c + 1;
    if (c == 0x3F9) return 0x3F2;
    if (in(c, 0x3FD, 0x3FF)) return c - 0x82;
    return c;
  }
  // Cyrillic
  if (in(c, 0x400, 0x4FF)) {
    if (in(c, 0x400, 0x40F)) return c + 0x50;
    if (in(c, 0x410, 0x42F)) return c + 0x20;
    if ((in(c, 0x460, 0x481) || in(c, 0x48A, 0x4BF) || in(c, 0x4D0, 0x4FF)) && !odd(c)) return c + 1;
    if (c == 0x4C0) return 0x4CF;
    if (in(c, 0x4C1, 0x4CE) && odd(c)) return c + 1;
    return c;
  }
  // Latin Extended Additional
  if (in(c, 0x1E00, 0x1EFF)) {
    if ((in(c, 0x1E00, 0x1E95) || in(c, 0x1EA0, 0x1EFF)) && !odd(c)) return c + 1;
    if (c == 0x1E9E) return 0xDF;
    return c;
  }
  // Fullwidth Latin
  if (in(c, 0xFF21, 0xFF3A)) return c + 0x20;
  return c;
}

bool fold_equal(std::wstring_view a, std::wstring_view b) {
  return a.size() == b.size() && fold_compare(a, b) == 0;
}

bool fold_starts_with(std::wstring_view s, std::wstring_view prefix) {
  return s.size() >= prefix.size() && fold_equal(s.substr(0, prefix.size()), prefix);
}

int fold_compare(std::wstring_view a, std::wstring_view b) {
  const size_t n = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    const char32_t x = to_lower(static_cast<char32_t>(a[i]));
    const char32_t y = to_lower(static_cast<char32_t>(b[i]));
    if (x != y) return x < y ? -1 : 1;
  }
  if (a.size() == b.size()) return 0;
  return a.size() < b.size() ? -1 : 1;
}

WordClass word_class(char32_t c) {
  if (c == 0x30FC || in(c, 0x3099, 0x309C) || c == 0xFF70 || c == 0xFF9E || c == 0xFF9F) {
    return WordClass::kExtender;
  }
  if (c < 0x80) {
    const bool alnum = (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9');
    return alnum ? WordClass::kAlnum : WordClass::kNone;
  }
  if (c < 0xC0) {
    return (c == 0xAA || c == 0xB5 || c == 0xBA) ? WordClass::kAlnum : WordClass::kNone;
  }
  if (c == 0xD7 || c == 0xF7) return WordClass::kNone;
  if (c == 0x37E || c == 0x387) return WordClass::kNone;
  if (c >= 0x2000 && c <= 0x2BFF) return WordClass::kNone;
  if (c >= 0x2E00 && c <= 0x2E7F) return WordClass::kNone;
  if (c >= 0x3000 && c <= 0x303F) {
    if (c == 0x3005 || c == 0x3006 || c == 0x3007 || (c >= 0x3021 && c <= 0x3029) ||
        (c >= 0x3038 && c <= 0x303B)) {
      return WordClass::kHan;
    }
    return WordClass::kNone;
  }
  if (c >= 0x3041 && c <= 0x309F) return WordClass::kHiragana;
  if (c == 0x30A0 || c == 0x30FB) return WordClass::kNone;
  if (c >= 0x30A1 && c <= 0x30FF) return WordClass::kKatakana;
  if (c >= 0x31F0 && c <= 0x31FF) return WordClass::kKatakana;
  if ((c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
      (c >= 0x20000 && c <= 0x3FFFF)) {
    return WordClass::kHan;
  }
  if (c >= 0xD800 && c <= 0xF8FF) return WordClass::kNone;  // surrogates, private use
  if (c >= 0xFE00 && c <= 0xFE6F) return WordClass::kNone;  // variation selectors, CJK forms
  if ((c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) ||
      (c >= 0xFF5B && c <= 0xFF65) || (c >= 0xFFE0)) {
    if (c < 0x10000) return WordClass::kNone;
  }
  if (c >= 0xFF66 && c <= 0xFF9F) return WordClass::kKatakana;
  if (c >= 0x1F000 && c <= 0x1FAFF) return WordClass::kNone;
  return WordClass::kAlnum;
}

std::wstring normalize_line_ends(std::wstring_view text) {
  std::wstring out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == L'\r') {
      out.push_back(L'\n');
      if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

std::wstring widen(std::string_view utf8) {
  std::wstring out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char b = static_cast<unsigned char>(utf8[i]);
    char32_t c = 0xFFFD;
    size_t n = 1;
    if (b < 0x80) {
      c = b;
    } else if (b >= 0xC2 && b <= 0xDF) {
      n = 2;
    } else if (b >= 0xE0 && b <= 0xEF) {
      n = 3;
    } else if (b >= 0xF0 && b <= 0xF4) {
      n = 4;
    }
    if (n > 1) {
      if (i + n > utf8.size()) {
        n = 1;
      } else {
        c = b & (0xFF >> (n + 1));
        for (size_t k = 1; k < n; ++k) {
          const unsigned char t = static_cast<unsigned char>(utf8[i + k]);
          if ((t & 0xC0) != 0x80) {
            c = 0xFFFD;
            n = 1;
            break;
          }
          c = (c << 6) | (t & 0x3F);
        }
      }
    }
    append_utf16(out, c);
    i += n;
  }
  return out;
}

}  // namespace geditor
