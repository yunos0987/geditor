// M4a line ends (docs/PLAN.md 2 "internal line ends" ... "writing to the
// clipboard", decisions F1, F7, F8): the piece tree's line-end attribute,
// the round trips of [C2], one test per PLAN 2 rule and example ([C3]), and
// the marks and the mode line of a mixed file ([C6]).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "clipboard.h"
#include "display.h"
#include "encoding.h"
#include "fake_host.h"
#include "piece_tree.h"

using geditor::DisplayRow;
using geditor::Eol;
using geditor::PieceTree;
using geditor::RowPos;
using test::Session;

namespace {

constexpr wchar_t kPath[] = L"C:\\work\\m.txt";

// Opens `bytes` as kPath in a fresh session (no undo history).
void open_bytes(Session& s, const std::string& bytes, const std::wstring& path = kPath) {
  s.host.add_file(path, bytes);
  s.ed.request_open(path);
  CHECK(s.ed.buffer().path == path);
}

// What saving the buffer now would write.
std::string bytes_of(const Session& s) {
  return geditor::encode_utf8(s.ed.buffer().text, s.ed.buffer().format);
}

// Saves with C-x C-s and returns the bytes on the fake disk.
std::string save(Session& s) {
  s.keys(L"C-x C-s");
  return s.host.file(s.ed.buffer().path);
}

void set_eol(Session& s, const wchar_t* name) {
  s.keys(L"C-x RET e");
  s.type(name);
  s.keys(L"RET");
}

// [C3] last item: one undo gives `before` back byte for byte, one redo
// `after`, both as saved.
void check_undo_redo(Session& s, const std::string& before, const std::string& after) {
  CHECK_EQ(bytes_of(s), after);
  s.keys(L"C-/");
  CHECK_EQ(bytes_of(s), before);
  CHECK_EQ(save(s), before);
  s.keys(L"C-?");
  CHECK_EQ(bytes_of(s), after);
  CHECK_EQ(save(s), after);
}

DisplayRow row_of(const Session& s, size_t line) {
  DisplayRow r;
  s.ed.display_row(RowPos{line, 0}, 0, 200, &r);
  return r;
}

const char* eol_bytes(Eol e) {
  switch (e) {
    case Eol::kLf: return "\n";
    case Eol::kCrLf: return "\r\n";
    case Eol::kCr: return "\r";
  }
  return "";
}

}  // namespace

// ---- Piece tree: the line-end attribute ----

TEST(m4a_piece_tree_runs_split_pieces_and_count_kinds) {
  // "a\nb\nc\nd" with CRLF, LF, CR line ends: runs start after the last '\n'
  // of the previous kind.
  std::wstring text = L"a\nb\nc\nd";
  std::vector<uint32_t> lf = {1, 3, 5};
  const std::vector<geditor::EolRun> runs = {{0, Eol::kCrLf}, {2, Eol::kLf}, {4, Eol::kCr}};
  const PieceTree t(text, lf, runs);
  CHECK_EQ(t.text(), text);
  CHECK_EQ(t.piece_count(), size_t{3});
  CHECK_EQ(t.eol_count(Eol::kCrLf), size_t{1});
  CHECK_EQ(t.eol_count(Eol::kLf), size_t{1});
  CHECK_EQ(t.eol_count(Eol::kCr), size_t{1});
  CHECK(t.line_eol(0) == Eol::kCrLf);
  CHECK(t.line_eol(1) == Eol::kLf);
  CHECK(t.line_eol(2) == Eol::kCr);
}

TEST(m4a_piece_tree_insert_takes_the_given_line_end) {
  PieceTree t(L"ab");
  t.insert(1, L"\n", Eol::kCrLf);
  t.insert(3, L"\n", Eol::kCr);
  CHECK_EQ(t.text(), std::wstring(L"a\nb\n"));
  CHECK(t.line_eol(0) == Eol::kCrLf);
  CHECK(t.line_eol(1) == Eol::kCr);
  CHECK_EQ(t.eol_count(Eol::kLf), size_t{0});
  // Typing at the end extends the piece only with the same line end.
  t.insert(4, L"x\n", Eol::kCr);
  t.insert(6, L"y\n", Eol::kLf);
  CHECK(t.line_eol(2) == Eol::kCr);
  CHECK(t.line_eol(3) == Eol::kLf);
}

