#include "renderer.h"

#include <algorithm>
#include <cmath>

#include "char_width.h"
#include "echo_text.h"
#include "measure.h"
#include "unicode.h"

namespace geditor::win {
namespace {

// Colours: the defaults of docs/PLAN.md 4.5 (settings arrive in M2/M3).
constexpr UINT32 kBackground = 0x1e1e1e;
constexpr UINT32 kForeground = 0xd4d4d4;
constexpr UINT32 kCursor = 0xffffff;
constexpr UINT32 kSelection = 0x264f78;
constexpr UINT32 kModeLineBackground = 0x007acc;
constexpr UINT32 kModeLineForeground = 0xffffff;

constexpr float kMarginDip = 4.0f;  // left of the text, mode line and echo area
constexpr float kCursorWidthDip = 2.0f;

D2D1_COLOR_F rgb(UINT32 value) {
  return D2D1::ColorF(value);
}

template <typename T>
void safe_release(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

// Cells of `c` starting at `column`, with a fast path for printable ASCII.
int advance_of(char32_t c, int column, int tab_width) {
  if (c >= 0x20 && c < 0x7F) return 1;
  return cell_advance(c, column, tab_width);
}

// Characters that are never drawn: controls, the space (blank anyway) and
// invisible format characters (zero width space, joiners, marks, variation
// selectors, BOM), which would otherwise show a missing-glyph box.
bool is_blank(char32_t c) {
  return c <= 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0) || is_invisible_format_char(c);
}

// Calls f(code_point, offset) for the code points of `s`; f returns false to
// stop. Returns false when stopped.
template <typename F>
bool for_each_code_point(std::wstring_view s, F&& f) {
  size_t i = 0;
  while (i < s.size()) {
    size_t units = 0;
    const char32_t c = decode_at(s, i, &units);
    if (!f(c, i)) return false;
    i += units;
  }
  return true;
}

}  // namespace

Renderer::~Renderer() {
  release();
}

bool Renderer::init(const wchar_t* font_family, float font_size_pt) {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_))) return false;
  mark_stage("d2d_factory");
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&dwrite_factory_)))) {
    return false;
  }
  mark_stage("dwrite_factory");
  const bool ok = measurer_.init(dwrite_factory_, font_family, font_size_pt * 96.0f / 72.0f);
  mark_stage("font");
  return ok;
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

bool Renderer::ensure_target(HWND hwnd) {
  if (hwnd_target_ != nullptr) return true;
  if (d2d_factory_ == nullptr) return false;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
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
  if (FAILED(hwnd_target_->CreateSolidColorBrush(rgb(kForeground), &hwnd_brush_))) {
    safe_release(hwnd_target_);
    return false;
  }
  return true;
}

void Renderer::discard_target() {
  safe_release(hwnd_brush_);
  safe_release(hwnd_target_);
}

void Renderer::resize(UINT width, UINT height) {
  if (hwnd_target_ != nullptr) hwnd_target_->Resize(D2D1::SizeU(width, height));
}

void Renderer::set_dpi(float dpi) {
  if (hwnd_target_ != nullptr) hwnd_target_->SetDpi(dpi, dpi);
}

HRESULT Renderer::paint(HWND hwnd, const Editor& editor, ViewState& view) {
  if (!ensure_target(hwnd)) return E_FAIL;
  mark_stage("render_target");
  target_ = hwnd_target_;
  brush_ = hwnd_brush_;
  const HRESULT hr = draw_frame(editor, view);
  target_ = nullptr;
  brush_ = nullptr;
  return hr;
}

HRESULT Renderer::paint_to_dc(HWND hwnd, HDC dc, const Editor& editor, const ViewState& view) {
  if (d2d_factory_ == nullptr) return E_FAIL;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
  const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
  ID2D1DCRenderTarget* dc_target = nullptr;
  ID2D1SolidColorBrush* dc_brush = nullptr;
  HRESULT hr = d2d_factory_->CreateDCRenderTarget(&props, &dc_target);
  if (SUCCEEDED(hr)) hr = dc_target->BindDC(dc, &rc);
  if (SUCCEEDED(hr)) hr = dc_target->CreateSolidColorBrush(rgb(kForeground), &dc_brush);
  if (SUCCEEDED(hr)) {
    target_ = dc_target;
    brush_ = dc_brush;
    ViewState copy = view;  // printing does not change the view
    hr = draw_frame(editor, copy);
    target_ = nullptr;
    brush_ = nullptr;
  }
  safe_release(dc_brush);
  safe_release(dc_target);
  return hr;
}

