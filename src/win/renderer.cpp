// ID2D1DeviceContext4 / ID2D1DeviceContext7 (colour glyphs, M3d) are
// declared for NTDDI versions newer than the project's _WIN32_WINNT; they are
// asked for at run time (QueryInterface), so this raises NTDDI_VERSION for
// this file only, before any header.
#define NTDDI_VERSION 0x0A00000D  // NTDDI_WIN10_CU

#include "renderer.h"

#include <d2d1_3.h>

#include <algorithm>
#include <cmath>

#include "char_width.h"
#include "echo_text.h"
#include "grapheme.h"
#include "measure.h"
#include "unicode.h"

namespace geditor::win {
namespace {

constexpr float kMarginDip = 4.0f;  // left of the gutter, mode line and echo area
constexpr float kCursorWidthDip = 2.0f;

D2D1_COLOR_F rgb(UINT32 value) {
  return D2D1::ColorF(value);
}

D2D1_COLOR_F color_of(const Editor& editor, ColorIndex index) {
  return rgb(editor.config().colors[static_cast<size_t>(index)]);
}

template <typename T>
void safe_release(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

// Cells of a cluster starting at `column` (TAB to the next stop).
int advance_of(const Cluster& c, int column, int tab_width) {
  if (c.first == U'\t') return cell_advance(U'\t', column, tab_width);
  return c.width;
}

// Characters that are never drawn: controls, the space (blank anyway) and
// invisible format characters (zero width space, joiners, marks, variation
// selectors, BOM), which would otherwise show a missing-glyph box.
bool is_blank(char32_t c) {
  return c <= 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0) || is_invisible_format_char(c);
}

// The longest cluster text that is shaped (decision G6): the code points
// after it in a longer cluster (only runs of hundreds of combining marks are
// that long) are not drawn.
constexpr size_t kMaxShapedClusterUnits = 64;

}  // namespace

Renderer::~Renderer() {
  release();
}

bool Renderer::init(const FontSpec& font) {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_))) return false;
  mark_stage("d2d_factory");
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&dwrite_factory_)))) {
    return false;
  }
  mark_stage("dwrite_factory");
  font_ = font;
  const bool ok = measurer_.init(dwrite_factory_, font, nullptr);
  mark_stage("font");
  return ok;
}

bool Renderer::set_font(const FontSpec& font, std::vector<std::wstring>* missing) {
  if (dwrite_factory_ == nullptr) return false;
  const bool injected_failure = fail_next_font_;
  fail_next_font_ = false;
  if (!injected_failure && measurer_.init(dwrite_factory_, font, missing)) {
    font_ = font;
    return true;
  }
  // Keep the previous font (it was built before, so it builds again).
  measurer_.init(dwrite_factory_, font_, nullptr);
  return false;
}

int Renderer::text_columns(float width_dip) const {
  const float cw = measurer_.cell_width();
  if (cw <= 0) return 1;
  return std::max(1, static_cast<int>(std::floor((width_dip - kMarginDip) / cw)));
}

void Renderer::release() {
  discard_target();
  measurer_.release();
  safe_release(dwrite_factory_);
  safe_release(d2d_factory_);
}

int Renderer::text_rows(float height_dip, int echo_rows) const {
  const float lh = measurer_.line_height();
  if (lh <= 0) return 1;
  const float text_height = height_dip - static_cast<float>(1 + std::max(1, echo_rows)) * lh;
  return std::max(1, static_cast<int>(std::floor(text_height / lh)));
}

int Renderer::echo_rows(float height_dip, const Editor& editor) const {
  const float lh = measurer_.line_height();
  const int total = lh > 0 ? static_cast<int>(std::floor(height_dip / lh)) : 3;
  return editor.echo_line_count(max_echo_rows(total));
}

float Renderer::text_left(const Editor& editor) const {
  return kMarginDip + static_cast<float>(editor.gutter_columns()) * measurer_.cell_width();
}

float Renderer::mode_line_top(float height_dip, const Editor& editor) const {
  const float lh = measurer_.line_height();
  return height_dip - static_cast<float>(echo_rows(height_dip, editor) + 1) * lh;
}

