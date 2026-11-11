// Piece tree tests, including the 10,000-operation random test of [M1-1].

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "piece_tree.h"
#include "test_harness.h"

using geditor::PieceList;
using geditor::PieceTree;

namespace {

// Naive model helpers.
size_t model_line_of(const std::wstring& s, size_t offset) {
  size_t line = 0;
  for (size_t i = 0; i < offset && i < s.size(); ++i) line += s[i] == L'\n' ? 1 : 0;
  return line;
}

std::vector<size_t> model_line_starts(const std::wstring& s) {
  std::vector<size_t> starts{0};
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == L'\n') starts.push_back(i + 1);
  }
  return starts;
}

struct Rng {
  uint32_t state;
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  size_t below(size_t n) { return n == 0 ? 0 : next() % n; }
};

}  // namespace

TEST(piece_tree_empty) {
  PieceTree t;
  CHECK_EQ(t.length(), size_t{0});
  CHECK_EQ(t.line_count(), size_t{1});
  CHECK_EQ(t.text(), std::wstring());
  CHECK_EQ(t.line_of(0), size_t{0});
  CHECK_EQ(t.line_start(0), size_t{0});
  CHECK_EQ(t.line_end(0), size_t{0});
}

TEST(piece_tree_insert_and_lines) {
  PieceTree t(L"abc\ndef");
  CHECK_EQ(t.line_count(), size_t{2});
  t.insert(3, L"X\nY");
  CHECK_EQ(t.text(), std::wstring(L"abcX\nY\ndef"));
  CHECK_EQ(t.line_count(), size_t{3});
  CHECK_EQ(t.line_start(1), size_t{5});
  CHECK_EQ(t.line_end(1), size_t{6});
  CHECK_EQ(t.line_start(2), size_t{7});
  CHECK_EQ(t.line_end(2), size_t{10});
  CHECK_EQ(t.line_of(6), size_t{1});
  CHECK_EQ(t.line_of(7), size_t{2});
  CHECK_EQ(t.column_of(9), size_t{2});
  CHECK_EQ(t.offset_of(0, 100), size_t{4});
}

TEST(piece_tree_erase_returns_pieces_for_reinsert) {
  PieceTree t(L"hello\nworld");
  t.insert(5, L"!!");
  const PieceList removed = t.erase(3, 6);
  CHECK_EQ(t.text(), std::wstring(L"helorld"));
  CHECK_EQ(geditor::total_length(removed), size_t{6});
  t.insert_pieces(3, removed);
  CHECK_EQ(t.text(), std::wstring(L"hello!!\nworld"));
  CHECK_EQ(t.line_count(), size_t{2});
}

TEST(piece_tree_typing_extends_one_piece) {
  PieceTree t(L"ab");
  for (int i = 0; i < 100; ++i) t.insert(1 + static_cast<size_t>(i), L"x");
  CHECK_EQ(t.length(), size_t{102});
  CHECK(t.piece_count() <= 3);
}

TEST(piece_tree_chunks_and_substr) {
  PieceTree t(L"0123456789");
  t.insert(5, L"abc");
  CHECK_EQ(t.substr(3, 6), std::wstring(L"34abc5"));
  CHECK_EQ(t.char_at(5), L'a');
  std::wstring joined;
  t.for_each_chunk(0, t.length(), [&](std::wstring_view c) {
    joined.append(c);
    return true;
  });
  CHECK_EQ(joined, t.text());
}

TEST(piece_tree_random_10000_operations_match_naive_model) {
  // Alphabet includes '\n' and a surrogate pair (U+1F600).
  const std::wstring alphabet[] = {L"a", L"b", L"\x3042", L"\n", L"\U0001F600", L"xy\nz", L"\n\n"};
  Rng rng{12345};
  PieceTree t;
  std::wstring model;
  bool ok = true;
  for (int op = 0; op < 10000 && ok; ++op) {
    const bool do_insert = model.empty() || rng.below(100) < 60 || model.size() < 50;
    if (do_insert && model.size() < 4000) {
      const size_t at = rng.below(model.size() + 1);
      std::wstring s;
      const size_t parts = 1 + rng.below(3);
      for (size_t i = 0; i < parts; ++i) s += alphabet[rng.below(std::size(alphabet))];
      t.insert(at, s);
      model.insert(at, s);
    } else {
      const size_t at = rng.below(model.size());
      const size_t count = 1 + rng.below(std::min<size_t>(20, model.size() - at));
      t.erase(at, count);
      model.erase(at, count);
    }
    // Compare everything after each operation.
    if (t.text() != model || t.length() != model.size()) ok = false;
    const std::vector<size_t> starts = model_line_starts(model);
    if (t.line_count() != starts.size()) ok = false;
    for (size_t line = 0; ok && line < starts.size(); ++line) {
      const size_t end = line + 1 < starts.size() ? starts[line + 1] - 1 : model.size();
      if (t.line_start(line) != starts[line] || t.line_end(line) != end) ok = false;
    }
    for (int k = 0; ok && k < 8; ++k) {
      const size_t off = rng.below(model.size() + 1);
      const size_t line = model_line_of(model, off);
      if (t.line_of(off) != line) ok = false;
      if (t.column_of(off) != off - starts[line]) ok = false;
      if (t.offset_of(line, off - starts[line]) != off) ok = false;
    }
    if (!ok) {
      std::printf("  mismatch after operation %d\n", op);
    }
  }
  CHECK(ok);
  CHECK_EQ(t.text(), model);
}
