// IDWriteFactory4 / IDWriteFactory8 and the glyph image formats (colour
// glyphs, M3d) are declared for NTDDI versions newer than the project's
// _WIN32_WINNT; they are asked for at run time (QueryInterface), so this
// raises NTDDI_VERSION for this file only, before any header.
#define NTDDI_VERSION 0x0A00000D  // NTDDI_WIN10_CU

#include "text_measurer.h"

#include <windows.h>

#include <dwrite_3.h>

#include <cmath>
#include <cwchar>

namespace geditor::win {
namespace {

template <typename T>
void safe_release(T*& p) {
  if (p != nullptr) {
    p->Release();
    p = nullptr;
  }
}

// The text of one character for IDWriteFontFallback::MapCharacters. It lives
// on the stack for the duration of the call, so reference counting is a no-op.
class CharSource final : public IDWriteTextAnalysisSource {
 public:
  CharSource(const wchar_t* text, UINT32 length, const wchar_t* locale)
      : text_(text), length_(length), locale_(locale) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSource)) {
      *object = static_cast<IDWriteTextAnalysisSource*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
                                              UINT32* length) noexcept override {
    if (position < length_) {
      *text = text_ + position;
      *length = length_ - position;
    } else {
      *text = nullptr;
      *length = 0;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text,
                                                  UINT32* length) noexcept override {
    if (position > 0 && position <= length_) {
      *text = text_;
      *length = position;
    } else {
      *text = nullptr;
      *length = 0;
    }
    return S_OK;
  }
  DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() noexcept override {
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
  }
  HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length,
                                          const WCHAR** locale) noexcept override {
    *length = position < length_ ? length_ - position : 0;
    *locale = locale_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetNumberSubstitution(
      UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) noexcept override {
    *length = position < length_ ? length_ - position : 0;
    *substitution = nullptr;
    return S_OK;
  }

 private:
  const wchar_t* text_;
  UINT32 length_;
  const wchar_t* locale_;
};

// Receives the script of the text from IDWriteTextAnalyzer::AnalyzeScript
// (the first run: a cluster has one script). On the stack, like CharSource.
class ScriptSink final : public IDWriteTextAnalysisSink {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSink)) {
      *object = static_cast<IDWriteTextAnalysisSink*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32 position, UINT32,
                                              const DWRITE_SCRIPT_ANALYSIS* analysis) noexcept override {
    if (!found && position == 0) {
      script = *analysis;
      found = true;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) noexcept override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32, UINT32, UINT8, UINT8) noexcept override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) noexcept override {
    return S_OK;
  }

  DWRITE_SCRIPT_ANALYSIS script{};
  bool found = false;
};

// The first code point of `text`.
char32_t first_code_point(std::wstring_view text) {
  if (text.empty()) return 0;
  const char32_t c = static_cast<char32_t>(text[0]);
  if (c >= 0xD800 && c <= 0xDBFF && text.size() > 1 && text[1] >= 0xDC00 && text[1] <= 0xDFFF) {
    return 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text[1]) - 0xDC00);
  }
  return c;
}

// Identity of a font face: its first file's reference key, index and
// simulations (different IDWriteFontFace objects can describe one face).
std::vector<uint8_t> face_key(IDWriteFontFace* face) {
  std::vector<uint8_t> key;
  UINT32 count = 0;
  if (FAILED(face->GetFiles(&count, nullptr)) || count == 0) return key;
  std::vector<IDWriteFontFile*> files(count, nullptr);
  if (SUCCEEDED(face->GetFiles(&count, files.data()))) {
    const void* data = nullptr;
    UINT32 size = 0;
    if (files[0] != nullptr && SUCCEEDED(files[0]->GetReferenceKey(&data, &size))) {
      const auto* bytes = static_cast<const uint8_t*>(data);
      key.assign(bytes, bytes + size);
    }
  }
  for (IDWriteFontFile*& f : files) safe_release(f);
  const UINT32 index = face->GetIndex();
  const auto simulations = static_cast<uint32_t>(face->GetSimulations());
  for (int i = 0; i < 4; ++i) key.push_back(static_cast<uint8_t>(index >> (8 * i)));
  for (int i = 0; i < 4; ++i) key.push_back(static_cast<uint8_t>(simulations >> (8 * i)));
  return key;
}

