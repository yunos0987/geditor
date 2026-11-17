// M4a fix F1 (docs/PLAN.md 2 "saving a CR line end before an LF line end"):
// an LF line end right after a CR line end would be written "\r" "\n" and
// read back as one CRLF, losing a line. Saving writes it as CR, the buffer
// gets the same line ends (not an undo step), and the echo area gives the
// count. Fake host only; the real files are in tools/app_check.py --only-m4a.

#include <string>
#include <vector>

#include "encoding.h"
#include "fake_host.h"
#include "piece_tree.h"

using geditor::Eol;
using test::FakeHost;
using test::Session;

namespace {

constexpr wchar_t kPath[] = L"C:\\work\\f1.txt";

void open_bytes(Session& s, const std::string& bytes) {
  s.host.add_file(kPath, bytes);
  s.ed.request_open(kPath);
  CHECK(s.ed.buffer().path == kPath);
}

// The line end of every line but the last.
std::vector<Eol> line_ends(const geditor::PieceTree& tree) {
  std::vector<Eol> ends;
  for (size_t line = 0; line < tree.line_feed_count(); ++line) ends.push_back(tree.line_eol(line));
  return ends;
}

std::string eol_names(const std::vector<Eol>& ends) {
  std::string text;
  for (const Eol e : ends) text += e == Eol::kLf ? "L" : e == Eol::kCrLf ? "W" : "R";
  return text;
}

// Reads `bytes` again as a file would be opened.
geditor::DecodedText reread(const std::string& bytes) {
  return geditor::decode_utf8(bytes);
}

}  // namespace

// [C1] The user's case: "a" (CR), an empty LF line, "b" (CRLF), built by
// deleting the text of the LF line.
TEST(m4a_fix_lf_after_cr_is_saved_as_cr_and_no_line_is_lost) {
  Session s;
  open_bytes(s, "a\rx\nb\r\n");
  CHECK_EQ(s.ed.buffer().text.line_count(), size_t{4});
  s.set_point(2);  // before "x"
  s.keys(L"C-d");
  CHECK_EQ(s.text(), std::wstring(L"a\n\nb\n"));
  CHECK_EQ(eol_names(line_ends(s.ed.buffer().text)), std::string("RLW"));
  // HEAD edef20c wrote "a\r\nb\r\n" here: three lines when read again.
  s.keys(L"C-x C-s");
  const std::string saved = s.host.file(kPath);
  CHECK_EQ(saved, std::string("a\r\rb\r\n"));
  CHECK(saved.find("\r\n") == saved.size() - 2);  // only the CRLF of "b"
  CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\f1.txt; saved 1 LF line end after a CR as CR to keep the lines"));
  CHECK(!s.modified());
  // The buffer has the line ends of the file: marks and mode line match it.
  CHECK_EQ(eol_names(line_ends(s.ed.buffer().text)), std::string("RRW"));
  CHECK_EQ(s.text(), std::wstring(L"a\n\nb\n"));
  geditor::DecodedText again = reread(saved);
  CHECK(again.status == geditor::DecodeStatus::kOk);
  CHECK_EQ(again.text, std::wstring(L"a\n\nb\n"));
  CHECK_EQ(again.line_feeds.size() + 1, s.ed.buffer().text.line_count());
  const geditor::PieceTree reread_tree = geditor::make_piece_tree(again);
  CHECK_EQ(eol_names(line_ends(reread_tree)), eol_names(line_ends(s.ed.buffer().text)));
  // Deferred fix F1: the default line end is now the file's most frequent
  // one, CR (HEAD 6064002 kept CRLF, which reopening does not give).
  CHECK_EQ(s.ed.eol_text(), std::wstring(L"Mixed(CR)"));
  // Opening the saved file gives the same lines.
  Session t;
  open_bytes(t, saved);
  CHECK_EQ(t.text(), s.text());
  CHECK_EQ(eol_names(line_ends(t.ed.buffer().text)), std::string("RRW"));
  CHECK_EQ(t.ed.eol_text(), s.ed.eol_text());
}

// A run of LF line ends after one CR: each follows a CR once the one before
// it is CR, so all of them change; the count says so.
TEST(m4a_fix_a_run_of_lf_after_cr_all_become_cr) {
  Session s;
  open_bytes(s, "a\rx\ny\nb");
  s.set_point(2);
  s.keys(L"C-d C-f C-d");  // remove "x" and "y"
  CHECK_EQ(s.text(), std::wstring(L"a\n\n\nb"));
  s.keys(L"C-x C-s");
  const std::string saved = s.host.file(kPath);
  CHECK_EQ(saved, std::string("a\r\r\rb"));
  CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\f1.txt; saved 2 LF line ends after a CR as CR to keep the lines"));
  CHECK_EQ(reread(saved).text, s.text());
  CHECK_EQ(eol_names(line_ends(s.ed.buffer().text)), std::string("RRR"));
}

