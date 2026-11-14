// Fonts through real DirectWrite ([M3-3] / packet [C5], decision E1, [C9]):
// which family draws sample characters, missing family names, and the cell
// size of text-scale steps.

#include <windows.h>

#include <dwrite.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "display.h"
#include "test_harness.h"
#include "text_measurer.h"

using geditor::win::FontSpec;
using geditor::win::TextMeasurer;

namespace {

// The shared DirectWrite factory, released at the end of each test.
class Factory {
 public:
  Factory() {
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&factory_));
  }
  ~Factory() {
    if (factory_ != nullptr) factory_->Release();
  }
  Factory(const Factory&) = delete;
  Factory& operator=(const Factory&) = delete;
  IDWriteFactory* get() const { return factory_; }

 private:
  IDWriteFactory* factory_ = nullptr;
};

bool installed(IDWriteFactory* factory, const wchar_t* family) {
  IDWriteFontCollection* collection = nullptr;
  if (FAILED(factory->GetSystemFontCollection(&collection, FALSE))) return false;
  UINT32 index = 0;
  BOOL exists = FALSE;
  collection->FindFamilyName(family, &index, &exists);
  collection->Release();
  return exists != FALSE;
}

void report(const char* what, const std::wstring& family) {
  std::printf("  %s -> %s\n", what, test::to_display(family).c_str());
}

}  // namespace

TEST(win_font_ascii_is_consolas_and_japanese_uses_a_fallback) {
  Factory f;
  CHECK(f.get() != nullptr);
  if (f.get() == nullptr) return;
  TextMeasurer m;
  std::vector<std::wstring> missing;
  CHECK(m.init(f.get(), FontSpec{}, &missing));
  CHECK(missing.empty());
  const std::wstring a = m.family_of(U'A');
  const std::wstring hiragana = m.family_of(U'あ');
  const std::wstring kanji = m.family_of(U'漢');
  const std::wstring emoji = m.family_of(U'\U0001F600');
  report("A", a);
  report("U+3042", hiragana);
  report("U+6F22", kanji);
  report("U+1F600", emoji);
  CHECK_EQ(a, std::wstring(L"Consolas"));
  CHECK(!hiragana.empty() && hiragana != L"Consolas");
  CHECK(!kanji.empty() && kanji != L"Consolas");
  CHECK(!emoji.empty() && emoji != L"Consolas");
  CHECK(m.glyph(U'あ').index != 0);
  CHECK(m.glyph(U'漢').index != 0);
}

TEST(win_font_fallback_families_come_first) {
  Factory f;
  if (f.get() == nullptr) return;
  // Uses a Japanese font that ships with Windows 10/11.
  const wchar_t* japanese = installed(f.get(), L"BIZ UDGothic") ? L"BIZ UDGothic" : L"MS Gothic";
  if (!installed(f.get(), japanese)) {
    std::printf("  SKIP: no BIZ UDGothic / MS Gothic\n");
    return;
  }
  TextMeasurer m;
  FontSpec spec;
  spec.fallback = {japanese};
  std::vector<std::wstring> missing;
  CHECK(m.init(f.get(), spec, &missing));
  CHECK(missing.empty());
  report("U+3042 with fallback", m.family_of(U'あ'));
  CHECK_EQ(m.family_of(U'あ'), std::wstring(japanese));
  CHECK_EQ(m.family_of(U'A'), std::wstring(L"Consolas"));
  // A character the fallback family lacks goes on to the system fallback.
  const std::wstring emoji = m.family_of(U'\U0001F600');
  report("U+1F600 with fallback", emoji);
  CHECK(!emoji.empty());
  CHECK(m.glyph(U'\U0001F600').index != 0);
}

TEST(win_font_missing_family_names_are_reported) {
  // E1: the names are reported; Consolas and the system fallback are used.
  Factory f;
  if (f.get() == nullptr) return;
  TextMeasurer m;
  FontSpec spec;
  spec.family = L"No Such Font 123";
  spec.fallback = {L"Missing Fallback 456", L"Consolas"};
  std::vector<std::wstring> missing;
  CHECK(m.init(f.get(), spec, &missing));
  CHECK_EQ(missing.size(), size_t{2});
  if (missing.size() == 2) {
    CHECK_EQ(missing[0], std::wstring(L"No Such Font 123"));
    CHECK_EQ(missing[1], std::wstring(L"Missing Fallback 456"));
  }
  CHECK_EQ(std::wstring(m.family()), std::wstring(L"Consolas"));
  CHECK_EQ(m.family_of(U'A'), std::wstring(L"Consolas"));
  CHECK(!m.family_of(U'あ').empty());
}

TEST(win_font_other_family_and_size) {
  Factory f;
  if (f.get() == nullptr) return;
  if (!installed(f.get(), L"Courier New")) return;
  TextMeasurer m;
  FontSpec spec;
  spec.family = L"Courier New";
  spec.em = 20;
  CHECK(m.init(f.get(), spec, nullptr));
  CHECK_EQ(m.family_of(U'A'), std::wstring(L"Courier New"));
  CHECK(std::fabs(m.em() - 20.0f) < 1e-4f);
}

// ---- [M3c] Clusters of several code points (decision G6, [C8]) ----