// The English (or first) name of a font's family.
std::wstring family_name(IDWriteFont* font) {
  std::wstring name;
  IDWriteFontFamily* family = nullptr;
  IDWriteLocalizedStrings* names = nullptr;
  if (SUCCEEDED(font->GetFontFamily(&family)) && SUCCEEDED(family->GetFamilyNames(&names))) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) index = 0;
    UINT32 length = 0;
    if (SUCCEEDED(names->GetStringLength(index, &length))) {
      name.resize(length + 1);
      if (SUCCEEDED(names->GetString(index, name.data(), length + 1))) {
        name.resize(length);
      } else {
        name.clear();
      }
    }
  }
  safe_release(names);
  safe_release(family);
  return name;
}

bool family_installed(IDWriteFontCollection* collection, const std::wstring& name, UINT32* index) {
  BOOL exists = FALSE;
  return !name.empty() && SUCCEEDED(collection->FindFamilyName(name.c_str(), index, &exists)) && exists;
}

}  // namespace

TextMeasurer::~TextMeasurer() {
  release();
}

void TextMeasurer::release() {
  for (Face& f : faces_) safe_release(f.face);
  faces_.clear();
  cache_.clear();
  clusters_.clear();
  safe_release(analyzer_);
  for (bool& known : ascii_known_) known = false;
  safe_release(fallback_);
  safe_release(collection_);
  fallback_tried_ = false;
  fallback_families_.clear();
  color_glyphs_.clear();
  safe_release(color_factory8_);
  safe_release(color_factory4_);
  safe_release(color_factory2_);
  color_factory_tried_ = false;
  color_failed_ = false;
  factory_ = nullptr;
}

bool TextMeasurer::init(IDWriteFactory* factory, const FontSpec& spec, std::vector<std::wstring>* missing) {
  release();
  factory_ = factory;
  em_ = spec.em;
  if (FAILED(factory_->GetSystemFontCollection(&collection_, FALSE))) return false;

  UINT32 family_index = 0;
  family_ = spec.family;
  if (!family_installed(collection_, family_, &family_index)) {
    if (missing != nullptr) missing->push_back(spec.family);
    // Consolas ships with Windows; the first family otherwise.
    family_ = L"Consolas";
    if (!family_installed(collection_, family_, &family_index)) family_index = 0;
  }
  for (const std::wstring& name : spec.fallback) {
    UINT32 unused = 0;
    if (family_installed(collection_, name, &unused)) {
      fallback_families_.push_back(name);
    } else if (missing != nullptr) {
      missing->push_back(name);
    }
  }
  IDWriteFontFamily* font_family = nullptr;
  IDWriteFont* font = nullptr;
  IDWriteFontFace* face = nullptr;
  bool ok = SUCCEEDED(collection_->GetFontFamily(family_index, &font_family)) &&
            SUCCEEDED(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                                        DWRITE_FONT_STRETCH_NORMAL,
                                                        DWRITE_FONT_STYLE_NORMAL, &font)) &&
            SUCCEEDED(font->CreateFontFace(&face));
  if (ok) {
    const std::wstring name = family_name(font);
    if (!name.empty()) family_ = name;
  }
  uint16_t index = 0;
  if (ok) ok = add_face(face, em_, family_, &index);
  safe_release(face);
  safe_release(font);
  safe_release(font_family);
  if (!ok) return false;

  DWRITE_FONT_METRICS metrics{};
  faces_[0].face->GetMetrics(&metrics);
  const float scale = faces_[0].design_to_dip;
  ascent_ = std::ceil(metrics.ascent * scale);
  line_height_ = ascent_ + std::ceil((metrics.descent + metrics.lineGap) * scale);
  const Glyph zero = glyph(U'0');
  cell_width_ = zero.advance > 0 ? zero.advance : em_ / 2;
  return true;
}