Renderer::Hit Renderer::hit_test(float x_dip, float y_dip, float width_dip, float height_dip,
                                 const Editor& editor, const ViewState& view) const {
  (void)width_dip;
  Hit hit;
  const float lh = measurer_.line_height();
  const float cw = measurer_.cell_width();
  const float mode_y = mode_line_top(height_dip, editor);
  if (y_dip >= mode_y + lh) {
    hit.area = Hit::Area::kEchoArea;
  } else if (y_dip >= mode_y) {
    hit.area = Hit::Area::kModeLine;
  }
  hit.row = lh > 0 ? static_cast<long long>(std::floor(y_dip / lh)) : 0;
  const float x0 = text_left(editor);
  if (x_dip < x0 || cw <= 0) {
    if (hit.area == Hit::Area::kText) hit.area = Hit::Area::kGutter;
    hit.column = -1;
  } else {
    hit.column = static_cast<int>(std::floor((x_dip - x0) / cw)) + view.left_column;
  }
  return hit;
}

bool Renderer::ensure_target(HWND hwnd) {
  if (hwnd_target_ != nullptr) return true;
  if (d2d_factory_ == nullptr) return false;
  if (fail_next_target_) {
    fail_next_target_ = false;
    return false;
  }
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float dpi = dpi_;
  // Software rendering: creating a GPU render target costs about 80ms at
  // startup on the development PC (D3D device and driver load), while the
  // software target costs about 25ms. M1-5 (keystroke to frame within 16ms on
  // a 100MB file) is met with it (tools/measure_input.py).
  const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), dpi, dpi);
  const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                        static_cast<UINT32>(rc.bottom - rc.top)),
      D2D1_PRESENT_OPTIONS_IMMEDIATELY);
  if (FAILED(d2d_factory_->CreateHwndRenderTarget(props, hwnd_props, &hwnd_target_))) {
    return false;
  }
  if (FAILED(hwnd_target_->CreateSolidColorBrush(rgb(0xffffff), &hwnd_brush_))) {
    safe_release(hwnd_target_);
    return false;
  }
  return true;
}

void Renderer::discard_target() {
  release_color_target(hwnd_color_);
  safe_release(hwnd_brush_);
  safe_release(hwnd_target_);
}

void Renderer::release_color_target(ColorTarget& target) {
  safe_release(target.dc7);
  safe_release(target.dc4);
  target.queried = false;
}

void Renderer::resize(UINT width, UINT height) {
  if (hwnd_target_ != nullptr) hwnd_target_->Resize(D2D1::SizeU(width, height));
}

void Renderer::set_dpi(float dpi) {
  dpi_ = dpi;
  if (hwnd_target_ != nullptr) hwnd_target_->SetDpi(dpi, dpi);
}

HRESULT Renderer::paint(HWND hwnd, const Editor& editor, ViewState& view) {
  if (!ensure_target(hwnd)) return E_FAIL;
  mark_stage("render_target");
  target_ = hwnd_target_;
  brush_ = hwnd_brush_;
  color_ = &hwnd_color_;
  const HRESULT hr = draw_frame(editor, view);
  target_ = nullptr;
  brush_ = nullptr;
  color_ = nullptr;
  return hr;
}

HRESULT Renderer::paint_to_dc(HWND hwnd, HDC dc, const Editor& editor, const ViewState& view) {
  if (d2d_factory_ == nullptr) return E_FAIL;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float dpi = dpi_;
  const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
  ID2D1DCRenderTarget* dc_target = nullptr;
  ID2D1SolidColorBrush* dc_brush = nullptr;
  HRESULT hr = d2d_factory_->CreateDCRenderTarget(&props, &dc_target);
  if (SUCCEEDED(hr)) hr = dc_target->BindDC(dc, &rc);
  if (SUCCEEDED(hr)) hr = dc_target->CreateSolidColorBrush(rgb(0xffffff), &dc_brush);
  ColorTarget dc_color;
  if (SUCCEEDED(hr)) {
    target_ = dc_target;
    brush_ = dc_brush;
    color_ = &dc_color;
    ViewState copy = view;  // printing does not change the view
    hr = draw_frame(editor, copy);
    target_ = nullptr;
    brush_ = nullptr;
    color_ = nullptr;
  }
  release_color_target(dc_color);
  safe_release(dc_brush);
  safe_release(dc_target);
  return hr;
}

