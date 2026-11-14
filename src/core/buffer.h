// A text buffer: text, cursor, mark, undo history and file information.
#pragma once

#include <cstddef>
#include <string>

#include "encoding.h"
#include "piece_tree.h"
#include "undo.h"

namespace geditor {

struct Buffer {
  PieceTree text;
  UndoHistory undo;
  size_t point = 0;
  bool has_mark = false;
  size_t mark = 0;
  bool mark_active = false;  // transient mark: the region is highlighted
  bool read_only = false;
  std::wstring path;         // empty for an untitled buffer
  FileFormat format;         // new buffers: UTF-8 without BOM, LF
  bool edited_in_command = false;
  // Editor::insert in the running command: point right after the inserted
  // text (decision G4: point then snaps forward to the end of the cluster
  // that the insertion joined).
  bool inserted_in_command = false;
  size_t insert_end = 0;

  bool modified() const { return undo.modified(); }

  // Empties the buffer and its history (path and format are reset too).
  void reset() {
    text = PieceTree();
    undo.clear();
    point = 0;
    has_mark = false;
    mark = 0;
    mark_active = false;
    read_only = false;
    path.clear();
    format = FileFormat();
    edited_in_command = false;
    inserted_in_command = false;
    insert_end = 0;
  }
};

}  // namespace geditor
