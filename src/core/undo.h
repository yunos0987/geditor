// Linear undo/redo. One command is one group; consecutive self-inserts are
// merged into one group up to 20 characters. Groups keep pieces (references
// into the piece tree's buffers), never copies of the text.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "piece_tree.h"

namespace geditor {

// Notified for each change that undo/redo applies, so that marks can follow.
class EditListener {
 public:
  virtual void on_insert(size_t offset, size_t count) = 0;
  virtual void on_erase(size_t offset, size_t count) = 0;

 protected:
  ~EditListener() = default;
};

class UndoHistory {
 public:
  static constexpr int kMaxTypingGroup = 20;

  // Bracket every command. `typing` marks a self-insert command.
  void begin_command(size_t point, bool typing);
  void end_command(size_t point);

  void record_insert(size_t offset, const PieceList& pieces);
  void record_erase(size_t offset, PieceList pieces);
  // set-eol (decision F7): the whole text went from the pieces `before` to
  // `after` (same text, other line ends; both empty when no line end
  // changed) and the buffer's default line end from `eol_before` to
  // `eol_after`.
  void record_eol_change(PieceList before, PieceList after, Eol eol_before, Eol eol_after);

  // Return false when there is nothing to undo/redo. `point` receives the
  // cursor position to restore; `default_eol` (when not null) the default
  // line end that an undone or redone set-eol restores.
  bool undo(PieceTree& tree, EditListener* listener, size_t* point, Eol* default_eol = nullptr);
  bool redo(PieceTree& tree, EditListener* listener, size_t* point, Eol* default_eol = nullptr);

  // query-replace (M5, one undo unit for the whole replacement): the groups
  // from index `first` (an undo_count() taken before them) to the newest
  // become one group, whose cursor before it is `point_before`. Nothing
  // happens when there are none.
  void merge_groups_since(size_t first, size_t point_before);

  // The current state becomes the saved state.
  void mark_saved();
  bool modified() const;

  // Drops all history; the current state becomes the saved state.
  void clear();

  size_t undo_count() const { return undo_.size(); }
  size_t redo_count() const { return redo_.size(); }

 private:
  struct Op {
    bool insert = false;
    size_t offset = 0;
    PieceList pieces;
    // set-eol: `pieces` before and `pieces_after` after (whole text).
    bool eol_change = false;
    PieceList pieces_after;
    Eol eol_before = Eol::kLf;
    Eol eol_after = Eol::kLf;
  };
  struct Group {
    uint64_t id = 0;
    std::vector<Op> ops;
    size_t point_before = 0;
    size_t point_after = 0;
    int typed = 0;
  };
  static constexpr uint64_t kUnreachable = ~uint64_t{0};

  uint64_t top_id() const { return undo_.empty() ? 0 : undo_.back().id; }
  void open_group();

  std::vector<Group> undo_;
  std::vector<Group> redo_;
  uint64_t next_id_ = 1;
  uint64_t saved_id_ = 0;
  bool in_command_ = false;
  bool open_ = false;
  bool typing_ = false;
  bool last_was_typing_ = false;
  size_t command_point_ = 0;
};

}  // namespace geditor
