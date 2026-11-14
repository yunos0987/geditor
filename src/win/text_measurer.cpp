#include "text_measurer.h"

#include <windows.h>

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
  for (bool& known : ascii_known_) known = false;
  safe_release(fallback_);
  safe_release(collection_);
  fallback_tried_ = false;
  fallback_families_.clear();
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

}  // namespace geditor::win
