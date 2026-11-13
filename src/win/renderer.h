// Direct2D drawing of the editor (docs/PLAN.md 4.6 / 4.7): the text area on
// a cell grid, the mode line and the echo area / minibuffer line (several
// lines while settings errors are shown). No line numbers, wrapping or menu
// bar yet; long lines scroll horizontally.
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include "editor.h"
#include "text_measurer.h"

namespace geditor::win {

// View state kept by the window between frames.
struct ViewState {
  int left_column = 0;             // first visible cell column of the text area
  bool show_pending_keys = false;  // echo the prefix keys (after 1 second)
  // Caret of the last frame in DIPs (for the IME windows).
  float caret_x = 0;
  float caret_y = 0;
  float caret_height = 0;
};

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Factories and the primary font; no render target yet.
  bool init(const wchar_t* font_family, float font_size_pt);
  void release();

  // Draws one frame. Returns the EndDraw result (D2DERR_RECREATE_TARGET means
  // the caller must discard the target and paint again).
  HRESULT paint(HWND hwnd, const Editor& editor, ViewState& view);
  // Draws the current frame into `dc` (WM_PRINTCLIENT, used by PrintWindow
  // for screenshots) without changing the view.
  HRESULT paint_to_dc(HWND hwnd, HDC dc, const Editor& editor, const ViewState& view);
  void resize(UINT width, UINT height);
  void set_dpi(float dpi);
  void discard_target();

  // Full text rows that fit in a client area `height_dip` high with an echo
  // area of `echo_rows` lines.
  int text_rows(float height_dip, int echo_rows) const;
  // Rows of the echo area for `editor` in a client area `height_dip` high,
  // capped so that the mode line and one text row stay visible.
  int echo_rows(float height_dip, const Editor& editor) const;
  float line_height() const { return measurer_.line_height(); }
  float em() const { return measurer_.em(); }
  const wchar_t* font_family() const { return measurer_.family(); }

 private:
  struct Cell {
    char32_t c;
    int column;  // relative to the first visible column
    int width;   // cells (0 for combining marks)
    size_t offset;
  };

  bool ensure_target(HWND hwnd);
  // BeginDraw .. EndDraw on target_ with brush_.
  HRESULT draw_frame(const Editor& editor, ViewState& view);
  void draw_text_area(const Editor& editor, ViewState& view, float width, float height);
  void draw_mode_line(const Editor& editor, float y, float width);
  // The echo area from `y` down: one line, or the lines of a multi-line
  // message (settings errors) followed by the minibuffer line when active.
  void draw_echo_area(const Editor& editor, ViewState& view, float y, float width, int rows);
  void draw_echo_line(std::wstring_view text, float y, int columns);
  // Collect the cells of columns [left, left + columns) into cells_.
  // collect_line returns true when it reached the end of the line; then
  // `end_column` is the column after its last character (relative to left).
  bool collect_line(const PieceTree& text, size_t line, int left, int columns, int tab_width,
                    int* end_column);
  int collect_string(std::wstring_view s, int left, int columns, int tab_width);
  void draw_cells(float x0, float top, D2D1_COLOR_F color);
  void flush_run(uint16_t face, float x, float baseline);
  void fill(float left, float top, float right, float bottom, D2D1_COLOR_F color);

  ID2D1Factory* d2d_factory_ = nullptr;
  IDWriteFactory* dwrite_factory_ = nullptr;
  ID2D1HwndRenderTarget* hwnd_target_ = nullptr;
  ID2D1SolidColorBrush* hwnd_brush_ = nullptr;
  // The target and brush of the frame being drawn (not owned).
  ID2D1RenderTarget* target_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  TextMeasurer measurer_;
  std::vector<Cell> cells_;
  std::vector<UINT16> run_glyphs_;
  std::vector<FLOAT> run_advances_;
};

}  // namespace geditor::win