TEST(m4a_piece_tree_assign_pieces_keeps_text_changes_line_ends) {
  PieceTree t(L"a\nb\nc");
  geditor::PieceList pieces = t.pieces();
  for (geditor::Piece& p : pieces) p.eol = Eol::kCr;
  const uint64_t revision = t.revision();
  t.assign_pieces(pieces);
  CHECK(t.revision() != revision);
  CHECK_EQ(t.text(), std::wstring(L"a\nb\nc"));
  CHECK_EQ(t.eol_count(Eol::kCr), size_t{2});
  CHECK_EQ(t.eol_count(Eol::kLf), size_t{0});
}

// Random inserts and erases with random line ends against a model that keeps
// the line end of each character ([M1-1] style, 4,000 operations).
TEST(m4a_piece_tree_random_line_ends_match_a_model) {
  PieceTree t;
  std::wstring model;
  std::vector<Eol> kinds;  // per character; only '\n' matters
  uint32_t state = 12345;
  auto next = [&]() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  };
  for (int op = 0; op < 4000; ++op) {
    const size_t len = model.size();
    if (len == 0 || next() % 3 != 0) {
      const size_t at = len == 0 ? 0 : next() % (len + 1);
      const Eol e = static_cast<Eol>(next() % 3);
      std::wstring piece;
      const size_t n = 1 + next() % 5;
      for (size_t i = 0; i < n; ++i) piece += next() % 3 == 0 ? L'\n' : static_cast<wchar_t>(L'a' + next() % 26);
      t.insert(at, piece, e);
      model.insert(at, piece);
      kinds.insert(kinds.begin() + static_cast<std::ptrdiff_t>(at), piece.size(), e);
    } else {
      const size_t at = next() % len;
      const size_t n = std::min<size_t>(1 + next() % 6, len - at);
      t.erase(at, n);
      model.erase(at, n);
      kinds.erase(kinds.begin() + static_cast<std::ptrdiff_t>(at), kinds.begin() + static_cast<std::ptrdiff_t>(at + n));
    }
    if (op % 97 == 0 || op == 3999) {
      size_t counts[3] = {0, 0, 0};
      std::string bytes;
      size_t line = 0;
      bool lines_ok = true;
      for (size_t i = 0; i < model.size(); ++i) {
        if (model[i] == L'\n') {
          ++counts[static_cast<size_t>(kinds[i])];
          bytes += eol_bytes(kinds[i]);
          lines_ok = lines_ok && t.line_eol(line) == kinds[i];
          ++line;
        } else {
          bytes += static_cast<char>(model[i]);
        }
      }
      CHECK(lines_ok);
      CHECK_EQ(t.text(), model);
      CHECK_EQ(t.eol_count(Eol::kLf), counts[0]);
      CHECK_EQ(t.eol_count(Eol::kCrLf), counts[1]);
      CHECK_EQ(t.eol_count(Eol::kCr), counts[2]);
      CHECK_EQ(geditor::encode_utf8(t, geditor::FileFormat()), bytes);
    }
  }
}

// ---- [C2] (M4-1 subset): utf-8 / utf-8-bom x LF / CRLF / CR / mixed ----

