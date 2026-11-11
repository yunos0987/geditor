#include "undo.h"

#include <utility>

namespace geditor {

void UndoHistory::begin_command(size_t point, bool typing) {
  in_command_ = true;
  open_ = false;
  typing_ = typing;
  command_point_ = point;
}

void UndoHistory::end_command(size_t point) {
  if (!in_command_) return;
  if (open_ && !undo_.empty()) undo_.back().point_after = point;
  last_was_typing_ = typing_ && open_;
  in_command_ = false;
  open_ = false;
}

void UndoHistory::open_group() {
  if (!redo_.empty()) {
    for (const Group& g : redo_) {
      if (g.id == saved_id_) saved_id_ = kUnreachable;
    }
    redo_.clear();
  }
  const bool can_merge = typing_ && last_was_typing_ && !undo_.empty() &&
                         undo_.back().typed > 0 && undo_.back().typed < kMaxTypingGroup &&
                         undo_.back().id != saved_id_;
  if (can_merge) {
    ++undo_.back().typed;
  } else {
    Group g;
    g.id = next_id_++;
    g.point_before = command_point_;
    g.point_after = command_point_;
    g.typed = typing_ ? 1 : 0;
    undo_.push_back(std::move(g));
  }
  open_ = true;
  if (!in_command_) {
    // An edit outside a command forms a group of its own.
    last_was_typing_ = false;
  }
}

void UndoHistory::record_insert(size_t offset, const PieceList& pieces) {
  if (pieces.empty()) return;
  if (!open_ || !in_command_) open_group();
  std::vector<Op>& ops = undo_.back().ops;
  // Typing appends to the add buffer: extend the previous insert when the new
  // text directly follows it both in the document and in the buffer.
  if (!ops.empty() && ops.back().insert && pieces.size() == 1) {
    Op& last = ops.back();
    Piece& tail = last.pieces.back();
    const Piece& p = pieces.front();
    if (last.offset + total_length(last.pieces) == offset && tail.buffer == p.buffer &&
        tail.start + tail.length == p.start) {
      tail.length += p.length;
      tail.line_feeds += p.line_feeds;
      if (!in_command_) open_ = false;
      return;
    }
  }
  Op op;
  op.insert = true;
  op.offset = offset;
  op.pieces = pieces;
  ops.push_back(std::move(op));
  if (!in_command_) open_ = false;
}

void UndoHistory::record_erase(size_t offset, PieceList pieces) {
  if (pieces.empty()) return;
  if (!open_ || !in_command_) open_group();
  Op op;
  op.insert = false;
  op.offset = offset;
  op.pieces = std::move(pieces);
  undo_.back().ops.push_back(std::move(op));
  if (!in_command_) open_ = false;
}

bool UndoHistory::undo(PieceTree& tree, EditListener* listener, size_t* point) {
  if (undo_.empty()) return false;
  Group g = std::move(undo_.back());
  undo_.pop_back();
  for (auto it = g.ops.rbegin(); it != g.ops.rend(); ++it) {
    const size_t n = total_length(it->pieces);
    if (it->insert) {
      tree.erase(it->offset, n);
      if (listener != nullptr) listener->on_erase(it->offset, n);
    } else {
      tree.insert_pieces(it->offset, it->pieces);
      if (listener != nullptr) listener->on_insert(it->offset, n);
    }
  }
  *point = g.point_before;
  redo_.push_back(std::move(g));
  open_ = false;
  last_was_typing_ = false;
  return true;
}

bool UndoHistory::redo(PieceTree& tree, EditListener* listener, size_t* point) {
  if (redo_.empty()) return false;
  Group g = std::move(redo_.back());
  redo_.pop_back();
  for (const Op& op : g.ops) {
    const size_t n = total_length(op.pieces);
    if (op.insert) {
      tree.insert_pieces(op.offset, op.pieces);
      if (listener != nullptr) listener->on_insert(op.offset, n);
    } else {
      tree.erase(op.offset, n);
      if (listener != nullptr) listener->on_erase(op.offset, n);
    }
  }
  *point = g.point_after;
  undo_.push_back(std::move(g));
  open_ = false;
  last_was_typing_ = false;
  return true;
}

void UndoHistory::mark_saved() {
  saved_id_ = top_id();
  last_was_typing_ = false;
}

bool UndoHistory::modified() const {
  return top_id() != saved_id_;
}

void UndoHistory::clear() {
  undo_.clear();
  redo_.clear();
  saved_id_ = 0;
  in_command_ = false;
  open_ = false;
  typing_ = false;
  last_was_typing_ = false;
}

}  // namespace geditor
