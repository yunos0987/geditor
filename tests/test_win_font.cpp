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