TEST(m4a_round_trip_utf8_and_bom_with_four_line_end_kinds) {
  const char* const kinds[] = {"lf", "crlf", "cr", "mixed"};
  for (const bool bom : {false, true}) {
    for (const char* kind : kinds) {
      // Japanese, an emoji, an empty line and a last line without a line end.
      const std::string k = kind;
      auto e = [&](int i) -> std::string {
        if (k == "lf") return "\n";
        if (k == "crlf") return "\r\n";
        if (k == "cr") return "\r";
        const char* mixed[] = {"\n", "\r\n", "\r"};
        return mixed[i % 3];
      };
      std::string bytes = bom ? "\xEF\xBB\xBF" : "";
      bytes += "line one" + e(0) + "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" + e(1) + e(2) + "\xF0\x9F\x98\x80 x" +
               e(3) + "last";
      Session s;
      open_bytes(s, bytes);
      if (bytes_of(s) != bytes) std::printf("  bom=%d %s\n", bom ? 1 : 0, kind);
      CHECK_EQ(bytes_of(s), bytes);
      CHECK_EQ(s.ed.buffer().format.bom, bom);
      // Load -> save through the editor: an edit and its removal make the
      // buffer modified with the same text.
      s.keys(L"M->");
      s.type(L"z");
      s.keys(L"DEL");
      CHECK(s.modified());
      CHECK_EQ(save(s), bytes);
      CHECK(!s.modified());
    }
  }
}

TEST(m4a_editing_one_line_of_a_mixed_file_keeps_the_others) {
  Session s;
  const std::string before = "l1\r\nl2\nl3\rl4\r\nl5";
  open_bytes(s, before);
  s.keys(L"C-n C-n C-e");  // the end of "l3" (a CR line)
  s.type(L"X");
  s.keys(L"C-n C-a");      // "l4"
  s.keys(L"C-d");
  const std::string after = "l1\r\nl2\nl3X\r4\r\nl5";
  CHECK_EQ(save(s), after);
}

// ---- [C3] (M4-6): one test per PLAN 2 rule and example ----

// "newly entered line ends": a typed newline gets the default line end.
TEST(m4a_typed_newline_gets_the_default_line_end) {
  Session s;
  const std::string before = "a\r\nb\nc\r\n";  // default CRLF (2 against 1)
  open_bytes(s, before);
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(CRLF)"));
  s.keys(L"M->");
  s.type(L"d");
  s.keys(L"RET");
  const std::string after = "a\r\nb\nc\r\nd\r\n";
  CHECK_EQ(bytes_of(s), after);
  s.keys(L"C-/");  // the newline
  CHECK_EQ(bytes_of(s), std::string("a\r\nb\nc\r\nd"));
  s.keys(L"C-?");
  CHECK_EQ(bytes_of(s), after);
  s.keys(L"C-/ C-/");
  CHECK_EQ(bytes_of(s), before);
  CHECK_EQ(save(s), before);
}

// "newly entered line ends": "\r\n" and a lone "\r" in pasted text become
// one '\n' with the default line end.
TEST(m4a_pasted_crlf_and_cr_become_default_line_ends) {
  Session s;
  const std::string before = "a\nb\rc\r";  // default CR (2 against 1)
  open_bytes(s, before);
  s.host.clipboard = L"x\r\ny\rz";
  s.host.clipboard_has_text = true;
  s.keys(L"M->");
  s.keys(L"C-y");
  CHECK_EQ(s.text(), std::wstring(L"a\nb\nc\nx\ny\nz"));
  check_undo_redo(s, before, "a\nb\rc\rx\ry\rz");
}

// The same for text that the IME commits.
TEST(m4a_ime_text_with_crlf_gets_the_default_line_end) {
  Session s;
  open_bytes(s, "a\r\n");
  s.keys(L"M->");
  s.ed.insert_text(L"p\r\nq\rr\n");
  CHECK_EQ(bytes_of(s), std::string("a\r\np\r\nq\r\nr\r\n"));
}

