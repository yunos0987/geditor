// Key translation of the Win32 layer (src/win/input.*): virtual key +
// modifier state + the character the layout produces -> Key, for a US and a
// JIS layout (stubbed), following the notation table of docs/PLAN.md 4.2.

#include <windows.h>

#include <optional>

#include "input.h"
#include "key.h"
#include "test_harness.h"

using geditor::Key;
using geditor::win::CharAssembler;
using geditor::win::ModifierState;
using geditor::win::translate_key_down;

namespace {

struct LayoutRow {
  unsigned vk;
  char32_t plain;
  char32_t shifted;
};

// Only the keys the tests use. Letters are handled in code.
constexpr LayoutRow kUsRows[] = {
    {'1', U'1', U'!'},          {'2', U'2', U'@'},          {'5', U'5', U'%'},
    {'6', U'6', U'^'},          {'0', U'0', U')'},          {VK_SPACE, U' ', U' '},
    {VK_OEM_2, U'/', U'?'},     {VK_OEM_MINUS, U'-', U'_'}, {VK_OEM_PLUS, U'=', U'+'},
    {VK_OEM_COMMA, U',', U'<'}, {VK_OEM_PERIOD, U'.', U'>'}, {VK_OEM_7, U'\'', U'"'},
};

// JIS: '_' is Shift + the "ro" key (VK_OEM_102), Shift + '-' gives '=',
// Shift + '2' gives '"', '0' has no shifted character.
constexpr LayoutRow kJisRows[] = {
    {'1', U'1', U'!'},          {'2', U'2', U'"'},          {'5', U'5', U'%'},
    {'6', U'6', U'&'},          {'0', U'0', 0},             {VK_SPACE, U' ', U' '},
    {VK_OEM_2, U'/', U'?'},     {VK_OEM_MINUS, U'-', U'='}, {VK_OEM_PLUS, U';', U'+'},
    {VK_OEM_COMMA, U',', U'<'}, {VK_OEM_PERIOD, U'.', U'>'}, {VK_OEM_7, U'^', U'~'},
    {VK_OEM_102, U'\\', U'_'},
};

template <size_t N>
char32_t layout_from(const LayoutRow (&rows)[N], unsigned vk, bool shift) {
  if (vk >= 'A' && vk <= 'Z') return shift ? vk : vk - 'A' + U'a';
  for (const LayoutRow& r : rows) {
    if (r.vk == vk) return shift ? r.shifted : r.plain;
  }
  return 0;
}

char32_t us_layout(void*, unsigned vk, unsigned, bool shift) {
  return layout_from(kUsRows, vk, shift);
}

char32_t jis_layout(void*, unsigned vk, unsigned, bool shift) {
  return layout_from(kJisRows, vk, shift);
}

enum Mods : unsigned { kNone = 0, kC = 1, kM = 2, kS = 4 };

ModifierState mods_of(unsigned m) {
  ModifierState s;
  s.ctrl = (m & kC) != 0;
  s.alt = (m & kM) != 0;
  s.shift = (m & kS) != 0;
  return s;
}

std::optional<Key> us(unsigned vk, unsigned m) {
  return translate_key_down(vk, 0, mods_of(m), &us_layout, nullptr);
}

std::optional<Key> jis(unsigned vk, unsigned m) {
  return translate_key_down(vk, 0, mods_of(m), &jis_layout, nullptr);
}

std::wstring shown(const std::optional<Key>& k) {
  return k ? geditor::key_to_string(*k) : std::wstring(L"(none)");
}

// The translation equals the key that `notation` parses to.
bool is(const std::optional<Key>& k, const wchar_t* notation) {
  const std::optional<Key> expected = geditor::parse_key(notation);
  return k.has_value() && expected.has_value() && *k == *expected;
}

#define CHECK_KEY(actual, notation)            \
  do {                                         \
    CHECK(is((actual), (notation)));           \
    CHECK_EQ(shown(actual), std::wstring(notation)); \
  } while (0)

}  // namespace

// ---- PLAN 4.2 notation table rows ----

TEST(win_input_row_modifier_ctrl) {
  CHECK_KEY(us('X', kC), L"C-x");
  CHECK_KEY(jis('X', kC), L"C-x");
}

TEST(win_input_row_modifier_meta) {
  CHECK_KEY(us('F', kM), L"M-f");
  CHECK_KEY(jis('F', kM), L"M-f");
}

