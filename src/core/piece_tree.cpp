#include "piece_tree.h"

#include <algorithm>
#include <utility>

namespace geditor {

struct PieceTree::Node {
  Piece piece;
  uint32_t priority = 0;
  Node* left = nullptr;
  Node* right = nullptr;
  size_t sum_length = 0;
  size_t sum_line_feeds = 0;
  // '\n' of the subtree with CRLF / CR line ends (LF: the rest).
  size_t sum_crlf = 0;
  size_t sum_cr = 0;
};

size_t total_length(const PieceList& pieces) {
  size_t n = 0;
  for (const Piece& p : pieces) n += p.length;
  return n;
}

PieceTree::PieceTree() = default;

PieceTree::PieceTree(std::wstring original) {
  std::vector<uint32_t> lf;
  for (size_t i = 0; i < original.size(); ++i) {
    if (original[i] == L'\n') lf.push_back(static_cast<uint32_t>(i));
  }
  *this = PieceTree(std::move(original), std::move(lf));
}

PieceTree::PieceTree(std::wstring original, std::vector<uint32_t> line_feeds)
    : PieceTree(std::move(original), std::move(line_feeds), std::vector<EolRun>()) {}

PieceTree::PieceTree(std::wstring original, std::vector<uint32_t> line_feeds, const std::vector<EolRun>& runs) {
  buffers_[0].text = std::move(original);
  buffers_[0].line_feeds = std::move(line_feeds);
  const size_t length = buffers_[0].text.size();
  if (length == 0) return;
  const std::vector<uint32_t>& lf = buffers_[0].line_feeds;
  if (runs.size() <= 1) {
    Piece p;
    p.length = length;
    p.line_feeds = lf.size();
    p.eol = runs.empty() ? Eol::kLf : runs.front().eol;
    root_ = new_node(p);
    return;
  }
  PieceList pieces;
  pieces.reserve(runs.size());
  size_t lf_index = 0;
  for (size_t i = 0; i < runs.size(); ++i) {
    const size_t start = i == 0 ? 0 : std::min(runs[i].start, length);
    const size_t end = i + 1 < runs.size() ? std::min(runs[i + 1].start, length) : length;
    if (end <= start) continue;
    size_t count = 0;
    while (lf_index < lf.size() && lf[lf_index] < end) {
      ++lf_index;
      ++count;
    }
    Piece p;
    p.start = start;
    p.length = end - start;
    p.line_feeds = count;
    p.eol = runs[i].eol;
    pieces.push_back(p);
  }
  build(pieces);
}

void PieceTree::build(const PieceList& pieces) {
  // Cartesian tree on the treap priorities: the right spine on a stack.
  std::vector<Node*> spine;
  for (const Piece& p : pieces) {
    if (p.length == 0) continue;
    Node* n = new_node(p);
    Node* last = nullptr;
    while (!spine.empty() && spine.back()->priority < n->priority) {
      last = spine.back();
      spine.pop_back();
      update(last);
    }
    n->left = last;
    if (!spine.empty()) spine.back()->right = n;
    spine.push_back(n);
  }
  while (!spine.empty()) {
    update(spine.back());
    if (spine.size() == 1) root_ = spine.back();
    spine.pop_back();
  }
}

size_t PieceTree::eol_count(Eol eol) const {
  if (root_ == nullptr) return 0;
  switch (eol) {
    case Eol::kCrLf: return root_->sum_crlf;
    case Eol::kCr: return root_->sum_cr;
    case Eol::kLf: break;
  }
  return root_->sum_line_feeds - root_->sum_crlf - root_->sum_cr;
}

Eol PieceTree::line_eol(size_t line) const {
  // The piece that holds the line's '\n'.
  const size_t offset = line_end(line);
  const Node* n = root_;
  size_t off = offset;
  while (n != nullptr) {
    const size_t ll = n->left != nullptr ? n->left->sum_length : 0;
    if (off < ll) {
      n = n->left;
      continue;
    }
    off -= ll;
    if (off < n->piece.length) return n->piece.eol;
    off -= n->piece.length;
    n = n->right;
  }
  return Eol::kLf;
}

PieceList PieceTree::pieces() const {
  PieceList out;
  std::vector<const Node*> stack;
  const Node* n = root_;
  while (n != nullptr || !stack.empty()) {
    while (n != nullptr) {
      stack.push_back(n);
      n = n->left;
    }
    n = stack.back();
    stack.pop_back();
    out.push_back(n->piece);
    n = n->right;
  }
  return out;
}

void PieceTree::assign_pieces(const PieceList& pieces) {
  destroy(root_);
  root_ = nullptr;
  build(pieces);
  ++revision_;
}

PieceTree::~PieceTree() {
  destroy(root_);
}

PieceTree::PieceTree(PieceTree&& other) noexcept
    : root_(other.root_), seed_(other.seed_), revision_(other.revision_) {
  buffers_[0] = std::move(other.buffers_[0]);
  buffers_[1] = std::move(other.buffers_[1]);
  other.root_ = nullptr;
  ++other.revision_;
}

PieceTree& PieceTree::operator=(PieceTree&& other) noexcept {
  if (this != &other) {
    destroy(root_);
    buffers_[0] = std::move(other.buffers_[0]);
    buffers_[1] = std::move(other.buffers_[1]);
    root_ = other.root_;
    seed_ = other.seed_;
    other.root_ = nullptr;
    // Never a value this object had before: caches keyed on it see a change.
    revision_ = std::max(revision_, other.revision_) + 1;
    ++other.revision_;
  }
  return *this;
}

size_t PieceTree::length() const {
  return root_ != nullptr ? root_->sum_length : 0;
}

size_t PieceTree::line_feed_count() const {
  return root_ != nullptr ? root_->sum_line_feeds : 0;
}

size_t PieceTree::piece_count() const {
  size_t count = 0;
  std::vector<const Node*> stack;
  if (root_ != nullptr) stack.push_back(root_);
  while (!stack.empty()) {
    const Node* n = stack.back();
    stack.pop_back();
    ++count;
    if (n->left != nullptr) stack.push_back(n->left);
    if (n->right != nullptr) stack.push_back(n->right);
  }
  return count;
}

void PieceTree::update(Node* n) {
  n->sum_length = n->piece.length;
  n->sum_line_feeds = n->piece.line_feeds;
  n->sum_crlf = n->piece.eol == Eol::kCrLf ? n->piece.line_feeds : 0;
  n->sum_cr = n->piece.eol == Eol::kCr ? n->piece.line_feeds : 0;
  if (n->left != nullptr) {
    n->sum_length += n->left->sum_length;
    n->sum_line_feeds += n->left->sum_line_feeds;
    n->sum_crlf += n->left->sum_crlf;
    n->sum_cr += n->left->sum_cr;
  }
  if (n->right != nullptr) {
    n->sum_length += n->right->sum_length;
    n->sum_line_feeds += n->right->sum_line_feeds;
    n->sum_crlf += n->right->sum_crlf;
    n->sum_cr += n->right->sum_cr;
  }
}

void PieceTree::destroy(Node* n) {
  std::vector<Node*> stack;
  if (n != nullptr) stack.push_back(n);
  while (!stack.empty()) {
    Node* x = stack.back();
    stack.pop_back();
    if (x->left != nullptr) stack.push_back(x->left);
    if (x->right != nullptr) stack.push_back(x->right);
    delete x;
  }
}

PieceTree::Node* PieceTree::new_node(const Piece& piece) {
  // xorshift32 for treap priorities.
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 17;
  seed_ ^= seed_ << 5;
  Node* n = new Node;
  n->piece = piece;
  n->priority = seed_;
  update(n);
  return n;
}

size_t PieceTree::count_line_feeds(uint32_t buffer, size_t start, size_t end) const {
  const std::vector<uint32_t>& lf = buffers_[buffer].line_feeds;
  const auto lo = std::lower_bound(lf.begin(), lf.end(), static_cast<uint32_t>(start));
  const auto hi = std::lower_bound(lo, lf.end(), static_cast<uint32_t>(end));
  return static_cast<size_t>(hi - lo);
}

void PieceTree::split(Node* t, size_t offset, Node*& left, Node*& right) {
  if (t == nullptr) {
    left = right = nullptr;
    return;
  }
  const size_t left_len = t->left != nullptr ? t->left->sum_length : 0;
  if (offset <= left_len) {
    split(t->left, offset, left, t->left);
    update(t);
    right = t;
  } else if (offset >= left_len + t->piece.length) {
    split(t->right, offset - left_len - t->piece.length, t->right, right);
    update(t);
    left = t;
  } else {
    const size_t k = offset - left_len;
    Piece head = t->piece;
    Piece tail = t->piece;
    head.length = k;
    head.line_feeds = count_line_feeds(head.buffer, head.start, head.start + k);
    tail.start += k;
    tail.length -= k;
    tail.line_feeds = t->piece.line_feeds - head.line_feeds;
    Node* rest = t->right;
    t->right = nullptr;
    t->piece = head;
    update(t);
    left = t;
    right = merge(new_node(tail), rest);
  }
}

PieceTree::Node* PieceTree::merge(Node* a, Node* b) {
  if (a == nullptr) return b;
  if (b == nullptr) return a;
  if (a->priority > b->priority) {
    a->right = merge(a->right, b);
    update(a);
    return a;
  }
  b->left = merge(a, b->left);
  update(b);
  return b;
}

bool PieceTree::extend_rightmost(Node* n, const Piece& p) {
  if (n == nullptr) return false;
  if (n->right != nullptr) {
    if (!extend_rightmost(n->right, p)) return false;
  } else {
    if (n->piece.buffer != p.buffer || n->piece.start + n->piece.length != p.start || n->piece.eol != p.eol) {
      return false;
    }
    n->piece.length += p.length;
    n->piece.line_feeds += p.line_feeds;
  }
  update(n);
  return true;
}

PieceList PieceTree::insert(size_t offset, std::wstring_view text, Eol eol) {
  if (text.empty()) return {};
  TextBuffer& add = buffers_[1];
  const size_t start = add.text.size();
  add.text.append(text);
  size_t lf = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == L'\n') {
      add.line_feeds.push_back(static_cast<uint32_t>(start + i));
      ++lf;
    }
  }
  Piece p;
  p.buffer = 1;
  p.start = start;
  p.length = text.size();
  p.line_feeds = lf;
  p.eol = eol;

  Node* left = nullptr;
  Node* right = nullptr;
  split(root_, offset, left, right);
  if (!extend_rightmost(left, p)) left = merge(left, new_node(p));
  root_ = merge(left, right);
  ++revision_;
  return PieceList{p};
}