// Not an undo step: undo after the save takes back the deletion of "x" only;
// the line end made CR by the save stays CR (it is now the line end of "x").
TEST(m4a_fix_the_change_is_not_an_undo_step) {
  Session s;
  open_bytes(s, "a\rx\nb\r\n");
  s.set_point(2);
  s.keys(L"C-d");
  s.keys(L"C-x C-s");
  CHECK(!s.modified());
  s.keys(L"C-/");
  CHECK_EQ(s.text(), std::wstring(L"a\nx\nb\n"));
  CHECK(s.modified());
  CHECK_EQ(eol_names(line_ends(s.ed.buffer().text)), std::string("RRW"));
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(kPath), std::string("a\rx\rb\r\n"));
  CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\f1.txt"));
}

// A failed save changes nothing in the buffer.
TEST(m4a_fix_failed_save_keeps_the_line_ends) {
  Session s;
  open_bytes(s, "a\rx\nb\r\n");
  s.set_point(2);
  s.keys(L"C-d");
  s.host.fail_step = FakeHost::SaveStep::kTempWrite;
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"Cannot write C:\\work\\f1.txt: writing the temporary file: The disk is full"));
  CHECK(s.modified());
  CHECK_EQ(eol_names(line_ends(s.ed.buffer().text)), std::string("RLW"));
  CHECK_EQ(s.host.file(kPath), std::string("a\rx\nb\r\n"));
  // The next save that works does the change.
  s.keys(L"C-x C-s");
  CHECK_EQ(s.host.file(kPath), std::string("a\r\rb\r\n"));
  CHECK(!s.modified());
}

// [C1] negative: without an LF right after a CR the bytes are those of
// HEAD edef20c (each line end as it was) and there is no such message.
TEST(m4a_fix_other_line_end_orders_are_saved_unchanged) {
  struct Row {
    const char* before;
    const char* after;  // after typing "X" at the end
  };
  const Row rows[] = {
      {"a\rb\nc\r\nd", "a\rb\nc\r\ndX"},  // CR then text, then LF
      {"a\n\rb", "a\n\rbX"},              // LF then CR
      {"a\r\n\nb", "a\r\n\nbX"},          // CRLF then LF
      {"a\r\r\nb", "a\r\r\nbX"},          // CR then CRLF
      {"a\r\rb", "a\r\rbX"},              // CR then CR
      {"a\n\nb\r", "a\n\nb\rX"},          // LF only, CR last
      {"", "X"},
  };
  for (const Row& r : rows) {
    Session s;
    open_bytes(s, r.before);
    const std::vector<Eol> ends = line_ends(s.ed.buffer().text);
    s.keys(L"M->");
    s.type(L"X");
    s.keys(L"C-x C-s");
    CHECK_EQ(s.host.file(kPath), std::string(r.after));
    CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\f1.txt"));
    CHECK(line_ends(s.ed.buffer().text) == ends);
  }
}

// The function alone: the pieces keep the text, only the LF after a CR
// changes, and nothing is returned when there is nothing to change.
TEST(m4a_fix_cr_kept_line_ends_on_pieces) {
  // "p" CR, then "" LF, "" LF, "q" LF, "r" (a file cannot hold this: the
  // runs build it).
  std::wstring text = L"p\n\n\nq\nr";
  std::vector<uint32_t> feeds = {1, 2, 3, 5};
  std::vector<geditor::EolRun> runs = {{0, Eol::kCr}, {2, Eol::kLf}};
  geditor::PieceTree tree(text, feeds, runs);
  CHECK_EQ(eol_names(line_ends(tree)), std::string("RLLL"));
  geditor::PieceList pieces;
  CHECK_EQ(geditor::cr_kept_line_ends(tree, &pieces), size_t{2});
  tree.assign_pieces(pieces);
  CHECK_EQ(tree.text(), text);
  CHECK_EQ(eol_names(line_ends(tree)), std::string("RRRL"));
  CHECK_EQ(geditor::cr_kept_line_ends(tree, &pieces), size_t{0});
  CHECK(pieces.empty());
}
