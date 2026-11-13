#include "jsonc.h"

#include <charconv>
#include <cmath>
#include <cwchar>

#include "unicode.h"

namespace geditor {
namespace {

constexpr int kMaxDepth = 100;
constexpr char32_t kEnd = 0xFFFFFFFF;  // past the last code point

// Decodes strict UTF-8 (no overlong forms, no surrogates, at most U+10FFFF).
// On an invalid byte, returns false and sets `bad` to the position of the
// code point that would start there.
bool decode_strict_utf8(std::string_view s, std::u32string& out, TextPos& bad) {
  size_t i = 0;
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
    i = 3;
  }
  TextPos pos;
  out.clear();
  out.reserve(s.size() - i);
  auto byte = [&](size_t k) { return k < s.size() ? static_cast<unsigned char>(s[k]) : 0u; };
  auto continuation = [&](size_t k, unsigned lo = 0x80, unsigned hi = 0xBF) {
    const unsigned b = byte(k);
    return k < s.size() && b >= lo && b <= hi;
  };
  while (i < s.size()) {
    const unsigned b0 = byte(i);
    char32_t c = 0;
    size_t n = 0;
    if (b0 < 0x80) {
      c = b0;
      n = 1;
    } else if (b0 >= 0xC2 && b0 <= 0xDF && continuation(i + 1)) {
      c = ((b0 & 0x1F) << 6) | (byte(i + 1) & 0x3F);
      n = 2;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
      const unsigned lo = b0 == 0xE0 ? 0xA0 : 0x80;
      const unsigned hi = b0 == 0xED ? 0x9F : 0xBF;
      if (continuation(i + 1, lo, hi) && continuation(i + 2)) {
        c = ((b0 & 0x0F) << 12) | ((byte(i + 1) & 0x3F) << 6) | (byte(i + 2) & 0x3F);
        n = 3;
      }
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
      const unsigned lo = b0 == 0xF0 ? 0x90 : 0x80;
      const unsigned hi = b0 == 0xF4 ? 0x8F : 0xBF;
      if (continuation(i + 1, lo, hi) && continuation(i + 2) && continuation(i + 3)) {
        c = ((b0 & 0x07) << 18) | ((byte(i + 1) & 0x3F) << 12) | ((byte(i + 2) & 0x3F) << 6) |
            (byte(i + 3) & 0x3F);
        n = 4;
      }
    }
    if (n == 0) {
      bad = pos;
      return false;
    }
    out.push_back(c);
    if (c == U'\n') {
      ++pos.line;
      pos.column = 1;
    } else {
      ++pos.column;
    }
    i += n;
  }
  return true;
}

bool is_word_char(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') ||
         c == U'_';
}

bool is_digit(char32_t c) {
  return c >= U'0' && c <= U'9';
}

int hex_value(char32_t c) {
  if (c >= U'0' && c <= U'9') return static_cast<int>(c - U'0');
  if (c >= U'a' && c <= U'f') return static_cast<int>(c - U'a' + 10);
  if (c >= U'A' && c <= U'F') return static_cast<int>(c - U'A' + 10);
  return -1;
}

// How a character is named in an error message.
std::wstring describe(char32_t c) {
  if (c == kEnd) return L"end of file";
  if (c < 0x20 || c == 0x7F) {
    wchar_t buf[16];
    swprintf(buf, 16, L"U+%04X", static_cast<unsigned>(c));
    return buf;
  }
  std::wstring s = L"'";
  append_utf16(s, c);
  return s + L"'";
}

class Parser {
 public:
  explicit Parser(const std::u32string& text) : s_(text) {}

  bool parse(JsonValue& out) {
    if (!skip_space()) return false;
    if (peek() == kEnd) {
      out.type = JsonValue::Type::kObject;
      return true;
    }
    if (peek() != U'{') {
      return fail(pos_, L"Expected '{': the settings must be one JSON object");
    }
    if (!parse_value(out, 0)) return false;
    if (!skip_space()) return false;
    if (peek() != kEnd) return fail(pos_, L"Unexpected " + describe(peek()) + L" after the top-level object");
    return true;
  }

  const JsonError& error() const { return error_; }

 private:
  char32_t peek(size_t ahead = 0) const {
    return i_ + ahead < s_.size() ? s_[i_ + ahead] : kEnd;
  }

