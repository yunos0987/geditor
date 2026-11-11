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

  // Return false when there is nothing to undo/redo. `point` receives the
  // cursor position to restore.
  bool undo(PieceTree& tree, EditListener* listener, size_t* point);
  bool redo(PieceTree& tree, EditListener* listener, size_t* point);

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
