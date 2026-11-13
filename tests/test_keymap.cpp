// Keymaps and the default M1 key table.

#include <string>

#include "commands.h"
#include "fake_host.h"
#include "keymap.h"

using geditor::Keymap;

namespace {

struct Expected {
  const wchar_t* keys;
  const char* command;
};

// Written independently of keymap.cpp: every key of PLAN 4.4 whose command
// is in M1, plus the non-conflicting Windows editing keys.
constexpr Expected kExpectedGlobal[] = {
    {L"C-f", "forward-char"},
    {L"C-b", "backward-char"},
    {L"C-n", "next-line"},
    {L"C-p", "previous-line"},
    {L"C-a", "beginning-of-line"},
    {L"C-e", "end-of-line"},
    {L"M-f", "forward-word"},
    {L"M-b", "backward-word"},
    {L"M-<", "beginning-of-buffer"},
    {L"M->", "end-of-buffer"},
    {L"C-v", "scroll-up-command"},
    {L"M-v", "scroll-down-command"},
    {L"C-l", "recenter-top-bottom"},
    {L"C-SPC", "set-mark-command"},
    {L"C-x C-x", "exchange-point-and-mark"},
    {L"C-x h", "mark-whole-buffer"},
    {L"C-g", "keyboard-quit"},
    {L"C-d", "delete-char"},
    {L"<delete>", "delete-char"},
    {L"DEL", "delete-backward-char"},
    {L"C-k", "kill-line"},
    {L"M-d", "kill-word"},
    {L"M-DEL", "backward-kill-word"},
    {L"C-w", "kill-region"},
    {L"M-w", "kill-ring-save"},
    {L"C-y", "yank"},
    {L"RET", "newline"},
    {L"C-j", "newline"},
    {L"C-o", "open-line"},
    {L"TAB", "insert-tab"},
    {L"C-q", "quoted-insert"},
    {L"C-t", "transpose-chars"},
    {L"M-u", "upcase-word"},
    {L"M-l", "downcase-word"},
    {L"M-c", "capitalize-word"},
    {L"C-/", "undo"},
    {L"C-_", "undo"},
    {L"C-x u", "undo"},
    {L"C-?", "redo"},
    {L"C-M-_", "redo"},
    {L"C-x C-f", "find-file"},
    {L"C-x C-s", "save-buffer"},
    {L"C-x C-w", "write-file"},
    {L"C-x C-q", "read-only-mode"},
    {L"C-x C-c", "quit"},
    // M2: the keys of PLAN 4.4 whose commands arrive in later milestones
    // (registered as not implemented), the prefix argument and M-x.
    {L"C-s", "isearch-forward"},
    {L"C-r", "isearch-backward"},
    {L"M-%", "query-replace"},
    {L"C-M-%", "query-replace-regexp"},
    {L"M-g g", "goto-line"},
    {L"M-g M-g", "goto-line"},
    {L"C-x RET f", "set-buffer-file-coding-system"},
    {L"C-x RET r", "revert-buffer-with-coding-system"},
    {L"C-x RET c", "find-file-with-encoding"},
    {L"C-x RET e", "set-eol"},
    {L"C-x C-+", "text-scale-increase"},
    {L"C-x C-=", "text-scale-increase"},
    {L"C-x C--", "text-scale-decrease"},
    {L"C-x C-0", "text-scale-reset"},
    {L"<f10>", "menu-bar-open"},
    {L"C-u", "universal-argument"},
    {L"M-0", "digit-argument"},
    {L"M-1", "digit-argument"},
    {L"M-2", "digit-argument"},
    {L"M-3", "digit-argument"},
    {L"M-4", "digit-argument"},
    {L"M-5", "digit-argument"},
    {L"M-6", "digit-argument"},
    {L"M-7", "digit-argument"},
    {L"M-8", "digit-argument"},
    {L"M-9", "digit-argument"},
    {L"M--", "negative-argument"},
    {L"C-x (", "kmacro-start-macro"},
    {L"<f3>", "kmacro-start-macro"},
    {L"C-x )", "kmacro-end-macro"},
    {L"C-x e", "kmacro-end-and-call-macro"},
    {L"<f4>", "kmacro-end-and-call-macro"},
    {L"M-x", "execute-extended-command"},
    {L"<left>", "backward-char"},
    {L"<right>", "forward-char"},
    {L"<up>", "previous-line"},
    {L"<down>", "next-line"},
    {L"<home>", "beginning-of-line"},
    {L"<end>", "end-of-line"},
    {L"<prior>", "scroll-down-command"},
    {L"<next>", "scroll-up-command"},
    {L"C-<left>", "backward-word"},
    {L"C-<right>", "forward-word"},
    {L"C-<home>", "beginning-of-buffer"},
    {L"C-<end>", "end-of-buffer"},
};

constexpr Expected kExpectedMinibuffer[] = {
    {L"RET", "exit-minibuffer"},
    {L"C-j", "exit-minibuffer"},
    {L"TAB", "minibuffer-complete"},
    {L"M-p", "previous-history-element"},
    {L"M-n", "next-history-element"},
};

}  // namespace