void PieceTree::insert_pieces(size_t offset, const PieceList& pieces) {
  Node* left = nullptr;
  Node* right = nullptr;
  split(root_, offset, left, right);
  for (const Piece& p : pieces) {
    if (p.length == 0) continue;
    left = merge(left, new_node(p));
  }
  root_ = merge(left, right);
  ++revision_;
}

PieceList PieceTree::erase(size_t offset, size_t count) {
  PieceList removed;
  if (count == 0) return removed;
  ++revision_;
  Node* left = nullptr;
  Node* mid = nullptr;
  Node* right = nullptr;
  split(root_, offset, left, mid);
  split(mid, count, mid, right);
  // In-order walk of the removed subtree.
  std::vector<Node*> stack;
  Node* n = mid;
  while (n != nullptr || !stack.empty()) {
    while (n != nullptr) {
      stack.push_back(n);
      n = n->left;
    }
    n = stack.back();
    stack.pop_back();
    removed.push_back(n->piece);
    n = n->right;
  }
  destroy(mid);
  root_ = merge(left, right);
  return removed;
}

wchar_t PieceTree::char_at(size_t offset) const {
  const Node* n = root_;
  size_t off = offset;
  while (n != nullptr) {
    const size_t ll = n->left != nullptr ? n->left->sum_length : 0;
    if (off < ll) {
      n = n->left;
      continue;
    }
    off -= ll;
    if (off < n->piece.length) return buffers_[n->piece.buffer].text[n->piece.start + off];
    off -= n->piece.length;
    n = n->right;
  }
  return 0;
}