bool TextMeasurer::add_face(IDWriteFontFace* face, float em, const std::wstring& family, uint16_t* index) {
  std::vector<uint8_t> key = face_key(face);
  for (size_t i = 0; i < faces_.size(); ++i) {
    if (faces_[i].face == face || (!key.empty() && faces_[i].key == key && faces_[i].em == em)) {
      *index = static_cast<uint16_t>(i);
      return true;
    }
  }
  if (faces_.size() >= 0xFFFF) return false;
  DWRITE_FONT_METRICS metrics{};
  face->GetMetrics(&metrics);
  if (metrics.designUnitsPerEm == 0) return false;
  face->AddRef();
  Face f;
  f.face = face;
  f.em = em;
  f.design_to_dip = em / static_cast<float>(metrics.designUnitsPerEm);
  f.key = std::move(key);
  f.family = family;
  faces_.push_back(std::move(f));
  *index = static_cast<uint16_t>(faces_.size() - 1);
  return true;
}

float TextMeasurer::advance_of(uint16_t face, uint16_t glyph) const {
  DWRITE_GLYPH_METRICS m{};
  if (FAILED(faces_[face].face->GetDesignGlyphMetrics(&glyph, 1, &m, FALSE))) return 0;
  return static_cast<float>(m.advanceWidth) * faces_[face].design_to_dip;
}

uint16_t TextMeasurer::glyph_index_in(uint16_t face, char32_t c) const {
  if (face >= faces_.size()) return 0;
  const UINT32 code = static_cast<UINT32>(c);
  uint16_t index = 0;
  if (FAILED(faces_[face].face->GetGlyphIndices(&code, 1, &index))) return 0;
  return index;
}

TextMeasurer::Glyph TextMeasurer::glyph(char32_t c) {
  if (c < 128) {
    if (!ascii_known_[c]) {
      ascii_[c] = lookup(c);
      ascii_known_[c] = true;
    }
    return ascii_[c];
  }
  const auto it = cache_.find(c);
  if (it != cache_.end()) return it->second;
  const Glyph g = lookup(c);
  cache_.emplace(c, g);
  return g;
}

TextMeasurer::Glyph TextMeasurer::lookup(char32_t c) {
  Glyph g;
  if (faces_.empty()) return g;
  const UINT32 code = static_cast<UINT32>(c);
  uint16_t index = 0;
  if (SUCCEEDED(faces_[0].face->GetGlyphIndices(&code, 1, &index)) && index != 0) {
    g.face = 0;
    g.index = index;
    g.advance = advance_of(0, index);
    return g;
  }
  g = fallback_glyph(c);
  if (g.index == 0) {
    // No font has it: draw the missing-glyph box of the primary font.
    g.face = 0;
    g.advance = advance_of(0, 0);
  }
  return g;
}

void TextMeasurer::create_fallback() {
  fallback_tried_ = true;
  if (GetUserDefaultLocaleName(locale_, static_cast<int>(sizeof(locale_) / sizeof(locale_[0]))) == 0) {
    wcscpy_s(locale_, L"en-us");
  }
  IDWriteFactory2* factory2 = nullptr;
  if (FAILED(factory_->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&factory2)))) {
    return;
  }
  IDWriteFontFallback* system = nullptr;
  factory2->GetSystemFontFallback(&system);
  if (fallback_families_.empty()) {
    fallback_ = system;
    factory2->Release();
    return;
  }
  // The settings' families first (for every character), then the system
  // fallback for the characters that none of them has.
  IDWriteFontFallbackBuilder* builder = nullptr;
  if (SUCCEEDED(factory2->CreateFontFallbackBuilder(&builder))) {
    const DWRITE_UNICODE_RANGE all{0, 0x10FFFF};
    std::vector<const WCHAR*> names;
    for (const std::wstring& name : fallback_families_) names.push_back(name.c_str());
    bool ok = SUCCEEDED(builder->AddMapping(&all, 1, names.data(), static_cast<UINT32>(names.size()),
                                            collection_, nullptr, nullptr, 1.0f));
    if (ok && system != nullptr) ok = SUCCEEDED(builder->AddMappings(system));
    if (ok) builder->CreateFontFallback(&fallback_);
    builder->Release();
  }
  if (fallback_ == nullptr) {
    fallback_ = system;  // the builder failed: the system fallback alone
  } else {
    safe_release(system);
  }
  factory2->Release();
}

std::wstring TextMeasurer::family_of(char32_t c) {
  const Glyph g = glyph(c);
  if (g.face >= faces_.size()) return std::wstring();
  return faces_[g.face].family;
}

