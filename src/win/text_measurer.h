// DirectWrite font faces for the cell grid (docs/PLAN.md 4.7): the primary
// font gives the cell width (advance of '0') and the line height; characters
// missing from it are mapped to another font with IDWriteFontFallback, which
// is created on the first missing character only: the settings' fallback
// families (built with IDWriteFontFallbackBuilder, the system fallback after
// them) or the system fallback alone (decision E1).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <dwrite_2.h>

namespace geditor::win {

// The font of the settings (font.family, font.fallback) at a size in DIPs.
struct FontSpec {
  std::wstring family = L"Consolas";
  std::vector<std::wstring> fallback;
  float em = 16;  // DIPs

  friend bool operator==(const FontSpec&, const FontSpec&) = default;
};

class TextMeasurer {
 public:
  struct Glyph {
    uint16_t face = 0;   // index for face() / face_em()
    uint16_t index = 0;  // glyph index in that face (0 = missing glyph box)
    float advance = 0;   // natural advance in DIPs
  };

  TextMeasurer() = default;
  ~TextMeasurer();
  TextMeasurer(const TextMeasurer&) = delete;
  TextMeasurer& operator=(const TextMeasurer&) = delete;

  // `factory` must outlive this object. Family names that are not installed
  // are added to `missing` (when not null): a missing primary family is
  // replaced with Consolas (or the first installed family), missing fallback
  // families are left out (decision E1).
  bool init(IDWriteFactory* factory, const FontSpec& spec, std::vector<std::wstring>* missing);
  void release();

  float em() const { return em_; }
  float cell_width() const { return cell_width_; }
  float line_height() const { return line_height_; }
  float ascent() const { return ascent_; }
  // The primary family in use.
  const wchar_t* family() const { return family_.c_str(); }
  // The family name of the font that draws `c` (diagnostics and tests).
  std::wstring family_of(char32_t c);

  Glyph glyph(char32_t c);
  // Glyph index of `c` in face `face` (0 when that face does not have it).
  uint16_t glyph_index_in(uint16_t face, char32_t c) const;
  // Natural advance in DIPs of glyph `index` of face `face`.
  float advance_in(uint16_t face, uint16_t index) const { return advance_of(face, index); }
  IDWriteFontFace* face(uint16_t i) const { return faces_[i].face; }
  float face_em(uint16_t i) const { return faces_[i].em; }

 private:
  struct Face {
    IDWriteFontFace* face = nullptr;
    float em = 0;              // font size in DIPs (fallback fonts may scale)
    float design_to_dip = 0;   // em / design units per em
    std::vector<uint8_t> key;  // file reference key, index and simulations
    std::wstring family;
  };

  bool add_face(IDWriteFontFace* face, float em, const std::wstring& family, uint16_t* index);
  Glyph lookup(char32_t c);
  Glyph fallback_glyph(char32_t c);
  void create_fallback();
  float advance_of(uint16_t face, uint16_t glyph) const;

  IDWriteFactory* factory_ = nullptr;
  IDWriteFontCollection* collection_ = nullptr;
  IDWriteFontFallback* fallback_ = nullptr;
  bool fallback_tried_ = false;
  std::wstring family_;
  std::vector<std::wstring> fallback_families_;  // installed ones only
  wchar_t locale_[85] = {};  // LOCALE_NAME_MAX_LENGTH
  float em_ = 0;
  float cell_width_ = 0;
  float line_height_ = 0;
  float ascent_ = 0;
  std::vector<Face> faces_;
  Glyph ascii_[128] = {};
  bool ascii_known_[128] = {};
  std::unordered_map<char32_t, Glyph> cache_;
};

}  // namespace geditor::win