// "mixed files": the default is the most frequent line end, ties CRLF > LF
// > CR (decision F1).
TEST(m4a_default_line_end_is_the_most_frequent_with_ties) {
  struct Row {
    const char* bytes;
    const wchar_t* eol;
  };
  const Row rows[] = {
      {"a\nb\r\n", L"Mixed(CRLF)"},     // LF = CRLF: CRLF
      {"a\nb\r", L"Mixed(LF)"},         // LF = CR: LF
      {"a\r\nb\r", L"Mixed(CRLF)"},     // CRLF = CR: CRLF
      {"a\nb\r\nc\r", L"Mixed(CRLF)"},  // all equal: CRLF
      {"a\rb\rc\n", L"Mixed(CR)"},      // CR most
      {"a\nb\nc\r\n", L"Mixed(LF)"},    // LF most
      {"a\r\nb\r\n", L"CRLF"},
      {"a\rb\r", L"CR"},
      {"a\nb", L"LF"},
  };
  for (const Row& r : rows) {
    Session s;
    open_bytes(s, r.bytes);
    if (s.ed.eol_text() != r.eol) std::printf("  %s\n", test::to_display(std::string(r.bytes)).c_str());
    CHECK_EQ(s.ed.eol_text(), std::wstring(r.eol));
    CHECK_EQ(bytes_of(s), std::string(r.bytes));
  }
}

// "files without line ends" (decision F1): new_file.eol.
TEST(m4a_file_without_line_ends_uses_new_file_eol) {
  Session s;
  s.settings(R"({"new_file": {"eol": "crlf"}})");
  open_bytes(s, "one line");
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"CRLF"));
  s.keys(L"C-e RET");
  CHECK_EQ(bytes_of(s), std::string("one line\r\n"));
  Session n;
  n.settings(R"({"new_file": {"eol": "cr"}})");
  CHECK_EQ(n.ed.eol_text(), std::wstring(L"CR"));  // the untitled buffer
  n.ed.request_open(L"C:\\work\\new.txt");          // a new file
  CHECK_EQ(n.ed.eol_text(), std::wstring(L"CR"));
  n.type(L"a");
  n.keys(L"RET");
  CHECK_EQ(save(n), std::string("a\r"));
}

// "joining and splitting lines": the example a⏎b↓c⏎ -> ab↓c⏎ -> a⏎b↓c⏎.
TEST(m4a_join_and_split_example) {
  Session s;
  const std::string start = "a\r\nb\nc\r\n";
  open_bytes(s, start);
  s.keys(L"C-f C-d");  // delete the CRLF after "a"
  const std::string joined = "ab\nc\r\n";
  CHECK_EQ(bytes_of(s), joined);
  s.keys(L"RET");      // split between "a" and "b"
  const std::string split = "a\r\nb\nc\r\n";
  CHECK_EQ(bytes_of(s), split);
  CHECK_EQ(save(s), split);
  // Undo / redo of each step.
  s.keys(L"C-/");
  CHECK_EQ(bytes_of(s), joined);
  s.keys(L"C-/");
  CHECK_EQ(bytes_of(s), start);
  s.keys(L"C-?");
  CHECK_EQ(bytes_of(s), joined);
  s.keys(L"C-?");
  CHECK_EQ(bytes_of(s), split);
}

// Splitting a line in the middle: the first half ends in the default, the
// second half keeps its own line end.
TEST(m4a_split_keeps_the_line_end_of_the_second_half) {
  Session s;
  const std::string before = "ab\ncd\r\nef\r\n";  // default CRLF
  open_bytes(s, before);
  s.keys(L"C-f");
  s.keys(L"RET");
  check_undo_redo(s, before, "a\r\nb\ncd\r\nef\r\n");
}

// "joining": the joined line ends in the line end of the later line.
TEST(m4a_join_keeps_the_later_line_end) {
  Session s;
  const std::string before = "x\ny\rz\n";  // default LF; "y" ends in CR
  open_bytes(s, before);
  s.keys(L"C-e C-d");  // join "x" and "y"
  check_undo_redo(s, before, "xy\rz\n");
}