TextMeasurer::Glyph TextMeasurer::fallback_glyph(char32_t c) {
  Glyph g;
  if (!fallback_tried_) create_fallback();
  if (fallback_ == nullptr) return g;

  wchar_t text[2];
  UINT32 length = 1;
  if (c >= 0x10000) {
    text[0] = static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10));
    text[1] = static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF));
    length = 2;
  } else {
    text[0] = static_cast<wchar_t>(c);
  }
  CharSource source(text, length, locale_);
  UINT32 mapped_length = 0;
  IDWriteFont* font = nullptr;
  float scale = 1.0f;
  if (FAILED(fallback_->MapCharacters(&source, 0, length, collection_, family_.c_str(),
                                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, &mapped_length, &font, &scale)) ||
      font == nullptr) {
    return g;
  }
  IDWriteFontFace* face = nullptr;
  uint16_t face_index = 0;
  const bool ok =
      SUCCEEDED(font->CreateFontFace(&face)) && add_face(face, em_ * scale, family_name(font), &face_index);
  safe_release(face);
  safe_release(font);
  if (!ok) return g;
  const UINT32 code = static_cast<UINT32>(c);
  uint16_t index = 0;
  if (FAILED(faces_[face_index].face->GetGlyphIndices(&code, 1, &index)) || index == 0) return g;
  g.face = face_index;
  g.index = index;
  g.advance = advance_of(face_index, index);
  return g;
}

bool TextMeasurer::covers(uint16_t face, std::wstring_view text) const {
  size_t i = 0;
  while (i < text.size()) {
    char32_t c = static_cast<char32_t>(text[i]);
    size_t units = 1;
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xDC00);
      units = 2;
    }
    i += units;
    // Joiners, variation selectors and tags: the shaper hides them.
    const bool ignorable = (c >= 0x200B && c <= 0x200F) || (c >= 0x2060 && c <= 0x2064) ||
                           (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0000 && c <= 0xE0FFF);
    if (!ignorable && glyph_index_in(face, c) == 0) return false;
  }
  return true;
}

bool TextMeasurer::map_cluster(std::wstring_view text, const wchar_t* base_family, uint16_t* face) {
  CharSource source(text.data(), static_cast<UINT32>(text.size()), locale_);
  UINT32 mapped_length = 0;
  IDWriteFont* font = nullptr;
  float scale = 1.0f;
  if (FAILED(fallback_->MapCharacters(&source, 0, static_cast<UINT32>(text.size()), collection_, base_family,
                                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, &mapped_length, &font, &scale)) ||
      font == nullptr) {
    return false;
  }
  IDWriteFontFace* font_face = nullptr;
  const bool ok = mapped_length > 0 && SUCCEEDED(font->CreateFontFace(&font_face)) &&
                  add_face(font_face, em_ * scale, family_name(font), face);
  safe_release(font_face);
  safe_release(font);
  return ok;
}

bool TextMeasurer::cluster_face(std::wstring_view text, bool emoji, uint16_t* face) {
  if (!fallback_tried_) create_fallback();
  if (fallback_ != nullptr) {
    // MapCharacters for the whole cluster, from the primary family.
    uint16_t mapped = 0;
    const bool have = map_cluster(text, family_.c_str(), &mapped);
    // A cluster shown as emoji (U+263A U+FE0F), which the primary family may
    // have as text (U+263A in Consolas), or a font that lacks part of the
    // cluster (U+20E3 of a keycap) would show a box: then the font that
    // MapCharacters picks without the primary family, when it has all of it.
    if (!have || emoji || !covers(mapped, text)) {
      uint16_t other = 0;
      if (map_cluster(text, L"", &other) && covers(other, text)) {
        *face = other;
        return true;
      }
    }
    if (have) {
      *face = mapped;
      return true;
    }
  }
  // No fallback: the face of the first code point when one has it.
  const Glyph g = glyph(first_code_point(text));
  if (g.index == 0) return false;
  *face = g.face;
  return true;
}

