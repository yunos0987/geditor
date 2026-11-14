// Text storage: a piece table kept in a balanced tree (treap). Each node holds
// one piece and the length / line-feed totals of its subtree, so insert, erase
// and offset <-> line conversions are O(log n). Text is UTF-16 with "\n" line
// ends. The buffers that pieces point into are never modified in place, so
// undo can keep pieces instead of copies of the text.
//
// Line ends (docs/PLAN.md 2 "internal line ends"): every piece carries the
// line end (LF / CRLF / CR) of the '\n' characters inside it, and the nodes
// sum the '\n' of each kind, so the number of line ends of any kind is O(1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace geditor {

enum class Eol : uint8_t { kLf, kCrLf, kCr };

struct Piece {
  uint32_t buffer = 0;  // 0 = original text, 1 = add buffer
  size_t start = 0;
  size_t length = 0;
  size_t line_feeds = 0;
  Eol eol = Eol::kLf;  // the line end of every '\n' in this piece
};

using PieceList = std::vector<Piece>;

size_t total_length(const PieceList& pieces);

// The line end of the '\n' characters from `start` (an offset into the
// original text) up to the start of the next run.
struct EolRun {
  size_t start = 0;
  Eol eol = Eol::kLf;
};

class PieceTree {
 public:
  PieceTree();
  explicit PieceTree(std::wstring original);
  // `line_feeds` must hold the offsets of every '\n' in `original`, ascending.
  // Every line end is LF.
  PieceTree(std::wstring original, std::vector<uint32_t> line_feeds);
  // `runs` (ascending starts, the first at 0; empty: one LF run) split the
  // text into pieces of one line end each.
  PieceTree(std::wstring original, std::vector<uint32_t> line_feeds, const std::vector<EolRun>& runs);
  ~PieceTree();
  PieceTree(const PieceTree&) = delete;
  PieceTree& operator=(const PieceTree&) = delete;
  PieceTree(PieceTree&& other) noexcept;
  PieceTree& operator=(PieceTree&& other) noexcept;

  size_t length() const;
  size_t line_feed_count() const;
  size_t line_count() const { return line_feed_count() + 1; }

  wchar_t char_at(size_t offset) const;
  std::wstring text() const;
  std::wstring substr(size_t offset, size_t count) const;

  // Inserts `text` at `offset` and returns the pieces that now hold it. Its
  // '\n' characters get the line end `eol`.
  PieceList insert(size_t offset, std::wstring_view text, Eol eol = Eol::kLf);
  // Re-inserts pieces previously returned by insert or erase.
  void insert_pieces(size_t offset, const PieceList& pieces);
  // Removes [offset, offset + count) and returns the removed pieces.
  PieceList erase(size_t offset, size_t count);

  // Line numbers are 0-based. Columns here are UTF-16 units.
  size_t line_of(size_t offset) const;
  size_t line_start(size_t line) const;
  // Offset of the line's '\n', or length() for the last line.
  size_t line_end(size_t line) const;
  size_t column_of(size_t offset) const { return offset - line_start(line_of(offset)); }
  // Clamped to the end of the line.
  size_t offset_of(size_t line, size_t column) const;

  // Calls f(std::wstring_view) for consecutive chunks covering
  // [offset, offset + count). f returns false to stop early.
  template <typename F>
  void for_each_chunk(size_t offset, size_t count, F&& f) const {
    visit_chunks(offset, count, &invoke_chunk<F>, &f);
  }
  // Like for_each_chunk with f(std::wstring_view, Eol): each chunk lies in
  // one piece and comes with that piece's line end.
  template <typename F>
  void for_each_chunk_eol(size_t offset, size_t count, F&& f) const {
    visit_chunks(offset, count, &invoke_chunk_eol<F>, &f);
  }

  // Number of '\n' whose line end is `eol`.
  size_t eol_count(Eol eol) const;
  // The line end of line `line` (0-based; must be below line_feed_count()).
  Eol line_eol(size_t line) const;
  // Every piece in text order.
  PieceList pieces() const;
  // Replaces every piece with `pieces`, which must hold the same text (the
  // pieces of pieces() with other line ends: set-eol and its undo).
  void assign_pieces(const PieceList& pieces);

  size_t piece_count() const;

  // Changes with every insert, insert_pieces and erase (by exactly 1) and
  // with every move into or out of this object (to a value this object never
  // had), so that caches of derived data can tell when to drop it.
  uint64_t revision() const { return revision_; }

 private:
  struct Node;
  struct TextBuffer {
    std::wstring text;
    std::vector<uint32_t> line_feeds;
  };
  using ChunkFn = bool (*)(void*, std::wstring_view, Eol);

  template <typename F>
  static bool invoke_chunk(void* ctx, std::wstring_view chunk, Eol) {
    return (*static_cast<F*>(ctx))(chunk);
  }
  template <typename F>
  static bool invoke_chunk_eol(void* ctx, std::wstring_view chunk, Eol eol) {
    return (*static_cast<F*>(ctx))(chunk, eol);
  }

  void visit_chunks(size_t offset, size_t count, ChunkFn fn, void* ctx) const;
  bool visit(const Node* n, size_t base, size_t lo, size_t hi, ChunkFn fn, void* ctx) const;

  size_t count_line_feeds(uint32_t buffer, size_t start, size_t end) const;
  Node* new_node(const Piece& piece);
  void split(Node* t, size_t offset, Node*& left, Node*& right);
  static Node* merge(Node* a, Node* b);
  static void update(Node* n);
  static void destroy(Node* n);
  static bool extend_rightmost(Node* n, const Piece& p);
  // Builds the tree of `pieces` (in text order) in O(n).
  void build(const PieceList& pieces);

  TextBuffer buffers_[2];
  Node* root_ = nullptr;
  uint32_t seed_ = 0x9E3779B9u;
  uint64_t revision_ = 0;
};

}  // namespace geditor