TEST(win_input_row_modifier_shift) {
  CHECK_KEY(us(VK_LEFT, kS), L"S-<left>");
  CHECK_KEY(jis(VK_LEFT, kS), L"S-<left>");
}

TEST(win_input_row_ctrl_meta_a) {
  CHECK_KEY(us('A', kC | kM), L"C-M-a");
  CHECK_KEY(jis('A', kC | kM), L"C-M-a");
}

TEST(win_input_row_ctrl_shift_a) {
  CHECK_KEY(us('A', kC | kS), L"C-S-a");
  CHECK_KEY(jis('A', kC | kS), L"C-S-a");
}

TEST(win_input_row_ctrl_question) {
  CHECK_KEY(us(VK_OEM_2, kC | kS), L"C-?");
  CHECK_KEY(jis(VK_OEM_2, kC | kS), L"C-?");
}

TEST(win_input_row_meta_less) {
  CHECK_KEY(us(VK_OEM_COMMA, kM | kS), L"M-<");
  CHECK_KEY(jis(VK_OEM_COMMA, kM | kS), L"M-<");
}

TEST(win_input_row_meta_percent) {
  CHECK_KEY(us('5', kM | kS), L"M-%");
  CHECK_KEY(jis('5', kM | kS), L"M-%");
}

TEST(win_input_ctrl_underscore_us_and_jis) {
  CHECK_KEY(us(VK_OEM_MINUS, kC | kS), L"C-_");
  // JIS: '_' is on the "ro" key; Shift + '-' is '='.
  CHECK_KEY(jis(VK_OEM_102, kC | kS), L"C-_");
  CHECK_KEY(jis(VK_OEM_MINUS, kC | kS), L"C-=");
}

TEST(win_input_ctrl_slash) {
  CHECK_KEY(us(VK_OEM_2, kC), L"C-/");
  CHECK_KEY(jis(VK_OEM_2, kC), L"C-/");
}

TEST(win_input_layout_dependent_symbols) {
  CHECK_KEY(us('2', kC | kS), L"C-@");
  CHECK_KEY(jis('2', kC | kS), L"C-\"");
  CHECK_KEY(us(VK_OEM_PLUS, kM), L"M-=");
  CHECK_KEY(jis(VK_OEM_PLUS, kM), L"M-;");
  // No character for Shift + '0' on JIS: not a key (goes on to Windows).
  CHECK(!jis('0', kC | kS).has_value());
}

TEST(win_input_row_ret_tab_esc_del) {
  for (auto* f : {&us, &jis}) {
    CHECK_KEY(f(VK_RETURN, kNone), L"RET");
    CHECK_KEY(f(VK_TAB, kNone), L"TAB");
    CHECK_KEY(f(VK_ESCAPE, kNone), L"ESC");
    CHECK_KEY(f(VK_BACK, kNone), L"DEL");
    CHECK_KEY(f(VK_BACK, kM), L"M-DEL");
  }
}

TEST(win_input_row_spc) {
  // Plain space arrives as WM_CHAR; with Ctrl it is C-SPC.
  CHECK(!us(VK_SPACE, kNone).has_value());
  CHECK_KEY(us(VK_SPACE, kC), L"C-SPC");
  CHECK_KEY(jis(VK_SPACE, kC), L"C-SPC");
  CharAssembler chars;
  CHECK_KEY(chars.feed(L' '), L"SPC");
}

TEST(win_input_row_editing_keys) {
  for (auto* f : {&us, &jis}) {
    CHECK_KEY(f(VK_DELETE, kNone), L"<delete>");
    CHECK_KEY(f(VK_INSERT, kNone), L"<insert>");
    CHECK_KEY(f(VK_HOME, kNone), L"<home>");
    CHECK_KEY(f(VK_END, kNone), L"<end>");
    CHECK_KEY(f(VK_PRIOR, kNone), L"<prior>");
    CHECK_KEY(f(VK_NEXT, kNone), L"<next>");
  }
}

TEST(win_input_row_arrows_and_function_keys) {
  for (auto* f : {&us, &jis}) {
    CHECK_KEY(f(VK_LEFT, kNone), L"<left>");
    CHECK_KEY(f(VK_RIGHT, kNone), L"<right>");
    CHECK_KEY(f(VK_UP, kNone), L"<up>");
    CHECK_KEY(f(VK_DOWN, kNone), L"<down>");
    CHECK_KEY(f(VK_F1, kNone), L"<f1>");
    CHECK_KEY(f(VK_F24, kNone), L"<f24>");
    CHECK_KEY(f(VK_F10, kNone), L"<f10>");
  }
}

