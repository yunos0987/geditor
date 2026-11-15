// M4b encodings through the core (docs/PLAN.md 2 "encodings that can be
// named" - "characters a non-Unicode encoding cannot write"): the 36
// combinations of 9 encodings and 4 kinds of line ends ([M4-1], [C1]), the
// detection branches ([M4-2], [C2]), characters that a code page cannot
// write, best fit included ([M4-3], [C3]), and bytes that cannot be read
// ([M4-7], [C4]). The code pages are the real Windows ones (encoding.cpp).

#include <cstdio>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#include "commands.h"
#include "encoding.h"
#include "fake_host.h"
#include "unicode.h"

using geditor::Codec;
using geditor::DecodedText;
using geditor::Eol;
using geditor::FileFormat;
using test::Session;

namespace {

constexpr wchar_t kPath[] = L"C:\\work\\e.txt";

// A Japanese and ASCII sample that each of the nine encodings can write:
// "abc 日本語", "テキスト 123", "かな漢字".
const wchar_t* const kLines[] = {L"abc \x65E5\x672C\x8A9E", L"\x30C6\x30AD\x30B9\x30C8 123",
                                 L"\x304B\x306A\x6F22\x5B57"};

// The lines in the code pages (as Python's cp932 / euc_jp / iso2022_jp
// codecs write them; code pages 932 / 20932 / 50220 write the same bytes).
const char* const kShiftJisLines[] = {"\x61\x62\x63\x20\x93\xFA\x96\x7B\x8C\xEA",
                                      "\x83\x65\x83\x4C\x83\x58\x83\x67\x20\x31\x32\x33",
                                      "\x82\xA9\x82\xC8\x8A\xBF\x8E\x9A"};
const char* const kEucJpLines[] = {"\x61\x62\x63\x20\xC6\xFC\xCB\xDC\xB8\xEC",
                                   "\xA5\xC6\xA5\xAD\xA5\xB9\xA5\xC8\x20\x31\x32\x33",
                                   "\xA4\xAB\xA4\xCA\xB4\xC1\xBB\xFA"};
const char* const kIso2022JpLines[] = {
    "\x61\x62\x63\x20\x1B\x24\x42\x46\x7C\x4B\x5C\x38\x6C\x1B\x28\x42",
    "\x1B\x24\x42\x25\x46\x25\x2D\x25\x39\x25\x48\x1B\x28\x42\x20\x31\x32\x33",
    "\x1B\x24\x42\x24\x2B\x24\x4A\x34\x41\x3B\x7A\x1B\x28\x42"};

std::string utf16(const std::wstring& s, bool big_endian) {
  std::string out;
  for (const wchar_t w : s) {
    const char high = static_cast<char>((static_cast<unsigned>(w) >> 8) & 0xFF);
    const char low = static_cast<char>(static_cast<unsigned>(w) & 0xFF);
    out.push_back(big_endian ? high : low);
    out.push_back(big_endian ? low : high);
  }
  return out;
}

// `text` in the encoding `name` (ASCII-compatible code pages: only ASCII
// text here, such as line ends).
std::string bytes_of(const std::wstring& name, const std::wstring& text) {
  if (name.rfind(L"utf-16le", 0) == 0) return utf16(text, false);
  if (name.rfind(L"utf-16be", 0) == 0) return utf16(text, true);
  return test::utf8(text);
}

std::string bom_of(const std::wstring& name) {
  if (name == L"utf-8-bom") return "\xEF\xBB\xBF";
  if (name == L"utf-16le-bom") return "\xFF\xFE";
  if (name == L"utf-16be-bom") return "\xFE\xFF";
  return std::string();
}

std::string line_bytes(const std::wstring& name, size_t i) {
  if (name == L"shift_jis") return kShiftJisLines[i];
  if (name == L"euc-jp") return kEucJpLines[i];
  if (name == L"iso-2022-jp") return kIso2022JpLines[i];
  return bytes_of(name, kLines[i]);
}

struct EolKind {
  const char* name;
  const wchar_t* ends[3];
};

constexpr EolKind kEolKinds[] = {
    {"LF", {L"\n", L"\n", L"\n"}},
    {"CRLF", {L"\r\n", L"\r\n", L"\r\n"}},
    {"CR", {L"\r", L"\r", L"\r"}},
    {"mixed", {L"\n", L"\r\n", L"\r"}},
};

std::string sample_bytes(const std::wstring& name, const EolKind& eol) {
  std::string out = bom_of(name);
  for (size_t i = 0; i < 3; ++i) out += line_bytes(name, i) + bytes_of(name, eol.ends[i]);
  return out;
}

std::wstring sample_text() {
  std::wstring out;
  for (const wchar_t* line : kLines) out += std::wstring(line) + L"\n";
  return out;
}

// Opens `bytes` as kPath (or `path`) in `encoding` ("" detects), replacing
// the buffer without questions.
void open_as(Session& s, const std::string& bytes, const std::wstring& encoding,
             const std::wstring& path = kPath) {
  s.host.add_file(path, bytes);
  if (!s.ed.buffer().path.empty() || s.ed.modified()) s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.ed.request_open(path, encoding);
  CHECK(s.ed.buffer().path == path);
}

std::wstring setting_name(const Session& s) {
  return geditor::encoding_setting_name(s.ed.buffer().format);
}

}  // namespace