bool Renderer::take_color_failure() {
  const bool pending = color_failure_pending_;
  color_failure_pending_ = false;
  return pending;
}

std::wstring Renderer::color_support() const {
  if (color_formats_shown_ == 0) return L"none";
  std::wstring text = L"colr";
  if ((color_formats_shown_ & TextMeasurer::kFormatSvg) != 0) text += L",svg";
  if ((color_formats_shown_ & TextMeasurer::kFormatBitmaps) != 0) text += L",bitmap";
  if ((color_formats_shown_ & TextMeasurer::kFormatPaintTree) != 0) text += L",paint";
  if (measurer_.color_failed()) text += L",failed";
  return text;
}

void Renderer::prepare_color() {
  if (color_ready_ || color_ == nullptr || target_ == nullptr) return;
  color_ready_ = true;
  if (!color_->queried) {
    // Decision D4: asked for on the first colour glyph of this target only.
    color_->queried = true;
    if (FAILED(target_->QueryInterface(__uuidof(ID2D1DeviceContext4), reinterpret_cast<void**>(&color_->dc4)))) {
      color_->dc4 = nullptr;
    }
    if (FAILED(target_->QueryInterface(__uuidof(ID2D1DeviceContext7), reinterpret_cast<void**>(&color_->dc7)))) {
      color_->dc7 = nullptr;
    }
  }
  // COLR layers are glyph runs in a colour (any target); outlines come with
  // them for the glyphs of a run that have no colour.
  uint32_t formats = TextMeasurer::kFormatTrueType | TextMeasurer::kFormatCff | TextMeasurer::kFormatColr;
  uint32_t paint_level = 0;
  if (color_->dc4 != nullptr) formats |= TextMeasurer::kFormatSvg | TextMeasurer::kFormatBitmaps;
  if (color_->dc7 != nullptr) {
    paint_level = static_cast<uint32_t>(color_->dc7->GetPaintFeatureLevel());
    if (paint_level != 0) formats |= TextMeasurer::kFormatPaintTree;
  }
  measurer_.set_color_formats(formats, paint_level);
  color_formats_shown_ = formats;
}

const TextMeasurer::ColorRun* Renderer::color_glyph(uint16_t face, uint16_t index) {
  if (measurer_.color_failed() || !measurer_.is_color_face(face)) return nullptr;
  prepare_color();
  return measurer_.color_glyph(face, index);
}

const TextMeasurer::ColorRun* Renderer::color_cluster(const TextMeasurer::ClusterGlyphs& g) {
  if (measurer_.color_failed() || !measurer_.is_color_face(g.face)) return nullptr;
  prepare_color();
  return measurer_.color_cluster(g);
}