TEST(win_input_row_key_sequence) {
  const std::optional<Key> first = us('X', kC);
  const std::optional<Key> second = us('S', kC);
  CHECK(first.has_value() && second.has_value());
  if (first && second) {
    CHECK_EQ(geditor::key_sequence_to_string({*first, *second}), std::wstring(L"C-x C-s"));
  }
}

// ---- Further keys of [C4] ----

TEST(win_input_shift_left) {
  CHECK_KEY(us(VK_LEFT, kS), L"S-<left>");
  CHECK_KEY(us(VK_RIGHT, kC | kS), L"C-S-<right>");
  CHECK_KEY(us(VK_END, kS), L"S-<end>");
}

TEST(win_input_meta_f4_passes_to_windows) {
  const std::optional<Key> k = us(VK_F4, kM);
  CHECK_KEY(k, L"M-<f4>");
  CHECK(k && geditor::win::passes_to_system_when_unbound(*k));
  const std::optional<Key> space = us(VK_SPACE, kM);
  CHECK_KEY(space, L"M-SPC");
  CHECK(space && geditor::win::passes_to_system_when_unbound(*space));
  CHECK(!geditor::win::passes_to_system_when_unbound(*us('F', kM)));
  CHECK(!geditor::win::passes_to_system_when_unbound(*us(VK_F4, kNone)));
}

TEST(win_input_esc) {
  CHECK_KEY(us(VK_ESCAPE, kNone), L"ESC");
  CharAssembler chars;
  CHECK_KEY(chars.feed(0x1B), L"ESC");
}

TEST(win_input_plain_keys_go_to_wm_char) {
  CHECK(!us('A', kNone).has_value());
  CHECK(!us('A', kS).has_value());
  CHECK(!us(VK_OEM_2, kS).has_value());
}

TEST(win_input_ignored_keys) {
  for (const int vk : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LSHIFT, VK_RMENU, VK_LWIN, VK_CAPITAL,
                       VK_PROCESSKEY, VK_PACKET, VK_KANJI, VK_CONVERT, VK_NONCONVERT, 0xF3,
                       0xF4}) {
    CHECK(!us(vk, kC).has_value());
    CHECK(!us(vk, kNone).has_value());
  }
}

// ---- WM_CHAR ----

TEST(win_input_char_ascii) {
  CharAssembler chars;
  CHECK_KEY(chars.feed(L'a'), L"a");
  CHECK_KEY(chars.feed(L'A'), L"A");
  CHECK_KEY(chars.feed(L'?'), L"?");
}

TEST(win_input_char_japanese) {
  CharAssembler chars;
  const std::optional<Key> k = chars.feed(L'日');
  CHECK(k.has_value());
  if (k) {
    CHECK_EQ(static_cast<uint32_t>(k->code), 0x65E5u);
    CHECK_EQ(static_cast<int>(k->mods), 0);
  }
}

TEST(win_input_char_surrogate_pair) {
  CharAssembler chars;
  CHECK(!chars.feed(static_cast<wchar_t>(0xD83D)).has_value());
  const std::optional<Key> k = chars.feed(static_cast<wchar_t>(0xDE00));
  CHECK(k.has_value());
  if (k) {
    CHECK_EQ(static_cast<uint32_t>(k->code), 0x1F600u);
    CHECK(geditor::is_self_insert_key(*k));
  }
}

TEST(win_input_char_lone_surrogates_dropped) {
  CharAssembler chars;
  CHECK(!chars.feed(static_cast<wchar_t>(0xDE00)).has_value());
  CHECK(!chars.feed(static_cast<wchar_t>(0xD83D)).has_value());
  CHECK_KEY(chars.feed(L'b'), L"b");  // the unpaired high surrogate is dropped
}

TEST(win_input_char_controls) {
  CharAssembler chars;
  CHECK_KEY(chars.feed(L'\r'), L"RET");
  CHECK_KEY(chars.feed(L'\t'), L"TAB");
  CHECK_KEY(chars.feed(L'\b'), L"DEL");
  CHECK(!chars.feed(0x01).has_value());  // Ctrl+A as WM_CHAR
  CHECK(!chars.feed(0x7F).has_value());  // Ctrl+Backspace
}