std::wstring PieceTree::text() const {
  return substr(0, length());
}

std::wstring PieceTree::substr(size_t offset, size_t count) const {
  std::wstring out;
  if (offset >= length()) return out;
  count = std::min(count, length() - offset);
  out.reserve(count);
  for_each_chunk(offset, count, [&out](std::wstring_view chunk) {
    out.append(chunk);
    return true;
  });
  return out;
}

void PieceTree::visit_chunks(size_t offset, size_t count, ChunkFn fn, void* ctx) const {
  if (count == 0 || offset >= length()) return;
  const size_t hi = std::min(length(), offset + count);
  visit(root_, 0, offset, hi, fn, ctx);
}

bool PieceTree::visit(const Node* n, size_t base, size_t lo, size_t hi, ChunkFn fn, void* ctx) const {
  if (n == nullptr) return true;
  const size_t ll = n->left != nullptr ? n->left->sum_length : 0;
  const size_t piece_begin = base + ll;
  const size_t piece_end = piece_begin + n->piece.length;
  if (lo < piece_begin) {
    if (!visit(n->left, base, lo, hi, fn, ctx)) return false;
  }
  const size_t a = std::max(lo, piece_begin);
  const size_t b = std::min(hi, piece_end);
  if (a < b) {
    const std::wstring& buf = buffers_[n->piece.buffer].text;
    if (!fn(ctx, std::wstring_view(buf.data() + n->piece.start + (a - piece_begin), b - a), n->piece.eol)) {
      return false;
    }
  }
  if (hi > piece_end) {
    if (!visit(n->right, piece_end, lo, hi, fn, ctx)) return false;
  }
  return true;
}