void Renderer::draw_color_run(const TextMeasurer::ColorRun& run, uint16_t face, D2D1_POINT_2F origin,
                              D2D1_COLOR_F color) {
  DWRITE_GLYPH_RUN glyphs{};
  glyphs.fontFace = measurer_.face(face);
  glyphs.fontEmSize = measurer_.face_em(face);
  for (const TextMeasurer::ColorLayer& layer : run.layers) {
    glyphs.glyphCount = layer.count;
    glyphs.glyphIndices = run.glyphs.data() + layer.first;
    glyphs.glyphAdvances = run.advances.data() + layer.first;
    glyphs.glyphOffsets = run.offsets.data() + layer.first;
    const D2D1_POINT_2F at = D2D1::Point2F(origin.x + layer.x, origin.y + layer.y);
    // The formats were chosen by prepare_color from the interfaces of this
    // target, so the interface of each layer is there.
    if ((layer.format & TextMeasurer::kFormatPaintTree) != 0 && color_->dc7 != nullptr) {
      brush_->SetColor(color);  // the paint tree's "text colour"
      color_->dc7->DrawPaintGlyphRun(at, &glyphs, brush_, 0, DWRITE_MEASURING_MODE_NATURAL);
    } else if ((layer.format & TextMeasurer::kFormatSvg) != 0 && color_->dc4 != nullptr) {
      brush_->SetColor(color);
      color_->dc4->DrawSvgGlyphRun(at, &glyphs, brush_, nullptr, 0, DWRITE_MEASURING_MODE_NATURAL);
    } else if ((layer.format & TextMeasurer::kFormatBitmaps) != 0 && color_->dc4 != nullptr) {
      color_->dc4->DrawColorBitmapGlyphRun(
          static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(layer.format & TextMeasurer::kFormatBitmaps), at, &glyphs,
          DWRITE_MEASURING_MODE_NATURAL, D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DEFAULT);
    } else {
      // A COLR layer (or an outline without colour) in its colour.
      brush_->SetColor(layer.foreground ? color
                                        : D2D1::ColorF(layer.color.r, layer.color.g, layer.color.b, layer.color.a));
      target_->DrawGlyphRun(at, &glyphs, brush_, DWRITE_MEASURING_MODE_NATURAL);
    }
  }
  brush_->SetColor(color);
}

HRESULT Renderer::draw_frame(const Editor& editor, ViewState& view) {
  color_ready_ = false;
  target_->BeginDraw();
  target_->SetTransform(D2D1::Matrix3x2F::Identity());
  target_->Clear(color_of(editor, kColorBackground));
  const D2D1_SIZE_F size = target_->GetSize();
  const float lh = measurer_.line_height();
  const int rows = echo_rows(size.height, editor);
  const float echo_y = size.height - static_cast<float>(rows) * lh;
  const float mode_y = echo_y - lh;
  draw_text_area(editor, view, size.width, std::max(0.0f, mode_y));
  draw_mode_line(editor, mode_y, size.width);
  draw_echo_area(editor, view, echo_y, size.width, rows);
  mark_stage("draw_calls");
  if (measurer_.color_failed() && !color_failure_reported_) {
    // Decision D3: reported once (the glyphs were drawn in one colour).
    color_failure_reported_ = true;
    color_failure_pending_ = true;
  }
  const HRESULT hr = target_->EndDraw();
  mark_stage("end_draw");
  return hr;
}

void Renderer::fill(float left, float top, float right, float bottom, D2D1_COLOR_F color) {
  brush_->SetColor(color);
  target_->FillRectangle(D2D1::RectF(left, top, right, bottom), brush_);
}