  void advance() {
    if (i_ >= s_.size()) return;
    if (s_[i_] == U'\n') {
      ++pos_.line;
      pos_.column = 1;
    } else {
      ++pos_.column;
    }
    ++i_;
  }

  bool fail(TextPos pos, std::wstring message) {
    error_.pos = pos;
    error_.message = std::move(message);
    return false;
  }

  // White space and comments.
  bool skip_space() {
    for (;;) {
      const char32_t c = peek();
      if (c == U' ' || c == U'\t' || c == U'\r' || c == U'\n') {
        advance();
        continue;
      }
      if (c != U'/') return true;
      const TextPos start = pos_;
      if (peek(1) == U'/') {
        while (peek() != kEnd && peek() != U'\n') advance();
        continue;
      }
      if (peek(1) == U'*') {
        advance();
        advance();
        for (;;) {
          if (peek() == kEnd) return fail(start, L"Unterminated comment");
          if (peek() == U'*' && peek(1) == U'/') {
            advance();
            advance();
            break;
          }
          advance();
        }
        continue;
      }
      return fail(start, L"Unexpected '/'");
    }
  }

  bool parse_value(JsonValue& out, int depth) {
    out.pos = pos_;
    const char32_t c = peek();
    if (c == U'{' || c == U'[') {
      if (depth >= kMaxDepth) return fail(pos_, L"Nesting is too deep");
      return c == U'{' ? parse_object(out, depth + 1) : parse_array(out, depth + 1);
    }
    if (c == U'"') {
      out.type = JsonValue::Type::kString;
      return parse_string(out.string);
    }
    if (c == U'-' || is_digit(c)) return parse_number(out);
    if (is_word_char(c)) {
      std::wstring word;
      while (is_word_char(peek())) {
        append_utf16(word, peek());
        advance();
      }
      if (word == L"true" || word == L"false") {
        out.type = JsonValue::Type::kBool;
        out.boolean = word == L"true";
        return true;
      }
      if (word == L"null") {
        out.type = JsonValue::Type::kNull;
        return true;
      }
      return fail(out.pos, L"Unexpected word '" + word + L"'");
    }
    if (c == kEnd) return fail(pos_, L"Unexpected end of file");
    return fail(pos_, L"Unexpected " + describe(c));
  }

  bool parse_object(JsonValue& out, int depth) {
    out.type = JsonValue::Type::kObject;
    advance();  // {
    if (!skip_space()) return false;
    if (peek() == U'}') {
      advance();
      return true;
    }
    for (;;) {
      // A key, or '}' after a trailing comma.
      if (peek() != U'"') {
        if (peek() == kEnd) return fail(pos_, L"Unexpected end of file: expected a key or '}'");
        return fail(pos_, L"Expected a key or '}', found " + describe(peek()));
      }
      JsonMember member;
      member.key_pos = pos_;
      if (!parse_string(member.key)) return false;
      if (!skip_space()) return false;
      if (peek() != U':') return fail(pos_, L"Expected ':', found " + describe(peek()));
      advance();
      if (!skip_space()) return false;
      if (!parse_value(member.value, depth)) return false;
      out.members.push_back(std::move(member));
      if (!skip_space()) return false;
      if (peek() == U'}') {
        advance();
        return true;
      }
      if (peek() != U',') return fail(pos_, L"Expected ',' or '}', found " + describe(peek()));
      advance();
      if (!skip_space()) return false;
      if (peek() == U'}') {  // trailing comma
        advance();
        return true;
      }
    }
  }

  bool parse_array(JsonValue& out, int depth) {
    out.type = JsonValue::Type::kArray;
    advance();  // [
    if (!skip_space()) return false;
    if (peek() == U']') {
      advance();
      return true;
    }
    for (;;) {
      if (peek() == U',') return fail(pos_, L"Expected a value or ']', found ','");
      JsonValue item;
      if (!parse_value(item, depth)) return false;
      out.items.push_back(std::move(item));
      if (!skip_space()) return false;
      if (peek() == U']') {
        advance();
        return true;
      }
      if (peek() != U',') return fail(pos_, L"Expected ',' or ']', found " + describe(peek()));
      advance();
      if (!skip_space()) return false;
      if (peek() == U']') {  // trailing comma
        advance();
        return true;
      }
    }
  }