bool TextMeasurer::shape_cluster(std::wstring_view text, bool emoji, ClusterGlyphs* out) {
  if (fail_next_shaping_) {
    fail_next_shaping_ = false;
    return false;
  }
  if (text.empty() || text.size() > 0xFFFF || factory_ == nullptr) return false;
  uint16_t face = 0;
  if (!cluster_face(text, emoji, &face)) return false;
  if (analyzer_ == nullptr && FAILED(factory_->CreateTextAnalyzer(&analyzer_))) return false;
  const UINT32 length = static_cast<UINT32>(text.size());
  CharSource source(text.data(), length, locale_);
  ScriptSink sink;
  if (FAILED(analyzer_->AnalyzeScript(&source, 0, length, &sink)) || !sink.found) return false;

  std::vector<UINT16> cluster_map(length);
  std::vector<DWRITE_SHAPING_TEXT_PROPERTIES> text_props(length);
  UINT32 max_glyphs = length * 3 / 2 + 16;
  std::vector<UINT16> glyphs;
  std::vector<DWRITE_SHAPING_GLYPH_PROPERTIES> glyph_props;
  UINT32 count = 0;
  HRESULT hr = E_NOT_SUFFICIENT_BUFFER;
  for (int attempt = 0; attempt < 3 && hr == E_NOT_SUFFICIENT_BUFFER; ++attempt) {
    glyphs.resize(max_glyphs);
    glyph_props.resize(max_glyphs);
    hr = analyzer_->GetGlyphs(text.data(), length, faces_[face].face, FALSE, FALSE, &sink.script, locale_,
                              nullptr, nullptr, nullptr, 0, max_glyphs, cluster_map.data(), text_props.data(),
                              glyphs.data(), glyph_props.data(), &count);
    max_glyphs *= 2;
  }
  if (FAILED(hr) || count == 0) return false;
  std::vector<FLOAT> advances(count);
  std::vector<DWRITE_GLYPH_OFFSET> offsets(count);
  if (FAILED(analyzer_->GetGlyphPlacements(text.data(), cluster_map.data(), text_props.data(), length,
                                           glyphs.data(), glyph_props.data(), count, faces_[face].face,
                                           faces_[face].em, FALSE, FALSE, &sink.script, locale_, nullptr,
                                           nullptr, 0, advances.data(), offsets.data()))) {
    return false;
  }
  bool any = false;
  for (UINT32 i = 0; i < count; ++i) {
    if (glyphs[i] != 0) any = true;
  }
  if (!any) return false;  // every glyph missing: the fallback shows the box
  out->face = face;
  out->glyphs.assign(glyphs.begin(), glyphs.begin() + count);
  out->advances.assign(advances.begin(), advances.end());
  out->offsets.assign(offsets.begin(), offsets.end());
  out->width = 0;
  for (const float a : out->advances) out->width += a;
  out->shaped = true;
  return true;
}

const TextMeasurer::ClusterGlyphs& TextMeasurer::cluster(std::wstring_view text, bool emoji) {
  // The key buffer is reused: a lookup of a known cluster allocates nothing
  // (decision D4 of M3d).
  cluster_key_.assign(text);
  cluster_key_.push_back(emoji ? L'1' : L'0');  // the presentation is part of the key
  const auto it = clusters_.find(cluster_key_);
  if (it != clusters_.end()) return it->second;
  std::wstring key = cluster_key_;
  ClusterGlyphs run;
  if (!shape_cluster(text, emoji, &run)) {
    // Decision C8: the first code point's glyph (the missing-glyph box of
    // the primary font when no font has it), never nothing.
    const Glyph g = glyph(first_code_point(text));
    run = ClusterGlyphs();
    run.face = g.face;
    run.glyphs.push_back(g.index);
    run.advances.push_back(g.advance);
    run.offsets.push_back(DWRITE_GLYPH_OFFSET{0, 0});
    run.width = g.advance;
    run.shaped = false;
  }
  // A bound on the cache (clusters of more than one code point are rare).
  if (clusters_.size() >= 4096) clusters_.clear();
  return clusters_.emplace(std::move(key), std::move(run)).first->second;
}

std::wstring TextMeasurer::family_of_cluster(std::wstring_view text, bool emoji) {
  const ClusterGlyphs& run = cluster(text, emoji);
  if (run.face >= faces_.size()) return std::wstring();
  return faces_[run.face].family;
}

// ---- Colour glyphs (M3d decisions D1, D3, D4) ----