void Renderer::draw_text_area(const Editor& editor, ViewState& view, float width, float height) {
  const Buffer& b = editor.buffer();
  const float cw = measurer_.cell_width();
  const float lh = measurer_.line_height();
  const int gutter = editor.gutter_columns();
  const int columns = std::max(1, text_columns(width) - gutter);
  const float x0 = kMarginDip + static_cast<float>(gutter) * cw;

  // Horizontal scrolling: rows that the core wraps within the window never
  // scroll; wider rows ("none", or "fixed" wider than the window, decision
  // E3) keep the cursor column visible.
  const Editor::PointView point = editor.point_view();
  const int wrap = editor.wrap_width();
  if (wrap > 0 && wrap < columns) {
    view.left_column = 0;
  } else if (point.column < view.left_column || point.column >= view.left_column + columns) {
    view.left_column = point.column < columns ? 0 : point.column - columns / 2;
  }
  const int left = view.left_column;

  const size_t current_line = editor.point_line();
  const D2D1_COLOR_F foreground = color_of(editor, kColorForeground);
  const D2D1_COLOR_F whitespace = color_of(editor, kColorWhitespace);
  const D2D1_COLOR_F selection = color_of(editor, kColorSelection);
  const D2D1_COLOR_F search_match = color_of(editor, kColorSearchMatch);
  const D2D1_COLOR_F search_current = color_of(editor, kColorSearchCurrent);
  const PieceTree& text = b.text;
  RowPos pos = editor.top();
  if (pos.line >= text.line_count()) return;
  const int rows = static_cast<int>(std::ceil(height / lh));  // the last one may be partial
  // M5: the matches of the rows drawn (empty outside isearch / query-replace).
  const Editor::SearchHighlights highlights = editor.search_highlights(rows);
  float cursor_y = -1;
  for (int r = 0; r < rows; ++r) {
    const float y = static_cast<float>(r) * lh;
    editor.display_row(pos, left, columns + 1, &row_);

    // Line numbers (decision E2): right-aligned, on the first row of a line.
    if (gutter > 0 && row_.first_row) {
      const std::wstring number = std::to_wstring(pos.line + 1);
      const int start = gutter - 1 - static_cast<int>(number.size());
      collect_string(number, 0, gutter, 1);
      for (Cell& c : cells_) c.column += start;
      draw_cells(kMarginDip, y,
                 color_of(editor, pos.line == current_line ? kColorLineNumberCurrent : kColorLineNumber));
    }

    target_->PushAxisAlignedClip(D2D1::RectF(x0, y, width, std::min(height, y + lh)),
                                 D2D1_ANTIALIAS_MODE_ALIASED);
    // Trailing whitespace background (decision E8).
    for (const DisplayCell& c : row_.cells) {
      if (c.trailing && c.width > 0) {
        const float x = x0 + static_cast<float>(c.column) * cw;
        fill(x, y, x + static_cast<float>(c.width) * cw, y + lh, whitespace);
      }
    }
    if (b.has_mark && b.mark_active && b.mark != b.point) {
      draw_range(std::min(b.mark, b.point), std::max(b.mark, b.point), x0, y, selection);
    }
    // Search matches over the region (PLAN 4.7), the current one last. The
    // ranges come from the core (Editor::search_highlights); the renderer
    // does not search.
    if (!highlights.matches.empty() || highlights.has_current) {
      const size_t row_start = row_.cells.empty() ? row_.line_end : row_.cells.front().offset;
      for (const SearchMatch& m : highlights.matches) {
        if (m.end > row_start && m.start <= row_.line_end && !(highlights.has_current && m == highlights.current)) {
          draw_range(m.start, m.end, x0, y, search_match);
        }
      }
      if (highlights.has_current && highlights.current.end > row_start &&
          highlights.current.start <= row_.line_end) {
        draw_range(highlights.current.start, highlights.current.end, x0, y, search_current);
      }
    }
    // Characters, then whitespace marks in their own colour. A cluster of
    // several code points (or an emoji) is drawn from its text in the buffer.
    for (int pass = 0; pass < 2; ++pass) {
      cells_.clear();
      cluster_text_.clear();
      for (const DisplayCell& c : row_.cells) {
        if (c.mark != (pass == 1)) continue;
        add_buffer_cell(b.text, c);
      }
      draw_cells(x0, y, pass == 0 ? foreground : whitespace);
    }
    // End-of-line mark in the cell after the line end (PLAN 4.7).
    if (row_.complete && row_.eol.shown) {
      collect_string(row_.eol.symbol, 0, 2, 1);
      for (Cell& c : cells_) c.column += row_.end_column;
      draw_cells(x0, y, color_of(editor, row_.eol.color));
    }
    target_->PopAxisAlignedClip();

    if (pos == point.pos) cursor_y = y;
    if (!editor.next_row(&pos)) break;
  }

  if (!editor.minibuffer_active() && cursor_y >= 0) {
    // A bar on the left edge of the cell (PLAN 4.7): a mark at the line end
    // stays visible.
    const float x = x0 + static_cast<float>(point.column - left) * cw;
    if (x >= x0 && x <= width) {
      fill(x, cursor_y, x + kCursorWidthDip, cursor_y + lh, color_of(editor, kColorCursor));
      view.caret_x = x;
      view.caret_y = cursor_y;
      view.caret_height = lh;
    }
  }
}