// "joining and splitting lines": a line end typed after a last line without
// one is the default.
TEST(m4a_newline_after_the_last_line_without_one) {
  for (const char* before : {"a\r\nb", "a\rb", "a\nb"}) {
    Session s;
    open_bytes(s, before);
    s.keys(L"M-> RET");
    const std::string b = before;
    const std::string after = b + b.substr(1, b.size() - 2);  // the file's line end
    CHECK_EQ(bytes_of(s), after);
    check_undo_redo(s, b, after);
  }
}

// "default line end and Mixed": removing every line end that differs from
// the default turns Mixed(...) into the default alone; undo brings it back.
TEST(m4a_removing_other_line_ends_ends_mixed) {
  Session s;
  const std::string before = "a\r\nb\nc\r\n";
  open_bytes(s, before);
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(CRLF)"));
  CHECK_EQ(s.ed.other_line_ends(), size_t{1});
  s.keys(L"C-n C-e C-d");  // the LF after "b"
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"CRLF"));
  CHECK(s.ed.mode_line().find(L"UTF-8 CRLF  L2") != std::wstring::npos);
  check_undo_redo(s, before, "a\r\nbc\r\n");
  // The default never changes by editing: deleting every CRLF of a mixed
  // file shows Mixed(CRLF) while the LF stays.
  Session t;
  open_bytes(t, "a\r\nb\n");
  t.keys(L"C-e C-d");
  CHECK_EQ(t.ed.eol_text(), std::wstring(L"Mixed(CRLF)"));
}

// "changing the line end": set-eol unifies every line, makes it the default,
// and one undo restores the mixed state and the old default (decision F7).
TEST(m4a_set_eol_unifies_and_one_undo_restores_the_mix) {
  Session s;
  const std::string before = "a\r\nb\nc\rd";  // one of each: default CRLF
  open_bytes(s, before);
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(CRLF)"));
  set_eol(s, L"cr");
  CHECK_EQ(s.message(), std::wstring(L"Line endings: CR"));
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"CR"));
  CHECK(s.modified());
  CHECK_EQ(s.text(), std::wstring(L"a\nb\nc\nd"));
  check_undo_redo(s, before, "a\rb\rc\rd");
  s.keys(L"C-/");
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(CRLF)"));
  CHECK(s.ed.buffer().format.eol == Eol::kCrLf);
  // A newline typed now gets the restored default.
  s.keys(L"M-> RET");
  CHECK_EQ(bytes_of(s), std::string("a\r\nb\nc\rd\r\n"));
}

TEST(m4a_set_eol_keeps_point_mark_and_text) {
  Session s;
  open_bytes(s, "ab\r\ncd\nef");
  s.keys(L"C-f C-SPC C-n");
  const size_t point = s.point();
  set_eol(s, L"lf");
  CHECK_EQ(s.point(), point);
  CHECK(s.ed.buffer().has_mark);
  CHECK_EQ(s.ed.buffer().mark, size_t{1});
  s.keys(L"C-/");
  CHECK_EQ(s.ed.buffer().mark, size_t{1});
}

TEST(m4a_set_eol_without_a_change_leaves_the_buffer_unmodified) {
  Session s;
  open_bytes(s, "a\r\nb\r\n");
  set_eol(s, L"crlf");
  CHECK_EQ(s.message(), std::wstring(L"Line endings are already CRLF"));
  CHECK(!s.modified());
  s.keys(L"C-/");
  CHECK_EQ(s.message(), std::wstring(L"No further undo information"));
  // Only the default changes (no line end to convert): one undo group.
  Session t;
  open_bytes(t, "no line end");
  set_eol(t, L"cr");
  CHECK_EQ(t.ed.eol_text(), std::wstring(L"CR"));
  CHECK(t.modified());
  t.keys(L"C-/");
  CHECK_EQ(t.ed.eol_text(), std::wstring(L"LF"));
  CHECK(!t.modified());
}

