// Direct2D drawing of the editor (docs/PLAN.md 4.6 / 4.7): the line-number
// gutter and the text area on a cell grid, the mode line and the echo area /
// minibuffer line (several lines while settings errors are shown). The rows,
// their wrap positions, their grapheme clusters and widths and their
// whitespace / end-of-line marks come from the core (Editor::display_row,
// grapheme.h); this file only draws them, one cluster in its cells. Rows wider than the
// window (wrap "none", or "fixed" wider than the window) scroll horizontally.
// Glyphs with a colour representation in their font (emoji) are drawn in
// their colours (M3d decision D1), squeezed into their cells like the others.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include "editor.h"
#include "text_measurer.h"

// Newer than the project's _WIN32_WINNT (d2d1_3.h for later NTDDI
// versions), asked for at run time; renderer.cpp raises NTDDI_VERSION.
struct ID2D1DeviceContext4;
struct ID2D1DeviceContext7;

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
  bool init(const FontSpec& font);
  // Replaces the font. Not installed family names are added to `missing`.
  // On failure the previous font stays and false is returned.
  bool set_font(const FontSpec& font, std::vector<std::wstring>* missing);
  const FontSpec& font_spec() const { return font_; }
  // The family that draws `c` (diagnostics).
  std::wstring family_of(char32_t c) { return measurer_.family_of(c); }
  void release();

  // Draws one frame. Returns the EndDraw result (D2DERR_RECREATE_TARGET means
  // the caller must discard the target and paint again).
  HRESULT paint(HWND hwnd, const Editor& editor, ViewState& view);
  // Draws the current frame into `dc` (WM_PRINTCLIENT, used by PrintWindow
  // for screenshots) without changing the view.
  HRESULT paint_to_dc(HWND hwnd, HDC dc, const Editor& editor, const ViewState& view);
  void resize(UINT width, UINT height);
  // The DPI of the window (set before the first paint and on WM_DPICHANGED).
  void set_dpi(float dpi);
  float dpi() const { return dpi_; }
  void discard_target();
  // Checks of tools/app_check.py: the next render target / font creation
  // fails as if Direct2D / DirectWrite had refused it.
  void fail_next_target() { fail_next_target_ = true; }
  void fail_next_font() { fail_next_font_ = true; }
  // The next shaping of a cluster fails (TextMeasurer::fail_next_shaping).
  void fail_next_shaping() { measurer_.fail_next_shaping(); }
  // The next colour glyph cannot be translated (TextMeasurer::fail_next_color):
  // from then on glyphs are drawn in one colour (decision D3).
  void fail_next_color() { measurer_.fail_next_color(); }
  // True once, after the first frame in which colour glyphs failed (decision
  // D3: the window says so once in the echo area).
  bool take_color_failure();
  // What the render target of the last colour glyph offered (state for the
  // tools): "none" before any colour glyph, else e.g. "colr,svg,bitmap,paint".
  std::wstring color_support() const;

  // Full text rows that fit in a client area `height_dip` high with an echo
  // area of `echo_rows` lines.
  int text_rows(float height_dip, int echo_rows) const;
  // Whole cells across a client area `width_dip` wide (line numbers
  // included).
  int text_columns(float width_dip) const;
  // Rows of the echo area for `editor` in a client area `height_dip` high,
  // capped so that the mode line and one text row stay visible.
  int echo_rows(float height_dip, const Editor& editor) const;
  float line_height() const { return measurer_.line_height(); }
  float cell_width() const { return measurer_.cell_width(); }
  float em() const { return measurer_.em(); }

  // Where a point of the client area (DIPs) is in the frame that paint
  // draws (decision B5): the area, the screen row counted from the top row
  // (negative above the client area) and the cell column of the text area
  // (the horizontal scroll included; -1 in the line-number gutter). Rows and
  // columns are cells of the grid only; the core maps them to offsets
  // through WrapLayout (Editor::offset_at_screen).
  struct Hit {
    enum class Area { kText, kGutter, kModeLine, kEchoArea };
    Area area = Area::kText;
    long long row = 0;
    int column = 0;
  };
  Hit hit_test(float x_dip, float y_dip, float width_dip, float height_dip, const Editor& editor,
               const ViewState& view) const;
  // The left edge of the text area (right of the gutter) and the top of the
  // mode line, in DIPs.
  float text_left(const Editor& editor) const;
  float mode_line_top(float height_dip, const Editor& editor) const;
  const wchar_t* font_family() const { return measurer_.family(); }

 private:
  struct Cell {
    char32_t c;  // the first code point of the cluster
    int column;  // relative to the first visible column
    int width;   // cells (0 for zero-width clusters)
    size_t offset;
    // A cluster of several code points: its text in cluster_text_ (length 0
    // for one code point, which is drawn from `c`).
    size_t text_begin = 0;
    size_t text_length = 0;
    bool emoji = false;  // shown as an emoji (from the core)
  };

  bool ensure_target(HWND hwnd);
  // BeginDraw .. EndDraw on target_ with brush_.
  HRESULT draw_frame(const Editor& editor, ViewState& view);
  void draw_text_area(const Editor& editor, ViewState& view, float width, float height);
  // Fills the cells of row_ whose clusters start in [start, end) (and its
  // line end as one cell when it is inside): the region, and the search
  // matches (M5; a match never holds a line end).
  void draw_range(size_t start, size_t end, float x0, float y, D2D1_COLOR_F color);
  void draw_mode_line(const Editor& editor, float y, float width);
  // The echo area from `y` down: one line, or the lines of a multi-line
  // message (settings errors) followed by the minibuffer line when active.
  void draw_echo_area(const Editor& editor, ViewState& view, float y, float width, int rows);
  // One line of the echo area from `x` DIPs right of the margin.
  void draw_echo_line(std::wstring_view text, float y, int columns, D2D1_COLOR_F color, float x = 0);
  // Collect the clusters of columns [left, left + columns) of `s` into
  // cells_ (the core's segmentation and widths).
  int collect_string(std::wstring_view s, int left, int columns, int tab_width);
  // Appends a cell; a cluster of several code points keeps its text.
  void add_cell(char32_t c, int column, int width, size_t offset, int code_points, bool emoji,
                std::wstring_view text);
  // Appends the cell of a buffer cluster; a cluster of several code points
  // or an emoji keeps its text (copied from the buffer, no allocation once
  // cluster_text_ has grown).
  void add_buffer_cell(const PieceTree& text, const DisplayCell& c);
  void draw_cells(float x0, float top, D2D1_COLOR_F color);
  // Draws a cluster of several code points (or an emoji) into its cells at x
  // (squeezed when wider), or over the base at `base_x` when it has no cells.
  void draw_cluster(const Cell& cell, float x, float baseline, D2D1_COLOR_F color);
  void flush_run(uint16_t face, float x, float baseline);
  void fill(float left, float top, float right, float bottom, D2D1_COLOR_F color);

  // ---- Colour glyphs (M3d decisions D1-D4) ----
  // The interfaces of a render target that draw colour glyphs, asked for
  // on its first colour glyph.
  struct ColorTarget {
    ID2D1DeviceContext4* dc4 = nullptr;  // SVG and bitmap glyphs
    ID2D1DeviceContext7* dc7 = nullptr;  // the COLR paint tree
    bool queried = false;
  };
  void release_color_target(ColorTarget& target);
  // On the first colour glyph of a frame: the interfaces of the target and
  // the formats that the measurer may translate into.
  void prepare_color();
  // The colour layers of a glyph / cluster, nullptr when it has none.
  const TextMeasurer::ColorRun* color_glyph(uint16_t face, uint16_t index);
  const TextMeasurer::ColorRun* color_cluster(const TextMeasurer::ClusterGlyphs& g);
  // Draws the layers of `run` (glyphs of face `face`) at `origin`; layers
  // without a colour of their own get `color`.
  void draw_color_run(const TextMeasurer::ColorRun& run, uint16_t face, D2D1_POINT_2F origin, D2D1_COLOR_F color);

  ID2D1Factory* d2d_factory_ = nullptr;
  IDWriteFactory* dwrite_factory_ = nullptr;
  ID2D1HwndRenderTarget* hwnd_target_ = nullptr;
  ID2D1SolidColorBrush* hwnd_brush_ = nullptr;
  // The target and brush of the frame being drawn (not owned).
  ID2D1RenderTarget* target_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  TextMeasurer measurer_;
  FontSpec font_;
  float dpi_ = 96;
  bool fail_next_target_ = false;
  bool fail_next_font_ = false;
  DisplayRow row_;
  std::vector<Cell> cells_;
  std::wstring cluster_text_;  // texts of the multi-code-point clusters of cells_
  std::vector<UINT16> run_glyphs_;
  std::vector<FLOAT> run_advances_;
  ColorTarget hwnd_color_;           // of hwnd_target_ (released with it)
  ColorTarget* color_ = nullptr;     // of the frame being drawn (not owned)
  bool color_ready_ = false;         // prepare_color ran in this frame
  uint32_t color_formats_shown_ = 0; // formats of the last prepare_color (state)
  bool color_failure_reported_ = false;
  bool color_failure_pending_ = false;
};

}  // namespace geditor::win