HRESULT Renderer::draw_frame(const Editor& editor, ViewState& view) {
  target_->BeginDraw();
  target_->SetTransform(D2D1::Matrix3x2F::Identity());
  target_->Clear(rgb(kBackground));
  const D2D1_SIZE_F size = target_->GetSize();
  const float lh = measurer_.line_height();
  const int rows = echo_rows(size.height, editor);
  const float echo_y = size.height - static_cast<float>(rows) * lh;
  const float mode_y = echo_y - lh;
  draw_text_area(editor, view, size.width, std::max(0.0f, mode_y));
  draw_mode_line(editor, mode_y, size.width);
  draw_echo_area(editor, view, echo_y, size.width, rows);
  mark_stage("draw_calls");
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
  const PieceTree& text = b.text;
  const int tab_width = editor.tab_width();
  const float cw = measurer_.cell_width();
  const float lh = measurer_.line_height();
  const int columns = std::max(1, static_cast<int>(std::floor((width - kMarginDip) / cw)));

  // Horizontal scrolling keeps the cursor column visible (no wrapping in M1).
  const int point_column = editor.point_column();
  if (point_column < view.left_column || point_column >= view.left_column + columns) {
    view.left_column = point_column < columns ? 0 : point_column - columns / 2;
  }
  const int left = view.left_column;

  const bool region = b.has_mark && b.mark_active && b.mark != b.point;
  const size_t region_start = std::min(b.mark, b.point);
  const size_t region_end = std::max(b.mark, b.point);

  target_->PushAxisAlignedClip(D2D1::RectF(kMarginDip, 0, width, height),
                               D2D1_ANTIALIAS_MODE_ALIASED);
  const size_t top = editor.top_line();
  const int rows = static_cast<int>(std::ceil(height / lh));  // the last one may be partial
  for (int row = 0; row < rows; ++row) {
    const size_t line = top + static_cast<size_t>(row);
    if (line >= text.line_count()) break;
    const float y = static_cast<float>(row) * lh;
    int end_column = 0;
    const bool reached_end = collect_line(text, line, left, columns + 1, tab_width, &end_column);
    if (region) {
      // Highlight runs of region cells, and the line end as one cell.
      int run_start = 0;
      int run_end = 0;
      bool in_run = false;
      auto add = [&](int column, int cells) {
        if (in_run && column == run_end) {
          run_end = column + cells;
          return;
        }
        if (in_run) fill(kMarginDip + run_start * cw, y, kMarginDip + run_end * cw, y + lh, rgb(kSelection));
        run_start = column;
        run_end = column + cells;
        in_run = true;
      };
      for (const Cell& c : cells_) {
        if (c.width > 0 && c.offset >= region_start && c.offset < region_end) add(c.column, c.width);
      }
      const size_t line_end = text.line_end(line);
      if (reached_end && line_end < text.length() && line_end >= region_start &&
          line_end < region_end) {
        add(end_column, 1);
      }
      if (in_run) fill(kMarginDip + run_start * cw, y, kMarginDip + run_end * cw, y + lh, rgb(kSelection));
    }
    draw_cells(kMarginDip, y, rgb(kForeground));
  }

  if (!editor.minibuffer_active()) {
    const size_t point_line = editor.point_line();
    if (point_line >= top && point_line < top + static_cast<size_t>(rows)) {
      const float x = kMarginDip + static_cast<float>(point_column - left) * cw;
      const float y = static_cast<float>(point_line - top) * lh;
      fill(x, y, x + kCursorWidthDip, y + lh, rgb(kCursor));
      view.caret_x = x;
      view.caret_y = y;
      view.caret_height = lh;
    }
  }
  target_->PopAxisAlignedClip();
}

void Renderer::draw_mode_line(const Editor& editor, float y, float width) {
  const float lh = measurer_.line_height();
  fill(0, y, width, y + lh, rgb(kModeLineBackground));
  const int columns =
      std::max(1, static_cast<int>(std::floor((width - kMarginDip) / measurer_.cell_width())));
  collect_string(editor.mode_line(), 0, columns + 1, editor.tab_width());
  draw_cells(kMarginDip, y, rgb(kModeLineForeground));
}