bool TextMeasurer::is_color_face(uint16_t face) {
  if (face >= faces_.size()) return false;
  Face& f = faces_[face];
  if (f.color < 0) {
    f.color = 0;
    IDWriteFontFace2* face2 = nullptr;
    if (SUCCEEDED(f.face->QueryInterface(__uuidof(IDWriteFontFace2), reinterpret_cast<void**>(&face2)))) {
      f.color = face2->IsColorFont() ? 1 : 0;
      face2->Release();
    }
  }
  return f.color == 1;
}

void TextMeasurer::drop_color_layers() {
  color_glyphs_.clear();
  for (auto& [key, run] : clusters_) {
    run.color_state = 0;
    run.color = ColorRun();
  }
}

void TextMeasurer::set_color_formats(uint32_t formats, uint32_t paint_level) {
  if (formats == color_formats_ && paint_level == paint_level_) return;
  color_formats_ = formats;
  paint_level_ = paint_level;
  drop_color_layers();
}

void TextMeasurer::fail_next_color() {
  fail_next_color_ = true;
  drop_color_layers();
}

void TextMeasurer::create_color_factory() {
  color_factory_tried_ = true;
  if (factory_ == nullptr) return;
  // The newest interface that the system has: IDWriteFactory8 translates
  // the COLR paint tree, IDWriteFactory4 chooses among the image formats,
  // IDWriteFactory2 gives COLR layers only.
  if (FAILED(factory_->QueryInterface(__uuidof(IDWriteFactory8), reinterpret_cast<void**>(&color_factory8_)))) {
    color_factory8_ = nullptr;
  }
  if (FAILED(factory_->QueryInterface(__uuidof(IDWriteFactory4), reinterpret_cast<void**>(&color_factory4_)))) {
    color_factory4_ = nullptr;
  }
  if (FAILED(factory_->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&color_factory2_)))) {
    color_factory2_ = nullptr;
  }
}

namespace {

// Appends one layer of an enumerated colour run to `out`; false when the
// layer is not of face `face` at size `em` (the caller draws the run in one
// colour then).
bool append_layer(const DWRITE_GLYPH_RUN& run, uint32_t format, float x, float y,
                  const DWRITE_COLOR_F& color, UINT16 palette_index, IDWriteFontFace* face, float em,
                  TextMeasurer::ColorRun* out) {
  if (run.fontFace != face || run.fontEmSize != em || run.glyphCount == 0 || run.glyphIndices == nullptr) {
    return false;
  }
  TextMeasurer::ColorLayer layer;
  layer.format = format;
  layer.x = x;
  layer.y = y;
  layer.color = color;
  layer.foreground = palette_index == 0xFFFF;
  layer.first = static_cast<uint32_t>(out->glyphs.size());
  layer.count = run.glyphCount;
  for (UINT32 i = 0; i < run.glyphCount; ++i) {
    out->glyphs.push_back(run.glyphIndices[i]);
    out->advances.push_back(run.glyphAdvances != nullptr ? run.glyphAdvances[i] : 0.0f);
    out->offsets.push_back(run.glyphOffsets != nullptr ? run.glyphOffsets[i] : DWRITE_GLYPH_OFFSET{0, 0});
  }
  out->layers.push_back(layer);
  return true;
}

}  // namespace