// ---- [C1] [M4-1]: 9 encodings x 4 kinds of line ends ----

TEST(m4b_36_combinations_load_and_save_the_same_bytes) {
  size_t cases = 0;
  for (const wchar_t* name : geditor::encoding_names()) {
    for (const EolKind& eol : kEolKinds) {
      Session s;
      const std::string bytes = sample_bytes(name, eol);
      open_as(s, bytes, name);
      const bool ok = s.text() == sample_text() && setting_name(s) == name && !s.ed.buffer().format.undecodable;
      // An edit and its removal, then C-x C-s writes every byte again.
      s.keys(L"M->");
      s.type(L"x");
      s.keys(L"DEL");
      CHECK(s.modified());
      s.keys(L"C-x C-s");
      const bool same = s.host.file(kPath) == bytes && !s.modified();
      if (!ok || !same) {
        std::printf("  %s / %s: read %s, saved %s (%s)\n", test::to_display(name).c_str(), eol.name,
                    ok ? "ok" : "WRONG", same ? "same" : "DIFFERENT", test::to_display(s.message()).c_str());
      }
      CHECK(ok);
      CHECK(same);
      ++cases;
    }
  }
  std::printf("  combinations: %zu\n", cases);
  CHECK_EQ(cases, size_t{36});
}

// [C4] each of the nine reads the Japanese sample when named, and the mode
// line names it.
TEST(m4b_each_encoding_reads_japanese_when_named) {
  const wchar_t* const display[] = {L"UTF-8",        L"UTF-8-BOM", L"UTF-16LE", L"UTF-16LE-BOM", L"UTF-16BE",
                                    L"UTF-16BE-BOM", L"Shift_JIS", L"EUC-JP",   L"ISO-2022-JP"};
  size_t i = 0;
  for (const wchar_t* name : geditor::encoding_names()) {
    Session s;
    open_as(s, sample_bytes(name, kEolKinds[1]), name);
    CHECK_EQ(s.text(), sample_text());
    CHECK_EQ(s.ed.encoding_text(), std::wstring(display[i]));
    CHECK(s.ed.mode_line().find(std::wstring(L"  ") + display[i] + L" CRLF  ") != std::wstring::npos);
    CHECK(s.message().empty());
    ++i;
  }
  CHECK_EQ(i, size_t{9});
}

// ---- [C2] [M4-2]: detection, one test per branch ----

