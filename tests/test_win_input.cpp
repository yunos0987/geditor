// Key translation of the Win32 layer (src/win/input.*): virtual key +
// modifier state + the character the layout produces -> Key, for a US and a
// JIS layout (stubbed), following the notation table of docs/PLAN.md 4.2.

#include <windows.h>

#include <optional>
#include <vector>

#include "headless.h"
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
    {'8', U'8', U'*'},          {'9', U'9', U'('},
};

// JIS: '_' is Shift + the "ro" key (VK_OEM_102), Shift + '-' gives '=',
// Shift + '2' gives '"', '0' has no shifted character.
constexpr LayoutRow kJisRows[] = {
    {'1', U'1', U'!'},          {'2', U'2', U'"'},          {'5', U'5', U'%'},
    {'6', U'6', U'&'},          {'0', U'0', 0},             {VK_SPACE, U' ', U' '},
    {VK_OEM_2, U'/', U'?'},     {VK_OEM_MINUS, U'-', U'='}, {VK_OEM_PLUS, U';', U'+'},
    {VK_OEM_COMMA, U',', U'<'}, {VK_OEM_PERIOD, U'.', U'>'}, {VK_OEM_7, U'^', U'~'},
    {VK_OEM_102, U'\\', U'_'},  {'8', U'8', U'('},          {'9', U'9', U')'},
    {VK_OEM_3, U'@', U'`'},
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

// ---- C-h and DEL (2026-11-17), C-M-s / C-M-r ----

namespace {

// The keys one key press gives, as the message loop runs it
// (MainWindow::pre_translate, the macro holding of quit_requested): the
// key down goes to translate_key_down first; a key it returns is handled
// and the message never reaches TranslateMessage, so no WM_CHAR is made
// for it. Only a key down that gives no key goes on to TranslateMessage,
// whose WM_CHAR `translated_char` (the control character Windows makes)
// goes to CharAssembler.
std::vector<Key> keys_of_press(unsigned vk, ModifierState mods, wchar_t translated_char) {
  std::vector<Key> keys;
  if (const std::optional<Key> k = translate_key_down(vk, 0, mods, &us_layout, nullptr)) {
    keys.push_back(*k);
    return keys;  // consumed: no TranslateMessage, no WM_CHAR
  }
  CharAssembler chars;
  if (const std::optional<Key> k = chars.feed(translated_char)) keys.push_back(*k);
  return keys;
}

}  // namespace

TEST(win_input_ctrl_h_is_one_c_h_and_backspace_one_del) {
  // Ctrl+H: Windows's TranslateMessage would make WM_CHAR 0x08 (the same
  // character as Backspace), but the key down is already C-h.
  const std::vector<Key> ctrl_h = keys_of_press('H', mods_of(kC), L'\b');
  CHECK_EQ(ctrl_h.size(), size_t{1});
  if (!ctrl_h.empty()) CHECK_EQ(shown(ctrl_h[0]), std::wstring(L"C-h"));
  // Backspace: DEL from the key down (named key), no WM_CHAR either.
  const std::vector<Key> backspace = keys_of_press(VK_BACK, mods_of(kNone), L'\b');
  CHECK_EQ(backspace.size(), size_t{1});
  if (!backspace.empty()) CHECK_EQ(shown(backspace[0]), std::wstring(L"DEL"));
  // Headless: the modifiers come from the lParam bits of the posted key.
  const std::vector<Key> headless_ctrl_h =
      keys_of_press('H', geditor::win::headless_key_modifiers(geditor::win::kHeadlessKeyCtrl), L'\b');
  CHECK_EQ(headless_ctrl_h.size(), size_t{1});
  if (!headless_ctrl_h.empty()) CHECK_EQ(shown(headless_ctrl_h[0]), std::wstring(L"C-h"));
  CHECK(!(ctrl_h.empty() || backspace.empty()) && !(ctrl_h[0] == backspace[0]));
  // A WM_CHAR 0x08 that does arrive on its own (posted, or from a key down
  // that gave no key) is DEL, as before.
  CharAssembler chars;
  CHECK_KEY(chars.feed(L'\b'), L"DEL");
}

TEST(win_input_ctrl_alt_s_and_r_are_c_m_s_and_c_m_r) {
  CHECK_KEY(us('S', kC | kM), L"C-M-s");
  CHECK_KEY(us('R', kC | kM), L"C-M-r");
  CHECK_KEY(jis('S', kC | kM), L"C-M-s");
  const ModifierState headless =
      geditor::win::headless_key_modifiers(geditor::win::kHeadlessKeyCtrl | geditor::win::kHeadlessKeyAlt);
  CHECK_KEY(translate_key_down('S', 0, headless, &us_layout, nullptr), L"C-M-s");
  CHECK_KEY(translate_key_down('R', 0, headless, &us_layout, nullptr), L"C-M-r");
  // C-u (Ctrl+U) too: a key of its own.
  CHECK_KEY(us('U', kC), L"C-u");
}

// 2026-11-18: the macro keys C-( / C-) / C-S-e. "(" and ")" are the
// characters that Shift gives, on the keys the layout has them on; the Key
// has no S- (key.cpp does not parse "C-S-8").
TEST(win_input_ctrl_parens_and_ctrl_shift_e) {
  CHECK_KEY(jis('8', kC | kS), L"C-(");
  CHECK_KEY(jis('9', kC | kS), L"C-)");
  CHECK_KEY(us('9', kC | kS), L"C-(");
  CHECK_KEY(us('0', kC | kS), L"C-)");
  CHECK_KEY(us('E', kC | kS), L"C-S-e");
  CHECK_KEY(jis('E', kC | kS), L"C-S-e");
  // Without Shift the digit keys are C-8 / C-9, not the macro keys.
  CHECK_KEY(jis('8', kC), L"C-8");
  CHECK_KEY(jis('9', kC), L"C-9");
  CHECK_KEY(us('9', kC), L"C-9");
  CHECK_KEY(us('8', kC | kS), L"C-*");
  // Headless: the modifiers of the lParam bits give the same keys.
  const ModifierState headless =
      geditor::win::headless_key_modifiers(geditor::win::kHeadlessKeyCtrl | geditor::win::kHeadlessKeyShift);
  CHECK_KEY(translate_key_down('8', 0, headless, &jis_layout, nullptr), L"C-(");
  CHECK_KEY(translate_key_down('9', 0, headless, &jis_layout, nullptr), L"C-)");
  CHECK_KEY(translate_key_down('E', 0, headless, &jis_layout, nullptr), L"C-S-e");
}

// 2026-11-19: Emacs's default keys S-<insert> / C-<insert> / S-<delete> /
// C-<delete> / C-DEL / M-<left> / M-<right> / C-@ / C-<digit> / C-M-<digit>
// / C-- / C-M--. Named keys keep Shift; "@" is the character the layout
// gives (JIS: its own key, US: Shift + 2).
TEST(win_input_emacs_default_keys_of_2026_11_19) {
  for (auto* f : {&us, &jis}) {
    CHECK_KEY(f(VK_INSERT, kS), L"S-<insert>");
    CHECK_KEY(f(VK_INSERT, kC), L"C-<insert>");
    CHECK_KEY(f(VK_DELETE, kS), L"S-<delete>");
    CHECK_KEY(f(VK_DELETE, kC), L"C-<delete>");
    CHECK_KEY(f(VK_BACK, kC), L"C-DEL");
    CHECK_KEY(f(VK_LEFT, kM), L"M-<left>");
    CHECK_KEY(f(VK_RIGHT, kM), L"M-<right>");
    CHECK_KEY(f('0', kC), L"C-0");
    CHECK_KEY(f('5', kC | kM), L"C-M-5");
    CHECK_KEY(f(VK_OEM_MINUS, kC), L"C--");
    CHECK_KEY(f(VK_OEM_MINUS, kC | kM), L"C-M--");
  }
  CHECK_KEY(jis(VK_OEM_3, kC), L"C-@");
  CHECK_KEY(us('2', kC | kS), L"C-@");
  // Ctrl+Backspace is one C-DEL: the key down is consumed, no WM_CHAR 0x7F.
  const std::vector<Key> ctrl_back = keys_of_press(VK_BACK, mods_of(kC), 0x7F);
  CHECK_EQ(ctrl_back.size(), size_t{1});
  if (!ctrl_back.empty()) CHECK_EQ(shown(ctrl_back[0]), std::wstring(L"C-DEL"));
  // Headless: the modifiers of the lParam bits give the same keys.
  const ModifierState shift = geditor::win::headless_key_modifiers(geditor::win::kHeadlessKeyShift);
  CHECK_KEY(translate_key_down(VK_INSERT, 0, shift, &jis_layout, nullptr), L"S-<insert>");
  CHECK_KEY(translate_key_down(VK_DELETE, 0, shift, &jis_layout, nullptr), L"S-<delete>");
  const ModifierState alt = geditor::win::headless_key_modifiers(geditor::win::kHeadlessKeyAlt);
  CHECK_KEY(translate_key_down(VK_LEFT, 0, alt, &jis_layout, nullptr), L"M-<left>");
}