bool TextMeasurer::translate_color(uint16_t face, const uint16_t* glyphs, const float* advances,
                                   const DWRITE_GLYPH_OFFSET* offsets, uint32_t count, ColorRun* out) {
  *out = ColorRun();
  if (color_failed_ || count == 0 || face >= faces_.size() || !is_color_face(face)) return false;
  if (!color_factory_tried_) create_color_factory();
  if (fail_next_color_) {
    fail_next_color_ = false;
    color_failed_ = true;
    return false;
  }
  if (color_factory2_ == nullptr) {
    // No TranslateColorGlyphRun (DirectWrite before Windows 8.1).
    color_failed_ = true;
    return false;
  }
  IDWriteFontFace* font_face = faces_[face].face;
  const float em = faces_[face].em;
  DWRITE_GLYPH_RUN run{};
  run.fontFace = font_face;
  run.fontEmSize = em;
  run.glyphCount = count;
  run.glyphIndices = glyphs;
  run.glyphAdvances = advances;
  run.glyphOffsets = offsets;
  const D2D1_POINT_2F origin{0, 0};
  bool ok = true;
  HRESULT hr = S_OK;
  if (color_factory4_ != nullptr) {
    IDWriteColorGlyphRunEnumerator1* runs = nullptr;
    const bool paint_tree = color_factory8_ != nullptr && paint_level_ != 0 && (color_formats_ & kFormatPaintTree) != 0;
    if (paint_tree) {
      hr = color_factory8_->TranslateColorGlyphRun(origin, &run, nullptr,
                                                   static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(color_formats_),
                                                   static_cast<DWRITE_PAINT_FEATURE_LEVEL>(paint_level_),
                                                   DWRITE_MEASURING_MODE_NATURAL, nullptr, 0, &runs);
    } else {
      const auto formats = static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(color_formats_ & ~kFormatPaintTree);
      hr = color_factory4_->TranslateColorGlyphRun(origin, &run, nullptr, formats, DWRITE_MEASURING_MODE_NATURAL,
                                                   nullptr, 0, &runs);
    }
    if (SUCCEEDED(hr)) {
      BOOL more = FALSE;
      while (ok && SUCCEEDED(hr = runs->MoveNext(&more)) && more) {
        const DWRITE_COLOR_GLYPH_RUN1* layer = nullptr;
        hr = runs->GetCurrentRun(&layer);
        ok = SUCCEEDED(hr) && layer != nullptr &&
             append_layer(layer->glyphRun, static_cast<uint32_t>(layer->glyphImageFormat), layer->baselineOriginX,
                          layer->baselineOriginY,
                          layer->runColor, layer->paletteIndex, font_face, em, out);
      }
      runs->Release();
    }
  } else {
    IDWriteColorGlyphRunEnumerator* runs = nullptr;
    hr = color_factory2_->TranslateColorGlyphRun(0, 0, &run, nullptr, DWRITE_MEASURING_MODE_NATURAL, nullptr, 0,
                                                 &runs);
    if (SUCCEEDED(hr)) {
      BOOL more = FALSE;
      while (ok && SUCCEEDED(hr = runs->MoveNext(&more)) && more) {
        const DWRITE_COLOR_GLYPH_RUN* layer = nullptr;
        hr = runs->GetCurrentRun(&layer);
        ok = SUCCEEDED(hr) && layer != nullptr &&
             append_layer(layer->glyphRun, kFormatColr, layer->baselineOriginX,
                          layer->baselineOriginY, layer->runColor, layer->paletteIndex, font_face, em, out);
      }
      runs->Release();
    }
  }
  if (hr == DWRITE_E_NOCOLOR) {
    *out = ColorRun();
    return false;  // no colour for these glyphs: drawn as before
  }
  if (FAILED(hr) || !ok || out->layers.empty()) {
    *out = ColorRun();
    color_failed_ = true;  // decision D3: one colour from now on
    return false;
  }
  return true;
}

const TextMeasurer::ColorRun* TextMeasurer::color_glyph(uint16_t face, uint16_t index) {
  if (color_failed_ || face >= faces_.size() || !is_color_face(face)) return nullptr;
  const uint32_t key = (static_cast<uint32_t>(face) << 16) | index;
  auto it = color_glyphs_.find(key);
  if (it == color_glyphs_.end()) {
    ColorRun run;
    const float advance = advance_of(face, index);
    const DWRITE_GLYPH_OFFSET offset{0, 0};
    translate_color(face, &index, &advance, &offset, 1, &run);
    if (color_failed_) return nullptr;
    // A bound on the cache, as for clusters.
    if (color_glyphs_.size() >= 4096) color_glyphs_.clear();
    it = color_glyphs_.emplace(key, std::move(run)).first;
  }
  return it->second.layers.empty() ? nullptr : &it->second;
}

const TextMeasurer::ColorRun* TextMeasurer::color_cluster(const ClusterGlyphs& cluster) {
  if (color_failed_ || cluster.glyphs.empty() || !is_color_face(cluster.face)) return nullptr;
  if (cluster.color_state == 0) {
    const bool color =
        translate_color(cluster.face, cluster.glyphs.data(), cluster.advances.data(), cluster.offsets.data(),
                        static_cast<uint32_t>(cluster.glyphs.size()), &cluster.color);
    if (color_failed_) return nullptr;
    cluster.color_state = color ? 2 : 1;
  }
  return cluster.color_state == 2 ? &cluster.color : nullptr;
}

}  // namespace geditor::win