void Renderer::draw_echo_area(const Editor& editor, ViewState& view, float y, float width, int rows) {
  const float cw = measurer_.cell_width();
  const float lh = measurer_.line_height();
  const int columns = std::max(1, static_cast<int>(std::floor((width - kMarginDip) / cw)));
  const bool multi_line = editor.message_is_multi_line();
  if (multi_line) {
    // Settings errors, one per line (as many as `rows` allows); the
    // minibuffer line (if any) below.
    const int message_rows = rows - (editor.minibuffer_active() ? 1 : 0);
    for (const std::wstring& line : editor.message_lines(message_rows)) {
      draw_echo_line(fit_message(line, columns), y, columns);
      y += lh;
    }
  }
  std::wstring text;
  int cursor_column = -1;
  if (editor.minibuffer_active()) {
    // The prompt stays visible; the input is abbreviated on the left.
    const Buffer& mb = editor.minibuffer_buffer();
    const MinibufferLine line = fit_minibuffer(editor.minibuffer_prompt(), mb.text.text(), mb.point,
                                               multi_line ? std::wstring() : editor.message(), columns);
    text = line.text;
    cursor_column = line.cursor_column;
  } else if (multi_line) {
    return;
  } else if (view.show_pending_keys && !editor.pending_keys_text().empty()) {
    text = fit_message(editor.pending_keys_text(), columns);
  } else {
    text = fit_message(editor.message(), columns);
  }
  draw_echo_line(text, y, columns);
  if (cursor_column >= 0) {
    const float x = kMarginDip + static_cast<float>(cursor_column) * cw;
    fill(x, y, x + kCursorWidthDip, y + lh, rgb(kCursor));
    view.caret_x = x;
    view.caret_y = y;
    view.caret_height = lh;
  }
}

void Renderer::draw_echo_line(std::wstring_view text, float y, int columns) {
  // The echo line counts TAB as one cell, like fit_message / fit_minibuffer.
  constexpr int kEchoTabWidth = 1;
  collect_string(text, 0, columns + 1, kEchoTabWidth);
  draw_cells(kMarginDip, y, rgb(kForeground));
}

bool Renderer::collect_line(const PieceTree& text, size_t line, int left, int columns,
                            int tab_width, int* end_column) {
  cells_.clear();
  const size_t start = text.line_start(line);
  const size_t end = text.line_end(line);
  const int right = left + columns;
  int column = 0;
  auto visit = [&](char32_t c, size_t offset) {
    if (column >= right) return false;
    const int w = advance_of(c, column, tab_width);
    if (column + w > left || (w == 0 && column >= left)) {
      cells_.push_back(Cell{c, column - left, w, offset});
    }
    column += w;
    return true;
  };
  size_t pos = start;
  wchar_t high = 0;
  bool stopped = false;
  text.for_each_chunk(start, end - start, [&](std::wstring_view chunk) {
    for (const wchar_t u : chunk) {
      const size_t at = pos++;
      if (high != 0) {
        const wchar_t h = high;
        high = 0;
        if (is_low_surrogate(u)) {
          if (!visit(combine_surrogates(h, u), at - 1)) {
            stopped = true;
            return false;
          }
          continue;
        }
        if (!visit(static_cast<char32_t>(h), at - 1)) {
          stopped = true;
          return false;
        }
      }
      if (is_high_surrogate(u)) {
        high = u;
        continue;
      }
      if (!visit(static_cast<char32_t>(u), at)) {
        stopped = true;
        return false;
      }
    }
    return true;
  });
  if (!stopped && high != 0 && !visit(static_cast<char32_t>(high), pos - 1)) stopped = true;
  *end_column = column - left;
  return !stopped;
}

int Renderer::collect_string(std::wstring_view s, int left, int columns, int tab_width) {
  cells_.clear();
  const int right = left + columns;
  int column = 0;
  for_each_code_point(s, [&](char32_t c, size_t offset) {
    if (column >= right) return false;
    const int w = advance_of(c, column, tab_width);
    if (column + w > left || (w == 0 && column >= left)) {
      cells_.push_back(Cell{c, column - left, w, offset});
    }
    column += w;
    return true;
  });
  return column - left;
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