size_t PieceTree::line_of(size_t offset) const {
  size_t line = 0;
  size_t off = offset;
  const Node* n = root_;
  while (n != nullptr) {
    const size_t ll = n->left != nullptr ? n->left->sum_length : 0;
    if (off < ll) {
      n = n->left;
      continue;
    }
    if (n->left != nullptr) line += n->left->sum_line_feeds;
    off -= ll;
    if (off < n->piece.length) {
      return line + count_line_feeds(n->piece.buffer, n->piece.start, n->piece.start + off);
    }
    line += n->piece.line_feeds;
    off -= n->piece.length;
    n = n->right;
  }
  return line;
}

size_t PieceTree::line_start(size_t line) const {
  if (line == 0) return 0;
  if (line > line_feed_count()) return length();
  // Find the line-th '\n' (1-based) and return the offset after it.
  size_t k = line;
  size_t base = 0;
  const Node* n = root_;
  while (n != nullptr) {
    const size_t left_lf = n->left != nullptr ? n->left->sum_line_feeds : 0;
    const size_t ll = n->left != nullptr ? n->left->sum_length : 0;
    if (k <= left_lf) {
      n = n->left;
      continue;
    }
    k -= left_lf;
    base += ll;
    if (k <= n->piece.line_feeds) {
      const std::vector<uint32_t>& lf = buffers_[n->piece.buffer].line_feeds;
      const auto first = std::lower_bound(lf.begin(), lf.end(), static_cast<uint32_t>(n->piece.start));
      const size_t pos = *(first + static_cast<std::ptrdiff_t>(k - 1));
      return base + (pos - n->piece.start) + 1;
    }
    k -= n->piece.line_feeds;
    base += n->piece.length;
    n = n->right;
  }
  return length();
}

size_t PieceTree::line_end(size_t line) const {
  if (line + 1 < line_count()) return line_start(line + 1) - 1;
  return length();
}

size_t PieceTree::offset_of(size_t line, size_t column) const {
  const size_t start = line_start(line);
  const size_t end = line_end(line);
  return std::min(start + column, end);
}

}  // namespace geditor