TEST(keymap_default_keys_resolve_to_commands) {
  const Keymap map = geditor::make_keymap(geditor::default_global_bindings());
  for (const Expected& e : kExpectedGlobal) {
    const auto keys = geditor::parse_key_sequence(e.keys);
    CHECK(keys.has_value());
    if (!keys) continue;
    const Keymap::Lookup r = map.lookup(*keys);
    if (r.result != Keymap::Result::kCommand || r.command != e.command) {
      std::printf("  key %s\n", test::to_display(e.keys).c_str());
    }
    CHECK(r.result == Keymap::Result::kCommand);
    CHECK_EQ(std::string(r.command), std::string(e.command));
  }
  // Same number of entries: nothing extra is bound by default.
  CHECK_EQ(geditor::default_global_bindings().size(), std::size(kExpectedGlobal));
}

TEST(keymap_minibuffer_keys_resolve_to_commands) {
  const Keymap map = geditor::make_keymap(geditor::default_minibuffer_bindings());
  for (const Expected& e : kExpectedMinibuffer) {
    const auto keys = geditor::parse_key_sequence(e.keys);
    CHECK(keys.has_value());
    if (!keys) continue;
    const Keymap::Lookup r = map.lookup(*keys);
    CHECK(r.result == Keymap::Result::kCommand);
    CHECK_EQ(std::string(r.command), std::string(e.command));
  }
}

TEST(keymap_default_bindings_name_registered_commands) {
  for (const auto& b : geditor::default_global_bindings()) {
    CHECK(geditor::find_command(b.command) != nullptr);
    CHECK(geditor::parse_key_sequence(b.keys).has_value());
  }
  for (const auto& b : geditor::default_minibuffer_bindings()) {
    CHECK(geditor::find_command(b.command) != nullptr);
  }
  CHECK(geditor::find_command("select-all") != nullptr);
  CHECK(geditor::find_command("copy") != nullptr);
  CHECK(geditor::find_command("cut") != nullptr);
  CHECK(geditor::find_command("paste") != nullptr);
}

// M2: M-g became a prefix (M-g g / M-g M-g goto-line of PLAN 4.4).
TEST(keymap_c_x_and_m_g_are_prefixes) {
  const Keymap map = geditor::make_keymap(geditor::default_global_bindings());
  CHECK(map.lookup(*geditor::parse_key_sequence(L"C-x")).result == Keymap::Result::kPrefix);
  CHECK(map.lookup(*geditor::parse_key_sequence(L"M-g")).result == Keymap::Result::kPrefix);
  CHECK(map.lookup(*geditor::parse_key_sequence(L"M-j")).result == Keymap::Result::kUndefined);
  CHECK(map.lookup(*geditor::parse_key_sequence(L"C-x C-z")).result == Keymap::Result::kUndefined);
}

TEST(keymap_holds_prefix_keys_and_rejects_conflicts) {
  Keymap map;
  CHECK(map.bind(L"C-c a b", "forward-char"));
  CHECK(map.lookup(*geditor::parse_key_sequence(L"C-c")).result == Keymap::Result::kPrefix);
  CHECK(map.lookup(*geditor::parse_key_sequence(L"C-c a")).result == Keymap::Result::kPrefix);
  CHECK(map.lookup(*geditor::parse_key_sequence(L"C-c a b")).result == Keymap::Result::kCommand);
  CHECK(!map.bind(L"C-c a", "backward-char"));       // command on a prefix
  CHECK(!map.bind(L"C-c a b c", "backward-char"));   // prefix through a command
  CHECK(!map.bind(L"C-", "backward-char"));          // bad notation
}

TEST(dispatch_undefined_key_shows_message_and_changes_nothing) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-x C-z");
  CHECK_EQ(s.message(), std::wstring(L"C-x C-z is undefined"));
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"M-j");  // M1 used M-g, which is a prefix since M2 (M-g g)
  CHECK_EQ(s.message(), std::wstring(L"M-j is undefined"));
  s.keys(L"<f5>");
  CHECK_EQ(s.message(), std::wstring(L"<f5> is undefined"));
  CHECK(!s.modified());
}

TEST(dispatch_pending_prefix_is_exposed_as_text) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-x");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-x-"));
  s.keys(L"C-g");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
  s.keys(L"ESC");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"ESC-"));
  s.keys(L"C-g");
  CHECK_EQ(s.message(), std::wstring(L"Quit"));
}

TEST(dispatch_esc_key_acts_as_meta) {
  test::Session s;
  s.load(L"one two");
  s.keys(L"ESC f");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"ESC DEL");
  CHECK_EQ(s.text(), std::wstring(L" two"));
  s.keys(L"ESC z");
  CHECK_EQ(s.message(), std::wstring(L"ESC z is undefined"));
}