  // Four hex digits after "\u"; -1 when they are missing.
  int read_hex4() {
    int value = 0;
    for (int k = 0; k < 4; ++k) {
      const int h = hex_value(peek());
      if (h < 0) return -1;
      value = value * 16 + h;
      advance();
    }
    return value;
  }

  bool parse_string(std::wstring& out) {
    const TextPos start = pos_;
    advance();  // "
    for (;;) {
      const char32_t c = peek();
      if (c == kEnd || c == U'\n' || c == U'\r') return fail(start, L"Unterminated string");
      if (c == U'"') {
        advance();
        return true;
      }
      if (c < 0x20) {
        return fail(pos_, L"Control character " + describe(c) + L" in a string (write it as an escape)");
      }
      if (c != U'\\') {
        append_utf16(out, c);
        advance();
        continue;
      }
      const TextPos escape = pos_;
      advance();  // backslash
      const char32_t e = peek();
      switch (e) {
        case U'"': out += L'"'; break;
        case U'\\': out += L'\\'; break;
        case U'/': out += L'/'; break;
        case U'b': out += L'\b'; break;
        case U'f': out += L'\f'; break;
        case U'n': out += L'\n'; break;
        case U'r': out += L'\r'; break;
        case U't': out += L'\t'; break;
        case U'u': {
          advance();
          const int unit = read_hex4();
          if (unit < 0) return fail(escape, L"Invalid \\u escape: expected four hex digits");
          if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return fail(escape, L"Lone surrogate in a \\u escape");
          }
          if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (peek() != U'\\' || peek(1) != U'u') {
              return fail(escape, L"Lone surrogate in a \\u escape");
            }
            advance();
            advance();
            const int low = read_hex4();
            if (low < 0xDC00 || low > 0xDFFF) {
              return fail(escape, L"Lone surrogate in a \\u escape");
            }
            out += static_cast<wchar_t>(unit);
            out += static_cast<wchar_t>(low);
          } else {
            out += static_cast<wchar_t>(unit);
          }
          continue;
        }
        default:
          return fail(escape, L"Invalid escape \\" + (e == kEnd ? std::wstring() : describe(e)));
      }
      advance();
    }
  }

  bool parse_number(JsonValue& out) {
    const TextPos start = pos_;
    std::string text;
    auto take = [&] {
      text += static_cast<char>(peek());
      advance();
    };
    if (peek() == U'-') take();
    if (!is_digit(peek())) return fail(start, L"Invalid number");
    if (peek() == U'0') {
      take();
      if (is_digit(peek())) return fail(start, L"Invalid number: no leading zeros");
    } else {
      while (is_digit(peek())) take();
    }
    bool integer = true;
    if (peek() == U'.') {
      integer = false;
      take();
      if (!is_digit(peek())) return fail(start, L"Invalid number");
      while (is_digit(peek())) take();
    }
    if (peek() == U'e' || peek() == U'E') {
      integer = false;
      take();
      if (peek() == U'+' || peek() == U'-') take();
      if (!is_digit(peek())) return fail(start, L"Invalid number");
      while (is_digit(peek())) take();
    }
    if (is_word_char(peek()) || peek() == U'.') return fail(start, L"Invalid number");
    double value = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (r.ec != std::errc() || !std::isfinite(value)) return fail(start, L"Number out of range");
    out.type = JsonValue::Type::kNumber;
    out.number = value;
    if (integer) {
      int64_t i = 0;
      const auto ri = std::from_chars(text.data(), text.data() + text.size(), i);
      if (ri.ec == std::errc() && ri.ptr == text.data() + text.size()) {
        out.integer = true;
        out.integer_value = i;
      }
    }
    return true;
  }

  const std::u32string& s_;
  size_t i_ = 0;
  TextPos pos_;
  JsonError error_;
};

}  // namespace

JsonParse parse_jsonc(std::string_view utf8) {
  JsonParse result;
  std::u32string text;
  TextPos bad;
  if (!decode_strict_utf8(utf8, text, bad)) {
    result.error.pos = bad;
    result.error.message = L"Invalid UTF-8";
    return result;
  }
  Parser parser(text);
  if (!parser.parse(result.value)) {
    result.error = parser.error();
    result.value = JsonValue();
    return result;
  }
  result.ok = true;
  return result;
}

}  // namespace geditor