void Renderer::draw_range(size_t region_start, size_t region_end, float x0, float y, D2D1_COLOR_F color) {
  const float cw = measurer_.cell_width();
  const float lh = measurer_.line_height();
  // Runs of cells in the range, and the line end as one cell.
  int run_start = 0;
  int run_end = 0;
  bool in_run = false;
  auto flush = [&] {
    if (in_run) {
      fill(x0 + static_cast<float>(run_start) * cw, y, x0 + static_cast<float>(run_end) * cw, y + lh, color);
    }
  };
  auto add = [&](int column, int cells) {
    if (in_run && column == run_end) {
      run_end = column + cells;
      return;
    }
    flush();
    run_start = column;
    run_end = column + cells;
    in_run = true;
  };
  for (const DisplayCell& c : row_.cells) {
    if (c.width > 0 && c.offset >= region_start && c.offset < region_end) add(c.column, c.width);
  }
  if (row_.complete && row_.last_row && row_.has_newline && row_.line_end >= region_start &&
      row_.line_end < region_end) {
    add(row_.end_column, 1);
  }
  flush();
}

void Renderer::draw_mode_line(const Editor& editor, float y, float width) {
  const float lh = measurer_.line_height();
  fill(0, y, width, y + lh, color_of(editor, kColorModeLineBackground));
  const int columns = text_columns(width);
  collect_string(editor.mode_line(), 0, columns + 1, editor.tab_width());
  draw_cells(kMarginDip, y, color_of(editor, kColorModeLineForeground));
}

void Renderer::draw_echo_area(const Editor& editor, ViewState& view, float y, float width, int rows) {
  const float cw = measurer_.cell_width();
  const float lh = measurer_.line_height();
  const int columns = text_columns(width);
  const D2D1_COLOR_F foreground = color_of(editor, kColorForeground);
  const bool multi_line = editor.message_is_multi_line();
  if (multi_line) {
    // Settings errors, one per line (as many as `rows` allows); the
    // minibuffer line (if any) below.
    const int message_rows = rows - (editor.minibuffer_active() ? 1 : 0);
    for (const std::wstring& line : editor.message_lines(message_rows)) {
      draw_echo_line(fit_message(line, columns), y, columns, foreground);
      y += lh;
    }
  }
  std::wstring text;
  int cursor_column = -1;
  if (editor.minibuffer_active()) {
    // The prompt stays visible; the input is abbreviated on the left. The
    // prompt and the input are drawn (and segmented) apart (review R3).
    const Buffer& mb = editor.minibuffer_buffer();
    const MinibufferLine line = fit_minibuffer(editor.minibuffer_prompt(), mb.text.text(), mb.point,
                                               multi_line ? std::wstring() : editor.message(), columns);
    const std::wstring_view all(line.text);
    draw_echo_line(all.substr(0, line.input_start), y, columns, foreground);
    draw_echo_line(all.substr(line.input_start), y, columns - line.input_column, foreground,
                   static_cast<float>(line.input_column) * cw);
    cursor_column = line.cursor_column;
  } else if (multi_line) {
    return;
  } else if (view.show_pending_keys && !editor.pending_keys_text().empty()) {
    text = fit_message(editor.pending_keys_text(), columns);
  } else {
    text = fit_message(editor.message(), columns);
  }
  if (!editor.minibuffer_active()) draw_echo_line(text, y, columns, foreground);
  if (cursor_column >= 0) {
    const float x = kMarginDip + static_cast<float>(cursor_column) * cw;
    fill(x, y, x + kCursorWidthDip, y + lh, color_of(editor, kColorCursor));
    view.caret_x = x;
    view.caret_y = y;
    view.caret_height = lh;
  }
}

void Renderer::draw_echo_line(std::wstring_view text, float y, int columns, D2D1_COLOR_F color, float x) {
  // The echo line counts TAB as one cell, like fit_message / fit_minibuffer.
  constexpr int kEchoTabWidth = 1;
  collect_string(text, 0, columns + 1, kEchoTabWidth);
  draw_cells(kMarginDip + x, y, color);
}