TEST(m4b_detect_utf8_bom) {
  Session s;
  open_as(s, "\xEF\xBB\xBF" + test::utf8(L"a\x65E5\n"), L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8-bom"));
  CHECK_EQ(s.text(), std::wstring(L"a\x65E5\n"));
}

TEST(m4b_detect_utf16le_bom) {
  Session s;
  open_as(s, "\xFF\xFE" + utf16(L"a\x65E5\r\n", false), L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16le-bom"));
  CHECK_EQ(s.text(), std::wstring(L"a\x65E5\n"));
  CHECK(s.ed.buffer().format.eol == Eol::kCrLf);
}

TEST(m4b_detect_utf16be_bom) {
  Session s;
  open_as(s, "\xFE\xFF" + utf16(L"a\x65E5\n", true), L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16be-bom"));
  CHECK_EQ(s.text(), std::wstring(L"a\x65E5\n"));
}

TEST(m4b_detect_utf16le_without_bom_by_nul_bias) {
  Session s;
  open_as(s, utf16(sample_text(), false), L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16le"));
  CHECK_EQ(s.text(), sample_text());
}

TEST(m4b_detect_utf16be_without_bom_by_nul_bias) {
  Session s;
  open_as(s, utf16(sample_text(), true), L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-16be"));
  CHECK_EQ(s.text(), sample_text());
}

// Retry 1 R3: a few NUL bytes do not make a file UTF-16 (at least 2 on one
// side, one for every 4 units, and 4 times the other side).
TEST(m4b_detect_one_nul_in_ascii_is_utf8) {
  struct Case {
    std::string bytes;
    const wchar_t* text;
  };
  const Case cases[] = {
      {std::string("hello\0world\n", 12), L"hello\0world\n"},    // NUL at an odd offset
      {std::string("hell\0world\n\n", 12), L"hell\0world\n\n"},  // NUL at an even offset
      {std::string("a\0", 2), L"a\0"},                            // one ASCII character of UTF-16LE: UTF-8
  };
  for (const Case& c : cases) {
    Session s;
    open_as(s, c.bytes, L"");
    CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
    CHECK_EQ(s.text(), std::wstring(c.text, c.bytes.size()));
    CHECK(!s.ed.buffer().format.undecodable);
  }
}

TEST(m4b_detect_a_utf8_log_with_a_few_nuls_is_utf8) {
  std::string log;
  for (int i = 0; i < 20; ++i) log += "2026-11-15 10:00:0" + std::to_string(i % 10) + " INFO ready \xE6\x97\xA5\n";
  log[30] = '\0';
  log[61] = '\0';
  log[92] = '\0';
  Session s;
  open_as(s, log, L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
  CHECK(!s.ed.buffer().format.undecodable);
  CHECK_EQ(s.ed.buffer().text.length(), size_t{20 * 33});  // 35 bytes per line: 32 units and "\n"
  // An invalid byte as well: still UTF-8, now with "!".
  log[100] = '\xFF';
  Session t;
  open_as(t, log, L"");
  CHECK_EQ(setting_name(t), std::wstring(L"utf-8"));
  CHECK_EQ(t.ed.encoding_text(), std::wstring(L"UTF-8!"));
}

// Real BOM-less UTF-16 of short ASCII text: 2 characters are enough (the
// threshold), 4 are well inside it.
TEST(m4b_detect_short_ascii_utf16_without_bom) {
  for (const wchar_t* text : {L"abcd", L"ab"}) {
    Session le;
    open_as(le, utf16(text, false), L"");
    CHECK_EQ(setting_name(le), std::wstring(L"utf-16le"));
    CHECK_EQ(le.text(), std::wstring(text));
    Session be;
    open_as(be, utf16(text, true), L"");
    CHECK_EQ(setting_name(be), std::wstring(L"utf-16be"));
    CHECK_EQ(be.text(), std::wstring(text));
  }
}

TEST(m4b_detect_ascii_only_is_utf8) {
  Session s;
  open_as(s, "plain ascii\r\nonly\r\n", L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
  CHECK(!s.ed.buffer().format.undecodable);
}

TEST(m4b_detect_no_line_end_takes_new_file_eol) {
  Session s;
  s.settings(R"({"new_file": {"eol": "crlf"}})");
  open_as(s, std::string(kShiftJisLines[0]), L"");
  CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
  CHECK(s.ed.buffer().format.eol == Eol::kCrLf);
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"CRLF"));
}

// Short Shift_JIS sentences. "ﾃｽﾄ", "ﾘｽﾄ", "ﾌｧｲﾙ" and "ﾃｷｽﾄ" start with
// valid UTF-8 (C3 BD = U+00FD and so on) and are Shift_JIS; "ﾃｽ" (C3 BD) and
// "ﾈｺ" (C8 BA) are valid UTF-8 as a whole and are read as UTF-8 (the order
// of PLAN 2: UTF-8 validity before Shift_JIS), and
// revert-buffer-with-coding-system shift_jis reads them right.
TEST(m4b_detect_short_shift_jis_sentences) {
  struct Case {
    const char* bytes;
    const wchar_t* text;
  };
  const Case sjis[] = {
      {"\x82\xB1\x82\xEA\x82\xCD\x83\x65\x83\x58\x83\x67", L"\x3053\x308C\x306F\x30C6\x30B9\x30C8"},  // これはテスト
      {"\x93\xFA\x96\x7B\x8C\xEA", L"\x65E5\x672C\x8A9E"},                                       // 日本語
      {"\x95\x5C\x8E\xA6", L"\x8868\x793A"},                                                     // 表示 (0x5C trail)
      {"\xC3\xBD\xC4", L"\xFF83\xFF7D\xFF84"},                                                   // ﾃｽﾄ
      {"\xD8\xBD\xC4", L"\xFF98\xFF7D\xFF84"},                                                   // ﾘｽﾄ
      {"\xCC\xA7\xB2\xD9", L"\xFF8C\xFF67\xFF72\xFF99"},                                         // ﾌｧｲﾙ
      {"\xC3\xB7\xBD\xC4", L"\xFF83\xFF77\xFF7D\xFF84"},                                         // ﾃｷｽﾄ
  };
  for (const Case& c : sjis) {
    Session s;
    open_as(s, c.bytes, L"");
    if (setting_name(s) != L"shift_jis") std::printf("  %s\n", test::to_display(std::wstring(c.text)).c_str());
    CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
    CHECK_EQ(s.text(), std::wstring(c.text));
    CHECK(!s.ed.buffer().format.undecodable);
  }
  const Case utf8_too[] = {
      {"\xC3\xBD", L"\xFF83\xFF7D"},  // ﾃｽ
      {"\xC8\xBA", L"\xFF88\xFF7A"},  // ﾈｺ
  };
  for (const Case& c : utf8_too) {
    Session s;
    open_as(s, c.bytes, L"");
    CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
    CHECK(!s.ed.buffer().format.undecodable);
    s.ed.revert_buffer_with_coding_system(L"shift_jis");
    CHECK_EQ(s.text(), std::wstring(c.text));
    CHECK_EQ(setting_name(s), std::wstring(L"shift_jis"));
  }
}

TEST(m4b_detect_broken_bytes_read_as_utf8_with_replacement) {
  Session s;
  // 0xFD is neither UTF-8 nor Shift_JIS; the second line holds it.
  open_as(s, "ok\n\xE3\x81\x82 \xFD end\n", L"");
  CHECK_EQ(setting_name(s), std::wstring(L"utf-8"));
  CHECK_EQ(s.text(), std::wstring(L"ok\n\x3042 \xFFFD end\n"));
  CHECK_EQ(s.ed.encoding_text(), std::wstring(L"UTF-8!"));
  CHECK(s.ed.mode_line().find(L"  UTF-8! LF  ") != std::wstring::npos);
  CHECK_EQ(s.message(), std::wstring(L"C:\\work\\e.txt: not valid UTF-8 or Shift_JIS; read as UTF-8 with U+FFFD "
                                     L"for the bytes that are not (first at byte offset 7, line 2)"));
}

// ---- [C3] [M4-3]: characters a code page cannot write stop the save ----

TEST(m4b_unrepresentable_characters_stop_the_save) {
  struct Case {
    const wchar_t* encoding;
    const wchar_t* character;
    const wchar_t* code;
    const char* why;
  };
  const Case cases[] = {
      {L"shift_jis", L"\U0001F600", L"U+1F600", "emoji"},
      {L"shift_jis", L"\xD55C", L"U+D55C", "Hangul"},
      {L"shift_jis", L"\x00A5", L"U+00A5", "best fit to 0x5C"},
      {L"shift_jis", L"\x00A2", L"U+00A2", "'?' with lpUsedDefaultChar (best fit 81 91 without the flag)"},
      {L"shift_jis", L"\x00C0", L"U+00C0", "best fit to A"},
      {L"euc-jp", L"\U0001F600", L"U+1F600", "emoji"},
      {L"euc-jp", L"\xD55C", L"U+D55C", "Hangul"},
      {L"euc-jp", L"\x00A5", L"U+00A5", "no best fit measured on 20932: '?'"},
      {L"iso-2022-jp", L"\U0001F600", L"U+1F600", "emoji"},
      {L"iso-2022-jp", L"\xD55C", L"U+D55C", "Hangul"},
      {L"iso-2022-jp", L"\xFF71", L"U+FF71", "best fit: half-width katakana to full-width"},
  };
  for (const Case& c : cases) {
    Session s;
    // Line 2 "テキスト 123" gets the character after "テキ" (column 4).
    const std::string bytes = sample_bytes(c.encoding, kEolKinds[0]);
    open_as(s, bytes, c.encoding);
    const unsigned long long time = s.host.write_time(kPath);
    const size_t saves = s.host.saves.size();
    s.ed.set_point(s.ed.buffer().text.line_start(1) + 2);
    s.ed.insert_text(c.character);
    s.ed.set_point(0);
    s.keys(L"C-x C-s");
    const std::wstring want = std::wstring(L"Cannot save as ") + geditor::encoding_name(s.ed.buffer().format) + L": " +
                              c.code + L" at line 2, column 4 cannot be written in " +
                              geditor::encoding_name(s.ed.buffer().format) +
                              L"; choose another encoding with set-buffer-file-coding-system";
    if (s.message() != want) std::printf("  %s %s\n", test::to_display(c.encoding).c_str(), c.why);
    CHECK_EQ(s.message(), want);
    CHECK_EQ(s.host.file(kPath), bytes);        // the file is unchanged
    CHECK_EQ(s.host.write_time(kPath), time);   // and not written at all
    CHECK_EQ(s.host.saves.size(), saves);       // no save_file call: no temporary file
    CHECK(s.modified());
    // Point is on the character.
    CHECK_EQ(s.point(), s.ed.buffer().text.line_start(1) + 2);
  }
}

// Retry 1 R2: on code page 932 with WC_NO_BEST_FIT_CHARS, when the text ends
// in one of 12 characters, the conversion into a buffer of exactly the
// queried size fails. The save still stops at that character
// (not at the first one), as the last character and as the only one; the
// same characters in EUC-JP and ISO-2022-JP either stop the save at their
// own position or are written and read back the same.
TEST(m4b_unwritable_last_character_is_named_in_every_code_page) {
  const wchar_t* const chars[] = {L"\x00A2", L"\x00A3", L"\x00AB", L"\x00AC", L"\x00AF", L"\x00B5", L"\x00B7",
                                  L"\x00B8", L"\x00BB", L"\x3094", L"\x3099", L"\x309A", L"\x00A5", L"\U0001F600"};
  size_t refused_sjis = 0;
  for (const wchar_t* name : {L"shift_jis", L"euc-jp", L"iso-2022-jp"}) {
    for (const wchar_t* c : chars) {
      for (const bool alone : {false, true}) {
        Session s;
        open_as(s, alone ? "" : "abc", name);
        s.keys(L"M->");
        s.ed.insert_text(c);
        s.ed.set_point(0);
        s.keys(L"C-x C-s");
        size_t units = 0;
        const char32_t code = geditor::decode_at(c, 0, &units);
        wchar_t hex[16];
        std::swprintf(hex, std::size(hex), L"U+%04X", static_cast<unsigned>(code));
        const std::wstring display = geditor::encoding_name(s.ed.buffer().format);
        // U+3099 / U+309A join "c" into one grapheme cluster: point goes
        // to the start of the cluster (decision G3), column 2.
        const bool joins = code == 0x3099 || code == 0x309A;
        const size_t at = alone ? 0 : joins ? 2 : 3;
        const std::wstring column = std::to_wstring(at);
        const std::wstring want = L"Cannot save as " + display + L": " + hex + L" at line 1, column " + column +
                                  L" cannot be written in " + display +
                                  L"; choose another encoding with set-buffer-file-coding-system";
        if (s.modified()) {
          if (s.message() != want) {
            std::printf("  %s %s alone=%d\n", test::to_display(std::wstring(name)).c_str(),
                        test::to_display(std::wstring(hex)).c_str(), alone ? 1 : 0);
          }
          CHECK_EQ(s.message(), want);
          CHECK_EQ(s.point(), at);
          CHECK_EQ(s.host.file(kPath), std::string(alone ? "" : "abc"));
          if (std::wstring(name) == L"shift_jis") ++refused_sjis;
        } else {
          // Written: it reads back as the same text.
          FileFormat f;
          CHECK(geditor::parse_encoding(name, &f));
          const DecodedText back = geditor::decode_as(s.host.file(kPath), f);
          CHECK_EQ(back.text, std::wstring(alone ? L"" : L"abc") + c);
          CHECK(!back.format.undecodable);
        }
      }
    }
  }
  // Shift_JIS can write none of them.
  CHECK_EQ(refused_sjis, std::size(chars) * 2);
}

TEST(m4b_unicode_encodings_write_every_character) {
  for (const wchar_t* name : {L"utf-8", L"utf-16le", L"utf-16be-bom"}) {
    Session s;
    open_as(s, sample_bytes(name, kEolKinds[0]), name);
    s.keys(L"M->");
    s.ed.insert_text(L"\U0001F600\xD55C\x00A5");
    s.keys(L"C-x C-s");
    CHECK(!s.modified());
    const std::wstring text = s.text();
    open_as(s, s.host.file(kPath), name, L"C:\\work\\again.txt");
    CHECK_EQ(s.text(), text);
  }
}

// ---- [C4] [M4-7]: bytes that cannot be read ----

TEST(m4b_undecodable_bytes_become_replacement_with_position_and_mark) {
  struct Case {
    const wchar_t* encoding;
    std::string bytes;
    const wchar_t* text;
    size_t offset;
    size_t line;  // 1-based
  };
  const Case cases[] = {
      {L"utf-8", std::string("a\nb\xC3(") + "\n", L"a\nb\xFFFD(\n", 3, 2},
      {L"utf-16le", utf16(L"a\nb", false) + "x", L"a\nb\xFFFD", 6, 2},
      {L"utf-16be", utf16(L"a\n", true) + utf16(std::wstring(1, static_cast<wchar_t>(0xDC00)), true),
       L"a\n\xFFFD", 4, 2},
      {L"shift_jis", std::string("a\r\n\x85\x40") + "b", L"a\n\xFFFD@b", 3, 2},
      {L"euc-jp", std::string("x\n\xA9\xA1") + "y", L"x\n\xFFFD\xFFFDy", 2, 2},
      {L"iso-2022-jp", std::string("ok\n\x80") + "z", L"ok\n\xFFFDz", 3, 2},
  };
  for (const Case& c : cases) {
    Session s;
    open_as(s, c.bytes, c.encoding);
    const std::wstring name = geditor::encoding_name(s.ed.buffer().format);
    if (s.text() != c.text) std::printf("  %s\n", test::to_display(c.encoding).c_str());
    CHECK_EQ(s.text(), std::wstring(c.text));
    CHECK_EQ(s.ed.encoding_text(), name + L"!");
    CHECK(s.ed.mode_line().find(L"  " + name + L"! ") != std::wstring::npos);
    CHECK_EQ(s.message(), std::wstring(kPath) + L": bytes that are not valid " + name + L" were read as U+FFFD " +
                              L"(first at byte offset " + std::to_wstring(c.offset) + L", line " +
                              std::to_wstring(c.line) + L")");
  }
}

// Bytes that read but do not come back the same (code page 932 reads the
// NEC-selected IBM extension ED 40 as U+7E8A and writes it as FA 5C; 50220
// reads ESC $ @ but writes ESC $ B) are treated the same way.
TEST(m4b_round_trip_mismatch_on_read_marks_the_buffer) {
  struct Case {
    const wchar_t* encoding;
    std::string bytes;
    const wchar_t* text;
    size_t offset;
  };
  const Case cases[] = {
      {L"shift_jis", std::string("ab\n\xED\x40\n"), L"ab\n\x7E8A\n", 3},
      // The first differing byte: '@' of ESC $ @ (written ESC $ B).
      {L"iso-2022-jp", std::string("x\n\x1B$@F|\x1B(B\n"), L"x\n\x65E5\n", 4},
      // No ESC ( B before the line end (written before it).
      {L"iso-2022-jp", std::string("\x1B$BF|\n"), L"\x65E5\n", 5},
  };
  for (const Case& c : cases) {
    Session s;
    open_as(s, c.bytes, c.encoding);
    const std::wstring name = geditor::encoding_name(s.ed.buffer().format);
    CHECK_EQ(s.text(), std::wstring(c.text));
    CHECK_EQ(s.ed.encoding_text(), name + L"!");
    const std::wstring where = L"(first at byte offset " + std::to_wstring(c.offset);
    CHECK(s.message().find(name + L" does not give the same bytes back " + where) != std::wstring::npos);
    // Saved in its code page: refused, the file unchanged.
    s.keys(L"M->");
    s.type(L"z");
    s.keys(L"C-x C-s");
    CHECK(s.message().find(L"Cannot save as " + name + L": the file was read with bytes that " + name +
                           L" cannot give back " + where) == 0);
    CHECK_EQ(s.host.file(kPath), c.bytes);
    CHECK(s.modified());
  }
}

TEST(m4b_saving_an_undecodable_buffer_asks_in_unicode_and_refuses_in_code_pages) {
  // UTF-8 and UTF-16: a question; No keeps the file, Yes writes U+FFFD.
  for (const wchar_t* name : {L"utf-8", L"utf-16le"}) {
    Session s;
    const std::string bytes = std::wstring(name) == L"utf-8" ? std::string("a\xFF") : utf16(L"a", false) + "!";
    open_as(s, bytes, name);
    CHECK(s.ed.buffer().format.undecodable);
    s.keys(L"M->");
    s.type(L"b");
    s.host.yes_no_answers.push_back(false);
    s.keys(L"C-x C-s");
    CHECK_EQ(s.host.yes_no_dialogs, 1);
    CHECK(s.host.yes_no_questions.back().find(L"their original bytes will be lost. Save anyway?") !=
          std::wstring::npos);
    CHECK_EQ(s.message(), std::wstring(L"Not saved"));
    CHECK_EQ(s.host.file(kPath), bytes);
    s.host.yes_no_answers.push_back(true);
    s.keys(L"C-x C-s");
    CHECK(!s.modified());
    CHECK_EQ(s.host.file(kPath), bytes_of(name, L"a\xFFFD" L"b"));
    // Saved: no "!" any more, and the next save asks nothing.
    CHECK_EQ(s.ed.encoding_text(), std::wstring(geditor::encoding_name(s.ed.buffer().format)));
    s.type(L"c");
    s.keys(L"C-x C-s");
    CHECK_EQ(s.host.yes_no_dialogs, 2);
  }
  // Code pages: refused without a question, the file unchanged.
  for (const wchar_t* name : {L"shift_jis", L"euc-jp", L"iso-2022-jp"}) {
    Session s;
    const std::string bytes = std::string("a\n\xFF\xFF");
    open_as(s, bytes, name);
    CHECK(s.ed.buffer().format.undecodable);
    s.type(L"q");
    s.keys(L"C-x C-s");
    CHECK_EQ(s.host.yes_no_dialogs, 0);
    CHECK(s.message().find(L"Cannot save as ") == 0);
    CHECK_EQ(s.host.file(kPath), bytes);
    CHECK(s.modified());
  }
}

// M4a fix F1 holds for every encoding: an LF line end right after a CR one
// is written as CR in the encoded bytes too.
TEST(m4b_cr_then_lf_rule_holds_in_every_encoding) {
  for (const wchar_t* name : geditor::encoding_names()) {
    Session s;
    open_as(s, bom_of(name) + bytes_of(name, L"a\rx\nb\r\n"), name);
    s.keys(L"M-< C-n C-k");
    CHECK_EQ(s.text(), std::wstring(L"a\n\nb\n"));
    s.keys(L"C-x C-s");
    const std::string want = bom_of(name) + bytes_of(name, L"a\r\rb\r\n");
    if (s.host.file(kPath) != want) std::printf("  %s\n", test::to_display(name).c_str());
    CHECK_EQ(s.host.file(kPath), want);
    CHECK(s.message().find(L"saved 1 LF line end after a CR as CR to keep the lines") != std::wstring::npos);
  }
}

// The decoder on its own: every branch of decode_detected and decode_as.
TEST(m4b_decoder_branches) {
  // A named encoding without BOM keeps a BOM as U+FEFF (the bytes come back).
  const DecodedText kept = geditor::decode_as("\xEF\xBB\xBFx", FileFormat{});
  CHECK_EQ(kept.text, std::wstring(L"\xFEFFx"));
  FileFormat bom;
  bom.bom = true;
  CHECK_EQ(geditor::decode_as("\xEF\xBB\xBFx", bom).text, std::wstring(L"x"));
  // ...and one with BOM reads a file without it.
  CHECK_EQ(geditor::decode_as("x", bom).text, std::wstring(L"x"));
  // Line ends split on UTF-16 the same way for every encoding.
  FileFormat le;
  le.codec = Codec::kUtf16Le;
  const DecodedText mixed = geditor::decode_as(utf16(L"a\rb\r\nc\nd", false), le);
  CHECK_EQ(mixed.text, std::wstring(L"a\nb\nc\nd"));
  CHECK_EQ(mixed.eol_counts[0] + mixed.eol_counts[1] + mixed.eol_counts[2], size_t{3});
  CHECK_EQ(mixed.eol_runs.size(), size_t{3});
  // An empty file in each encoding.
  for (const wchar_t* name : geditor::encoding_names()) {
    FileFormat f;
    CHECK(geditor::parse_encoding(name, &f));
    const DecodedText d = geditor::decode_as("", f);
    CHECK(d.text.empty() && !d.format.undecodable);
  }
  CHECK(geditor::decode_detected("").format.codec == Codec::kUtf8);
}