TEST(m4a_set_eol_rejects_an_unknown_name) {
  Session s;
  open_bytes(s, "a\nb");
  set_eol(s, L"dos");
  CHECK_EQ(s.message(), std::wstring(L"[No match]"));
  CHECK(s.ed.minibuffer_active());
  s.keys(L"C-g");
  CHECK(!s.modified());
  // The settings refuse it too, so <f12> stays unbound.
  s.settings(R"({"keys": {"<f12>": ["set-eol", "dos"]}})");
  CHECK(!s.ed.settings_errors().empty());
  s.keys(L"<f12>");
  CHECK(!s.modified());
}

// "writing to the clipboard": kill and copy put "\n" text in the host, and
// the Win32 layer writes it as CRLF whatever each line's own line end.
TEST(m4a_kill_and_copy_write_crlf_to_the_clipboard) {
  Session s;
  open_bytes(s, "a\rb\nc\r\nd");
  s.keys(L"C-SPC M->");
  s.keys(L"M-w");
  CHECK_EQ(s.host.clipboard, std::wstring(L"a\nb\nc\nd"));
  CHECK_EQ(geditor::win::to_clipboard_text(s.host.clipboard), std::wstring(L"a\r\nb\r\nc\r\nd"));
  s.keys(L"M-< C-e C-k");  // kills the CR line end after "a"
  CHECK_EQ(s.host.clipboard, std::wstring(L"\n"));
  CHECK_EQ(geditor::win::to_clipboard_text(s.host.clipboard), std::wstring(L"\r\n"));
  check_undo_redo(s, "a\rb\nc\r\nd", "ab\nc\r\nd");
}

// ---- [C6] (M4-8 core, decision F8): marks, colours and the mode line ----

TEST(m4a_mixed_file_marks_each_line_in_its_own_symbol_and_colour) {
  Session s;
  open_bytes(s, "a\r\nb\nc\rd");
  const DisplayRow r0 = row_of(s, 0);
  const DisplayRow r1 = row_of(s, 1);
  const DisplayRow r2 = row_of(s, 2);
  CHECK(r0.eol.shown && r1.eol.shown && r2.eol.shown);
  CHECK_EQ(r0.eol.symbol, std::wstring(L"\u21B5"));  // CRLF
  CHECK_EQ(r1.eol.symbol, std::wstring(L"\u2193"));  // LF
  CHECK_EQ(r2.eol.symbol, std::wstring(L"\u2190"));  // CR
  CHECK(r0.eol.color == geditor::kColorEol);         // the default
  CHECK(r1.eol.color == geditor::kColorEolOther);
  CHECK(r2.eol.color == geditor::kColorEolOther);
  CHECK(!row_of(s, 3).eol.shown);                    // no line end
  // After set-eol every line is the default.
  set_eol(s, L"lf");
  for (size_t line = 0; line < 3; ++line) {
    CHECK_EQ(row_of(s, line).eol.symbol, std::wstring(L"\u2193"));
    CHECK(row_of(s, line).eol.color == geditor::kColorEol);
  }
}

TEST(m4a_cr_file_marks_and_custom_symbols) {
  Session s;
  s.settings(R"({"eol_marks": {"lf": "L", "crlf": "W", "cr": "M"}})");
  open_bytes(s, "a\rb\nc\r");
  CHECK_EQ(row_of(s, 0).eol.symbol, std::wstring(L"M"));
  CHECK_EQ(row_of(s, 1).eol.symbol, std::wstring(L"L"));
  CHECK(row_of(s, 0).eol.color == geditor::kColorEol);
  CHECK(row_of(s, 1).eol.color == geditor::kColorEolOther);
}

TEST(m4a_mode_line_shows_the_line_end_part) {
  Session s;
  open_bytes(s, "a\r\nb\nc\rd");
  CHECK_EQ(s.ed.mode_line(), std::wstring(L"--- m.txt  UTF-8 Mixed(CRLF)  L1 C0  wrap:max"));
  Session c;
  open_bytes(c, "\xEF\xBB\xBF" "a\rb");
  CHECK_EQ(c.ed.mode_line(), std::wstring(L"--- m.txt  UTF-8 BOM CR  L1 C0  wrap:max"));
}
