// UTF-8 / BOM / EOL decoding and encoding.

#include "encoding.h"
#include "test_harness.h"

using geditor::DecodeStatus;
using geditor::DecodedText;
using geditor::Eol;
using geditor::PieceTree;

namespace {

std::string round_trip(const std::string& bytes) {
  DecodedText d = geditor::decode_utf8(bytes);
  CHECK(d.status == DecodeStatus::kOk);
  const geditor::FileFormat format = d.format;
  PieceTree t = geditor::make_piece_tree(d);
  return geditor::encode_utf8(t, format);
}

}  // namespace

TEST(encoding_utf8_lf_decodes) {
  const DecodedText d = geditor::decode_utf8("a\xE3\x81\x82\nb\xF0\x9F\x98\x80");
  CHECK(d.status == DecodeStatus::kOk);
  CHECK(!d.format.bom);
  CHECK(d.format.eol == Eol::kLf);
  CHECK_EQ(d.text, std::wstring(L"a\x3042\nb\U0001F600"));
  CHECK_EQ(d.line_feeds.size(), size_t{1});
  CHECK_EQ(d.line_feeds[0], uint32_t{2});
}

TEST(encoding_bom_crlf_decodes_to_lf) {
  const DecodedText d = geditor::decode_utf8("\xEF\xBB\xBFone\r\ntwo\r\n");
  CHECK(d.status == DecodeStatus::kOk);
  CHECK(d.format.bom);
  CHECK(d.format.eol == Eol::kCrLf);
  CHECK_EQ(d.text, std::wstring(L"one\ntwo\n"));
  CHECK_EQ(d.line_feeds.size(), size_t{2});
}

TEST(encoding_no_line_end_uses_lf) {
  const DecodedText d = geditor::decode_utf8("single line");
  CHECK(d.status == DecodeStatus::kOk);
  CHECK(d.format.eol == Eol::kLf);
}

TEST(encoding_round_trip_is_byte_identical) {
  const std::string lf = "line 1\n\xE6\x97\xA5\xE6\x9C\xAC\nlast without eol: long ascii text here";
  CHECK_EQ(round_trip(lf), lf);
  const std::string bom_crlf = "\xEF\xBB\xBF" "a\r\n\xF0\x9F\x98\x80\r\n\r\nend\r\n";
  CHECK_EQ(round_trip(bom_crlf), bom_crlf);
  CHECK_EQ(round_trip(""), std::string());
  CHECK_EQ(round_trip("no line end"), std::string("no line end"));
  CHECK_EQ(round_trip("\xEF\xBB\xBF"), std::string("\xEF\xBB\xBF"));
}

// M4b (PLAN 2 "bytes that cannot be read"): invalid UTF-8 is read with
// U+FFFD, one per maximal subpart, and marked undecodable (M4a refused it).
TEST(encoding_invalid_utf8_reads_as_replacement_characters) {
  struct Case {
    const char* bytes;
    const wchar_t* text;
  };
  const Case bad[] = {
      {"abc\x80", L"abc\xFFFD"},                        // stray continuation
      {"\xC0\xAF", L"\xFFFD\xFFFD"},                    // overlong
      {"\xE0\x80\xAF", L"\xFFFD\xFFFD\xFFFD"},          // overlong 3-byte
      {"\xED\xA0\x80", L"\xFFFD\xFFFD\xFFFD"},          // surrogate
      {"\xF4\x90\x80\x80", L"\xFFFD\xFFFD\xFFFD\xFFFD"},  // above U+10FFFF
      {"ok \xE3\x81", L"ok \xFFFD"},                    // truncated: one maximal subpart
      {"\xFF", L"\xFFFD"},
      {"\xF0\x9F\x98x", L"\xFFFDx"},                    // truncated 4-byte before ASCII
  };
  for (const Case& c : bad) {
    const DecodedText d = geditor::decode_utf8(c.bytes);
    CHECK(d.status == DecodeStatus::kOk);
    CHECK(d.format.undecodable);
    CHECK(d.replaced);
    CHECK_EQ(d.text, std::wstring(c.text));
  }
  const DecodedText d = geditor::decode_utf8("ab\ncd\r\nabc\x80");
  CHECK_EQ(d.error_offset, size_t{10});
  CHECK_EQ(d.error_line, size_t{2});
  CHECK(!geditor::decode_utf8("valid \xE3\x81\x82").format.undecodable);
}

// M1 refused mixed and CR-only line ends; M4a keeps them line by line.
TEST(encoding_mixed_and_cr_line_ends_are_kept) {
  for (const char* s : {"a\nb\r\nc", "a\rb\nc", "a\rb\rc", "trailing cr\r"}) {
    CHECK(geditor::decode_utf8(s).status == DecodeStatus::kOk);
    CHECK_EQ(round_trip(s), std::string(s));
  }
  CHECK(geditor::decode_utf8("a\rb\rc").format.eol == Eol::kCr);
  CHECK_EQ(geditor::decode_utf8("a\rb\rc").text, std::wstring(L"a\nb\nc"));
}

TEST(encoding_long_ascii_fast_path_keeps_line_feeds) {
  std::string s;
  for (int i = 0; i < 100; ++i) s += "0123456789abcdef\n";
  const DecodedText d = geditor::decode_utf8(s);
  CHECK(d.status == DecodeStatus::kOk);
  CHECK_EQ(d.line_feeds.size(), size_t{100});
  CHECK_EQ(d.text.size(), s.size());
  CHECK_EQ(d.line_feeds[99], uint32_t{17 * 100 - 1});
}
