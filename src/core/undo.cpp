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
        tail.start + tail.length == p.start && tail.eol == p.eol) {
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

void UndoHistory::record_eol_change(PieceList before, PieceList after, Eol eol_before, Eol eol_after) {
  if (!open_ || !in_command_) open_group();
  Op op;
  op.eol_change = true;
  op.pieces = std::move(before);
  op.pieces_after = std::move(after);
  op.eol_before = eol_before;
  op.eol_after = eol_after;
  undo_.back().ops.push_back(std::move(op));
  if (!in_command_) open_ = false;
}

bool UndoHistory::undo(PieceTree& tree, EditListener* listener, size_t* point, Eol* default_eol) {
  if (undo_.empty()) return false;
  Group g = std::move(undo_.back());
  undo_.pop_back();
  for (auto it = g.ops.rbegin(); it != g.ops.rend(); ++it) {
    if (it->eol_change) {
      // The text is the same: no listener call (marks and wrap rows stay).
      if (!it->pieces.empty() || !it->pieces_after.empty()) tree.assign_pieces(it->pieces);
      if (default_eol != nullptr) *default_eol = it->eol_before;
      continue;
    }
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

bool UndoHistory::redo(PieceTree& tree, EditListener* listener, size_t* point, Eol* default_eol) {
  if (redo_.empty()) return false;
  Group g = std::move(redo_.back());
  redo_.pop_back();
  for (const Op& op : g.ops) {
    if (op.eol_change) {
      if (!op.pieces.empty() || !op.pieces_after.empty()) tree.assign_pieces(op.pieces_after);
      if (default_eol != nullptr) *default_eol = op.eol_after;
      continue;
    }
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

void UndoHistory::merge_groups_since(size_t first, size_t point_before) {
  if (first >= undo_.size()) return;
  Group& merged = undo_[first];
  for (size_t i = first + 1; i < undo_.size(); ++i) {
    Group& g = undo_[i];
    // A saved state inside the merged groups can no longer be reached.
    if (merged.id == saved_id_) saved_id_ = kUnreachable;
    for (Op& op : g.ops) merged.ops.push_back(std::move(op));
    merged.point_after = g.point_after;
    merged.id = g.id;  // the newest id: a save after the last group stays saved
    merged.count += g.count;
  }
  merged.point_before = point_before;
  merged.typed = 0;
  undo_.resize(first + 1);
  last_was_typing_ = false;
}

void UndoHistory::merge_groups_from_id(uint64_t first_id, size_t point_before) {
  size_t first = undo_.size();
  while (first > 0 && undo_[first - 1].id >= first_id) --first;
  merge_groups_since(first, point_before);
}

bool UndoHistory::add_count(size_t n) {
  if (!in_command_ || !open_ || undo_.empty()) return false;
  undo_.back().count += n;
  return true;
}

size_t UndoHistory::count_from_id(uint64_t first_id) const {
  size_t total = 0;
  for (size_t i = undo_.size(); i > 0 && undo_[i - 1].id >= first_id; --i) total += undo_[i - 1].count;
  return total;
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