TEST(win_font_clusters_are_shaped_with_one_font) {
  Factory f;
  if (f.get() == nullptr) return;
  TextMeasurer m;
  CHECK(m.init(f.get(), FontSpec{}, nullptr));
  // `emoji`: the core shows the cluster as an emoji (Cluster::emoji).
  const struct {
    const char* name;
    std::wstring text;
    bool emoji;
  } samples[] = {
      {"U+263A U+FE0F", L"\x263A\xFE0F", true},
      {"ZWJ family", L"\U0001F468\x200D\U0001F469\x200D\U0001F467", true},
      {"flag JP", L"\U0001F1EF\U0001F1F5", true},
      {"keycap #", L"#\xFE0F\x20E3", true},
      {"thumbs up + skin tone", L"\U0001F44D\U0001F3FD", true},
      {"U+304B U+3099", L"\x304B\x3099", false},
      {"e + U+0301", L"e\x0301", false},
      {"Hangul L V T", L"\x1100\x1161\x11A8", false},
      {"Devanagari KA VIRAMA SSA", L"\x0915\x094D\x0937", false},
  };
  for (const auto& s : samples) {
    const TextMeasurer::ClusterGlyphs& g = m.cluster(s.text, s.emoji);
    std::printf("  %s -> %s, %zu glyph(s), %.1f DIPs%s\n", s.name,
                test::to_display(m.family_of_cluster(s.text, s.emoji)).c_str(), g.glyphs.size(), g.width,
                g.shaped ? "" : " (fallback)");
    CHECK(g.shaped);
    CHECK(!g.glyphs.empty());
    CHECK_EQ(g.glyphs.size(), g.advances.size());
    CHECK_EQ(g.glyphs.size(), g.offsets.size());
    CHECK(g.width > 0);
    // Nothing is drawn as the missing-glyph box.
    bool box = false;
    for (const uint16_t index : g.glyphs) box = box || index == 0;
    CHECK(!box);
  }
  // An emoji cluster asks for the emoji font, though Consolas has U+263A as
  // text; "g" + U+FE0F is no emoji (the core says so) and stays in Consolas.
  CHECK(m.family_of_cluster(L"\x263A\xFE0F", true) != std::wstring(L"Consolas"));
  CHECK_EQ(m.family_of_cluster(L"e\x0301", false), std::wstring(L"Consolas"));
  CHECK_EQ(m.family_of_cluster(L"g\xFE0F", false), std::wstring(L"Consolas"));
}

TEST(win_font_cluster_without_glyphs_draws_the_missing_glyph_box) {
  // [C8]: no font has U+10FFFD (a private use character of plane 16): the
  // cluster is drawn as the missing-glyph box of the primary font, not as
  // nothing.
  Factory f;
  if (f.get() == nullptr) return;
  TextMeasurer m;
  CHECK(m.init(f.get(), FontSpec{}, nullptr));
  const TextMeasurer::ClusterGlyphs& g = m.cluster(L"\U0010FFFD\x0301", false);
  CHECK(!g.shaped);
  CHECK_EQ(g.glyphs.size(), size_t{1});
  if (!g.glyphs.empty()) CHECK_EQ(g.glyphs[0], uint16_t{0});
  CHECK(g.width > 0);
  CHECK_EQ(g.face, uint16_t{0});
}

TEST(win_font_cluster_shaping_failure_draws_the_first_character) {
  // [C8]: a failed shaping falls back to the glyph of the first code point.
  Factory f;
  if (f.get() == nullptr) return;
  TextMeasurer m;
  CHECK(m.init(f.get(), FontSpec{}, nullptr));
  m.fail_next_shaping();
  const TextMeasurer::ClusterGlyphs& g = m.cluster(L"e\x0301", false);
  CHECK(!g.shaped);
  CHECK_EQ(g.glyphs.size(), size_t{1});
  if (!g.glyphs.empty()) CHECK_EQ(g.glyphs[0], m.glyph(U'e').index);
  CHECK(g.glyphs.empty() || g.glyphs[0] != 0);
  CHECK(g.width > 0);
  // The next cluster is shaped again.
  CHECK(m.cluster(L"a\x0301", false).shaped);
}

TEST(win_font_text_scale_steps_change_the_cell_size) {
  // [C9]: each step is 1.2 times the size; Consolas cells scale with it.
  Factory f;
  if (f.get() == nullptr) return;
  TextMeasurer base;
  TextMeasurer scaled;
  FontSpec spec;
  spec.em = static_cast<float>(geditor::text_scale_pixels(12, 0, 96));
  CHECK(base.init(f.get(), spec, nullptr));
  spec.em = static_cast<float>(geditor::text_scale_pixels(12, 1, 96));
  CHECK(scaled.init(f.get(), spec, nullptr));
  const double ratio = scaled.cell_width() / base.cell_width();
  std::printf("  cell width %.3f -> %.3f (x%.4f), line height %.1f -> %.1f\n", base.cell_width(),
              scaled.cell_width(), ratio, base.line_height(), scaled.line_height());
  CHECK(std::fabs(ratio - 1.2) < 0.001);
  CHECK(scaled.line_height() > base.line_height());
}