int Renderer::collect_string(std::wstring_view s, int left, int columns, int tab_width) {
  cells_.clear();
  cluster_text_.clear();
  const int right = left + columns;
  int column = 0;
  for_each_cluster(s, [&](const Cluster& c) {
    if (column >= right) return false;
    const int w = advance_of(c, column, tab_width);
    if (column + w > left || (w == 0 && column >= left)) {
      add_cell(c.first, column - left, w, c.offset, c.code_points, c.emoji,
               s.substr(c.offset, std::min(c.length, kMaxShapedClusterUnits)));
    }
    column += w;
    return true;
  });
  return column - left;
}

void Renderer::add_cell(char32_t c, int column, int width, size_t offset, int code_points, bool emoji,
                        std::wstring_view text) {
  Cell cell{c, column, width, offset};
  cell.emoji = emoji;
  // An emoji of one code point too: it is shaped with the emoji font as a
  // cluster (decision D1 of M3d).
  if ((code_points > 1 || emoji) && !text.empty()) {
    cell.text_begin = cluster_text_.size();
    cell.text_length = text.size();
    cluster_text_.append(text);
  }
  cells_.push_back(cell);
}

void Renderer::add_buffer_cell(const PieceTree& text, const DisplayCell& c) {
  Cell cell{c.c, c.column, c.width, c.offset};
  cell.emoji = c.emoji;
  if (c.code_points > 1 || (c.emoji && !c.mark)) {
    cell.text_begin = cluster_text_.size();
    text.for_each_chunk(c.offset, std::min(c.length, kMaxShapedClusterUnits), [this](std::wstring_view chunk) {
      cluster_text_.append(chunk);
      return true;
    });
    cell.text_length = cluster_text_.size() - cell.text_begin;
  }
  cells_.push_back(cell);
}

void Renderer::draw_cluster(const Cell& cell, float x, float baseline, D2D1_COLOR_F color) {
  const std::wstring_view text(cluster_text_.data() + cell.text_begin, cell.text_length);
  const TextMeasurer::ClusterGlyphs& g = measurer_.cluster(text, cell.emoji);
  if (g.glyphs.empty()) return;
  // Wider than its cells: squeezed horizontally into them (PLAN 4.7), in
  // colour or not.
  const float slot = static_cast<float>(cell.width) * measurer_.cell_width();
  const bool squeeze = cell.width > 0 && g.width > slot + 0.5f;
  if (squeeze) {
    target_->SetTransform(D2D1::Matrix3x2F::Scale(slot / g.width, 1.0f, D2D1::Point2F(x, baseline)));
  }
  // Colour only for clusters shown as emoji (2 cells, decision G2 of M3c);
  // text-style symbols and U+FE0E stay in one colour (user, 2026-11-14).
  if (const TextMeasurer::ColorRun* layers = cell.emoji ? color_cluster(g) : nullptr) {
    draw_color_run(*layers, g.face, D2D1::Point2F(x, baseline), color);
  } else {
    DWRITE_GLYPH_RUN run{};
    run.fontFace = measurer_.face(g.face);
    run.fontEmSize = measurer_.face_em(g.face);
    run.glyphCount = static_cast<UINT32>(g.glyphs.size());
    run.glyphIndices = g.glyphs.data();
    run.glyphAdvances = g.advances.data();
    run.glyphOffsets = g.offsets.data();
    target_->DrawGlyphRun(D2D1::Point2F(x, baseline), &run, brush_, DWRITE_MEASURING_MODE_NATURAL);
  }
  if (squeeze) target_->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Renderer::flush_run(uint16_t face, float x, float baseline) {
  if (run_glyphs_.empty()) return;
  DWRITE_GLYPH_RUN run{};
  run.fontFace = measurer_.face(face);
  run.fontEmSize = measurer_.face_em(face);
  run.glyphCount = static_cast<UINT32>(run_glyphs_.size());
  run.glyphIndices = run_glyphs_.data();
  run.glyphAdvances = run_advances_.data();
  target_->DrawGlyphRun(D2D1::Point2F(x, baseline), &run, brush_, DWRITE_MEASURING_MODE_NATURAL);
  run_glyphs_.clear();
  run_advances_.clear();
}

void Renderer::draw_cells(float x0, float top, D2D1_COLOR_F color) {
  brush_->SetColor(color);
  const float cw = measurer_.cell_width();
  const float baseline = top + measurer_.ascent();
  uint16_t run_face = 0;
  float run_x = 0;
  float prev_x = 0;
  // The last drawn character with cells: zero-width characters (combining
  // marks) are drawn on it.
  bool has_base = false;
  uint16_t base_face = 0;
  float base_x = 0;
  float base_end = 0;  // where the base glyph's own advance ends
  for (const Cell& cell : cells_) {
    if (cell.text_length > 0) {
      // A cluster of several code points (decision G6): one glyph run in its
      // cells; one without cells goes over the base before it.
      flush_run(run_face, run_x, baseline);
      const float x = x0 + static_cast<float>(cell.column) * cw;
      draw_cluster(cell, cell.width == 0 && has_base ? base_x : x, baseline, color);
      has_base = false;
      continue;
    }
    if (is_blank(cell.c)) {
      flush_run(run_face, run_x, baseline);
      continue;
    }
    const float x = x0 + static_cast<float>(cell.column) * cw;
    const float slot = static_cast<float>(cell.width) * cw;
    if (cell.width == 0) {
      // A combining mark, drawn over the base character, in the base's font
      // when it has the mark. A mark with no advance goes at the end of the
      // base glyph's advance (where proportional fonts expect the pen); a
      // mark with an advance (monospace fonts such as Consolas centre it in
      // its own box) is drawn over the last part of the base glyph.
      flush_run(run_face, run_x, baseline);
      TextMeasurer::Glyph mark = measurer_.glyph(cell.c);
      if (has_base) {
        const uint16_t index = measurer_.glyph_index_in(base_face, cell.c);
        if (index != 0) {
          mark.face = base_face;
          mark.index = index;
          mark.advance = measurer_.advance_in(base_face, index);
        }
      }
      run_glyphs_.push_back(mark.index);
      run_advances_.push_back(0.0f);
      const float mark_x = has_base ? std::max(base_x, base_end - mark.advance) : x;
      flush_run(mark.face, mark_x, baseline);
      continue;
    }
    const TextMeasurer::Glyph g = measurer_.glyph(cell.c);
    has_base = true;
    base_face = g.face;
    base_x = x;
    base_end = x + std::min(g.advance, slot);
    if (const TextMeasurer::ColorRun* layers = cell.emoji ? color_glyph(g.face, g.index) : nullptr) {
      // A glyph of a colour font with colour (decision D1), only when its
      // cluster is shown as emoji: its layers, squeezed into its cells like
      // the glyph below.
      flush_run(run_face, run_x, baseline);
      const bool squeeze = g.advance > slot + 0.5f;
      if (squeeze) {
        target_->SetTransform(D2D1::Matrix3x2F::Scale(slot / g.advance, 1.0f, D2D1::Point2F(x, baseline)));
      }
      draw_color_run(*layers, g.face, D2D1::Point2F(x, baseline), color);
      if (squeeze) target_->SetTransform(D2D1::Matrix3x2F::Identity());
      continue;
    }
    if (g.advance > slot + 0.5f) {
      // Wider than its cells: squeeze it horizontally into them.
      flush_run(run_face, run_x, baseline);
      target_->SetTransform(D2D1::Matrix3x2F::Scale(slot / g.advance, 1.0f, D2D1::Point2F(x, baseline)));
      run_glyphs_.push_back(g.index);
      run_advances_.push_back(g.advance);
      flush_run(g.face, x, baseline);
      target_->SetTransform(D2D1::Matrix3x2F::Identity());
      continue;
    }
    if (run_glyphs_.empty() || g.face != run_face) {
      flush_run(run_face, run_x, baseline);
      run_face = g.face;
      run_x = x;
    } else {
      run_advances_.back() = x - prev_x;
    }
    run_glyphs_.push_back(g.index);
    run_advances_.push_back(slot);
    prev_x = x;
  }
  flush_run(run_face, run_x, baseline);
}

}  // namespace geditor::win
