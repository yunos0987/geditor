// Settings ([M2-3], [M2-4], [M2-5] core part, [M2-7]): the four binding
// forms, unbinding, prefix conflicts, semantic and syntax errors with line
// and column, the Windows compatible layer, reloading, the multi-line echo
// area and find-user-init-file.

#include <cstdio>
#include <string>
#include <vector>

#include "config.h"
#include "fake_host.h"
#include "keymap.h"

using geditor::Diagnostic;
using geditor::Keymap;
using geditor::KeymapKind;
using geditor::TextPos;

namespace {

Keymap::Lookup global_lookup(const test::Session& s, const wchar_t* keys) {
  return s.ed.keymap(KeymapKind::kGlobal).lookup(*geditor::parse_key_sequence(keys));
}

std::string command_of(const test::Session& s, const wchar_t* keys) {
  const Keymap::Lookup r = global_lookup(s, keys);
  return r.result == Keymap::Result::kCommand ? std::string(r.command) : std::string();
}

// True when the settings errors include one at line:column containing
// `fragment`.
bool has_error(const test::Session& s, int line, int column, const wchar_t* fragment) {
  for (const Diagnostic& d : s.ed.settings_errors()) {
    if (d.pos.line == line && d.pos.column == column && d.message.find(fragment) != std::wstring::npos) {
      return true;
    }
  }
  std::printf("  no error %d:%d %s; errors:\n", line, column, test::to_display(fragment).c_str());
  for (const Diagnostic& d : s.ed.settings_errors()) {
    std::printf("    %d:%d %s\n", d.pos.line, d.pos.column, test::to_display(d.message).c_str());
  }
  return false;
}

// Settings text with `n` unknown items, one per line (line i + 2 for i >= 0).
std::string unknown_items(int n) {
  std::string text = "{\n";
  for (int i = 0; i < n; ++i) text += "  \"unknown_" + std::to_string(i) + "\": 1,\n";
  return text + "}\n";
}

}  // namespace

// ---- [M2-3] the four value forms ----

TEST(settings_form_command_name) {
  test::Session s;
  s.load(L"abc def");
  s.settings("{\"keys\": {\"C-c a\": \"end-of-line\"}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-c a");
  CHECK_EQ(s.point(), size_t{7});
}

TEST(settings_form_command_with_arguments) {
  test::Session s;
  s.load(L"x");
  s.settings("{\"keys\": {\"C-c d\": [\"insert-text\", \"2026-11-10\"]}}");
  s.keys(L"C-c d");
  CHECK_EQ(s.text(), std::wstring(L"2026-11-10x"));
  CHECK_EQ(s.point(), size_t{10});
}

TEST(settings_form_command_sequence) {
  test::Session s;
  s.load(L"one\ntwo\n");
  s.set_point(2);
  s.settings(
      "{\"keys\": {\"C-c l\": [[\"beginning-of-line\"],\n"
      "                       [\"insert-text\", \"// \"],\n"
      "                       [\"next-line\"]]}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-c l");
  CHECK_EQ(s.text(), std::wstring(L"// one\ntwo\n"));
  CHECK_EQ(s.ed.point_line(), size_t{1});
  s.keys(L"C-c l");
  CHECK_EQ(s.text(), std::wstring(L"// one\n// two\n"));
  s.keys(L"C-/");  // one undo group per run
  CHECK_EQ(s.text(), std::wstring(L"// one\ntwo\n"));
}

TEST(settings_form_null_on_emacs_default_key) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-f\": null}}");
  CHECK(global_lookup(s, L"C-f").result == Keymap::Result::kUnbound);
  s.keys(L"C-f");
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"C-f is undefined"));
  s.keys(L"<right>");  // other keys of the same command still work
  CHECK_EQ(s.point(), size_t{1});
}

TEST(settings_form_null_on_windows_compat_key) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-M-a\": null}}");
  s.keys(L"C-M-a");
  // Not select-all, and nothing from a lower layer either.
  CHECK_EQ(s.message(), std::wstring(L"C-M-a is undefined"));
  CHECK(!s.ed.buffer().mark_active);
  CHECK_EQ(s.point(), size_t{0});
}

TEST(settings_form_null_on_plain_character_key) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"q\": null}}");
  s.type(L"q");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  CHECK_EQ(s.message(), std::wstring(L"q is undefined"));
  s.type(L"w");
  CHECK_EQ(s.text(), std::wstring(L"wabc"));
}

TEST(settings_null_on_shifted_key_blocks_shift_translation) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"S-<right>\": null}}");
  s.keys(L"S-<right>");
  CHECK_EQ(s.point(), size_t{0});
  CHECK_EQ(s.message(), std::wstring(L"S-<right> is undefined"));
}

TEST(settings_null_on_prefix_key_unbinds_the_whole_prefix) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-x\": null, \"C-x b\": \"end-of-line\"}}");
  CHECK(s.ed.settings_errors().empty());
  // The later item makes C-x a prefix again, holding only C-x b.
  s.keys(L"C-x b");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"C-x C-s");
  CHECK_EQ(s.message(), std::wstring(L"C-x C-s is undefined"));
}

// ---- [M2-3] semantic errors: item ignored, rest applied, line and column ----

TEST(settings_prefix_conflict_command_on_prefix_key) {
  test::Session s;
  s.load(L"abc");
  s.settings(
      "{\n"
      "  \"keys\": {\n"
      "    \"C-x\": \"forward-char\",\n"
      "    \"C-c a\": \"end-of-line\"\n"
      "  }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  CHECK(has_error(s, 3, 5, L"\"C-x\" is a prefix key"));
  CHECK(global_lookup(s, L"C-x").result == Keymap::Result::kPrefix);
  CHECK_EQ(command_of(s, L"C-x C-s"), std::string("save-buffer"));
  s.keys(L"C-c a");  // the rest of the file applies
  CHECK_EQ(s.point(), size_t{3});
}

TEST(settings_prefix_conflict_prefix_through_command) {
  test::Session s;
  s.load(L"abc");
  s.settings(
      "{\n"
      "  \"keys\": {\n"
      "    \"C-c a\": \"end-of-line\",\n"
      "    \"C-f a\": \"beginning-of-line\"\n"
      "  }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  CHECK(has_error(s, 4, 5, L"\"C-f\" runs a command"));
  CHECK_EQ(command_of(s, L"C-f"), std::string("forward-char"));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("end-of-line"));
}

TEST(settings_prefix_conflict_inside_the_file_itself) {
  test::Session s;
  s.settings("{\"keys\": {\"C-c a\": \"end-of-line\", \"C-c a b\": \"undo\", \"C-c\": \"undo\"}}");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{2});
  CHECK(has_error(s, 1, 35, L"\"C-c a\" runs a command"));
  CHECK(has_error(s, 1, 54, L"\"C-c\" is a prefix key"));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("end-of-line"));
}

TEST(settings_unknown_command) {
  test::Session s;
  s.load(L"abc");
  s.settings(
      "{\n"
      "  \"keys\": {\n"
      "    \"C-c a\": \"no-such-command\",\n"
      "    \"C-c b\": [\"also-missing\", 1],\n"
      "    \"C-c c\": [[\"end-of-line\"], [\"missing-too\"]],\n"
      "    \"C-c e\": \"end-of-line\"\n"
      "  }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{3});
  CHECK(has_error(s, 3, 14, L"Unknown command \"no-such-command\""));
  CHECK(has_error(s, 4, 15, L"Unknown command \"also-missing\""));
  CHECK(has_error(s, 5, 33, L"Unknown command \"missing-too\""));
  CHECK(global_lookup(s, L"C-c a").result == Keymap::Result::kUndefined);
  CHECK(global_lookup(s, L"C-c c").result == Keymap::Result::kUndefined);
  CHECK_EQ(command_of(s, L"C-c e"), std::string("end-of-line"));
}

TEST(settings_wrong_argument_type_and_count) {
  test::Session s;
  s.settings(
      "{\n"
      "  \"keys\": {\n"
      "    \"C-c a\": [\"insert-text\", 5],\n"
      "    \"C-c b\": [\"insert-text\", \"a\", \"b\"],\n"
      "    \"C-c c\": [\"forward-char\", \"x\"],\n"
      "    \"C-c d\": [\"set-eol\", \"unix\"],\n"
      "    \"C-c e\": [[\"insert-text\"]],\n"
      "    \"C-c f\": [\"set-eol\", \"crlf\"]\n"
      "  }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{5});
  CHECK(has_error(s, 3, 30, L"Argument 1 of \"insert-text\" must be a string"));
  CHECK(has_error(s, 4, 14, L"\"insert-text\" takes 1 argument, not 2"));
  CHECK(has_error(s, 5, 14, L"\"forward-char\" takes no arguments, not 1"));
  CHECK(has_error(s, 6, 26, L"must be one of lf, crlf, cr"));
  CHECK(has_error(s, 7, 15, L"needs 1 argument in a command sequence"));
  CHECK_EQ(command_of(s, L"C-c f"), std::string("set-eol"));
}

TEST(settings_invalid_key_notation_and_binding_value) {
  test::Session s;
  s.settings(
      "{\n"
      "  \"keys\": {\n"
      "    \"C-\": \"undo\",\n"
      "    \"C-x  C-s\": \"undo\",\n"
      "    \"C-c a\": 42,\n"
      "    \"C-c b\": [],\n"
      "    \"C-c c\": [[\"undo\"], \"redo\"]\n"
      "  }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{5});
  CHECK(has_error(s, 3, 5, L"Invalid key \"C-\""));
  CHECK(has_error(s, 4, 5, L"Invalid key \"C-x  C-s\""));
  CHECK(has_error(s, 5, 14, L"Invalid binding"));
  CHECK(has_error(s, 6, 14, L"Empty binding"));
  CHECK(has_error(s, 7, 25, L"Each command of a sequence must be an array"));
}

TEST(settings_unknown_setting_items) {
  test::Session s;
  s.settings(
      "{\n"
      "  \"tab_widht\": 4,\n"
      "  \"font\": { \"family\": \"Consolas\", \"weight\": 700 },\n"
      "  \"keys\": { \"C-c a\": \"undo\" }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{2});
  CHECK(has_error(s, 2, 3, L"Unknown setting \"tab_widht\""));
  CHECK(has_error(s, 3, 35, L"Unknown setting \"font.weight\""));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("undo"));
}

TEST(settings_invalid_colour) {
  test::Session s;
  s.settings(
      "{\n"
      "  \"colors\": { \"cursor\": \"#12345g\", \"selection\": \"#ABCDEF\", \"eol\": \"red\" }\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{2});
  CHECK(has_error(s, 2, 25, L"Invalid colour for \"colors.cursor\""));
  CHECK(has_error(s, 2, 67, L"Invalid colour for \"colors.eol\""));
  CHECK_EQ(s.ed.config().colors[geditor::kColorSelection], 0xABCDEFu);
  CHECK_EQ(s.ed.config().colors[geditor::kColorCursor], 0xffffffu);  // default kept
}

TEST(settings_duplicate_key_later_wins) {
  test::Session s;
  s.load(L"abc");
  s.settings(
      "{\n"
      "  \"keys\": {\n"
      "    \"C-c a\": \"end-of-line\",\n"
      "    \"C-c a\": \"forward-char\",\n"
      "    \"C-M-q\": \"undo\",\n"
      "    \"M-C-q\": \"redo\"\n"
      "  },\n"
      "  \"tab_width\": 2,\n"
      "  \"tab_width\": 8\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{3});
  CHECK(has_error(s, 3, 5, L"Duplicate key \"C-c a\""));
  CHECK(has_error(s, 5, 5, L"Duplicate key \"C-M-q\""));  // same key as M-C-q
  CHECK(has_error(s, 8, 3, L"Duplicate item \"tab_width\""));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("forward-char"));
  CHECK_EQ(command_of(s, L"C-M-q"), std::string("redo"));
  CHECK_EQ(s.ed.config().tab_width, 8);
}

TEST(settings_other_items_are_validated_and_kept) {
  test::Session s;
  s.settings(
      "{\n"
      "  \"font\": { \"family\": \"BIZ UDGothic\", \"size\": 10.5, \"fallback\": [\"Segoe UI Emoji\"] },\n"
      "  \"tab_width\": 0,\n"
      "  \"line_numbers\": \"yes\",\n"
      "  \"menu_bar\": false,\n"
      "  \"wrap\": { \"mode\": \"fixed\", \"column\": 100 },\n"
      "  \"whitespace\": { \"space\": true, \"tab\": 1 },\n"
      "  \"eol_marks\": { \"show\": false, \"lf\": \"ab\", \"cr\": \"\xE2\x86\x90\" },\n"
      "  \"new_file\": { \"encoding\": \"shift_jis\", \"eol\": \"unix\" },\n"
      "  \"backup\": false,\n"
      "  \"search\": { \"case_fold\": \"never\" }\n"
      "}\n");
  const geditor::Config& c = s.ed.config();
  CHECK_EQ(s.ed.settings_errors().size(), size_t{5});
  CHECK(has_error(s, 3, 16, L"\"tab_width\" must be a whole number from 1 to 32"));
  CHECK(has_error(s, 4, 19, L"\"line_numbers\" must be true or false"));
  CHECK(has_error(s, 7, 41, L"\"whitespace.tab\" must be true or false"));
  CHECK(has_error(s, 8, 39, L"\"eol_marks.lf\" must be one character"));
  CHECK(has_error(s, 9, 49, L"\"new_file.eol\" must be one of lf, crlf, cr"));
  CHECK_EQ(c.font_family, std::wstring(L"BIZ UDGothic"));
  CHECK(c.font_size == 10.5);
  CHECK_EQ(c.font_fallback.size(), size_t{1});
  CHECK_EQ(c.tab_width, 4);
  CHECK(c.line_numbers);
  CHECK(!c.menu_bar);
  CHECK(c.wrap_mode == geditor::WrapMode::kFixed);
  CHECK_EQ(c.wrap_column, 100);
  CHECK(c.whitespace_space);
  CHECK(c.whitespace_tab);
  CHECK(!c.eol_marks_show);
  CHECK_EQ(c.eol_mark_lf, std::wstring(L"\u2193"));
  CHECK_EQ(c.eol_mark_cr, std::wstring(L"\u2190"));
  CHECK_EQ(c.new_file_encoding, std::wstring(L"shift_jis"));
  CHECK_EQ(c.new_file_eol, std::wstring(L"lf"));
  CHECK(!c.backup);
  CHECK(c.case_fold == geditor::CaseFold::kNever);
  // M2 applies only the keys: the editor's tab width is unchanged.
  CHECK_EQ(s.ed.tab_width(), 4);
}

TEST(settings_syntax_error_rejects_the_whole_file) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-c a\": \"end-of-line\"}, \"tab_width\": 2}");  // A
  CHECK(s.ed.settings_errors().empty());
  // B: a missing comma on line 3, and a binding that must not apply.
  s.settings(
      "{\n"
      "  \"keys\": {\"C-c b\": \"end-of-line\"}\n"
      "  \"tab_width\": 8\n"
      "}\n");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  CHECK(has_error(s, 3, 3, L"Expected ',' or '}'"));
  CHECK(s.message().find(L".geditor.json:3:3: Expected ',' or '}'") == 0);
  CHECK_EQ(command_of(s, L"C-c a"), std::string("end-of-line"));  // A is still active
  CHECK(global_lookup(s, L"C-c b").result == Keymap::Result::kUndefined);
  CHECK_EQ(s.ed.config().tab_width, 2);
  s.keys(L"C-c a");
  CHECK_EQ(s.point(), size_t{3});
}

TEST(settings_minibuffer_keys_apply_while_reading) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"minibuffer_keys\": {\"C-c c\": \"minibuffer-complete\", \"TAB\": null}}");
  CHECK(s.ed.settings_errors().empty());
  s.keys(L"C-x C-f");
  s.keys(L"TAB");  // unbound in the minibuffer: no completion, no insert-tab
  CHECK_EQ(s.message(), std::wstring(L"TAB is undefined"));
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\"));
  s.keys(L"C-g");
  s.keys(L"TAB");  // global TAB is unchanged
  CHECK_EQ(s.text(), std::wstring(L"\tabc"));
}

// ---- [M2-4] Windows compatible keys (PLAN 4.4 table: 11 rows, 12 keys) ----

namespace {

struct CompatRow {
  const wchar_t* key;
  const char* command;
  const char* replaced_emacs;  // the Emacs operation of the same key
};

// Written from the PLAN 4.4 table, independently of keymap.cpp.
constexpr CompatRow kCompatRows[] = {
    {L"C-M-a", "select-all", "beginning-of-defun"},
    {L"C-M-c", "copy", "exit-recursive-edit"},
    {L"C-M-x", "cut", "eval-defun"},
    {L"C-M-v", "paste", "scroll-other-window"},
    {L"C-M-z", "undo", "(none)"},
    {L"C-M-y", "redo", "(none)"},
    {L"C-M-s", "save-buffer", "isearch-forward-regexp"},
    {L"C-M-w", "quit", "append-next-kill"},
    {L"C-M-f", "isearch-forward", "forward-sexp"},
    {L"C-M-h", "query-replace", "mark-defun"},
    {L"C-M-r", "query-replace", "isearch-backward-regexp"},
    {L"C-M-g", "goto-line", "(none)"},
};

}  // namespace

TEST(compat_table_has_twelve_keys) {
  CHECK_EQ(std::size(kCompatRows), size_t{12});
  CHECK_EQ(geditor::default_compat_bindings().size(), size_t{12});
}

TEST(compat_keys_resolve_by_default) {
  test::Session s;
  for (const CompatRow& row : kCompatRows) {
    if (command_of(s, row.key) != row.command) std::printf("  key %s\n", test::to_display(row.key).c_str());
    CHECK_EQ(command_of(s, row.key), std::string(row.command));
  }
}

TEST(compat_layer_overrides_the_emacs_layer) {
  // An Emacs layer that binds every compat key to its Emacs operation.
  std::vector<geditor::DefaultBinding> emacs;
  for (const CompatRow& row : kCompatRows) emacs.push_back({row.key, row.replaced_emacs});
  const Keymap map = geditor::build_keymap(emacs, geditor::default_compat_bindings(), {}, nullptr);
  for (const CompatRow& row : kCompatRows) {
    const Keymap::Lookup r = map.lookup(*geditor::parse_key_sequence(row.key));
    CHECK(r.result == Keymap::Result::kCommand);
    CHECK_EQ(std::string(r.command), std::string(row.command));
  }
  // Without the compat layer the Emacs operation would run.
  const Keymap emacs_only = geditor::build_keymap(emacs, {}, {}, nullptr);
  CHECK_EQ(std::string(emacs_only.lookup(*geditor::parse_key_sequence(L"C-M-a")).command),
           std::string("beginning-of-defun"));
}

TEST(compat_layer_is_overridden_by_settings) {
  test::Session s;
  std::string text = "{\"keys\": {";
  for (const CompatRow& row : kCompatRows) {
    text += "\"" + test::utf8(row.key) + "\": \"end-of-line\",";
  }
  text += "}}";
  s.settings(text);
  CHECK(s.ed.settings_errors().empty());
  for (const CompatRow& row : kCompatRows) {
    CHECK_EQ(command_of(s, row.key), std::string("end-of-line"));
  }
}

// ---- [M2-5] reload (core) ----

TEST(settings_new_text_changes_bindings) {
  test::Session s;
  s.load(L"abc");
  s.settings("{}");
  CHECK(global_lookup(s, L"C-c a").result == Keymap::Result::kUndefined);
  s.settings("{\"keys\": {\"C-c a\": \"end-of-line\"}}");
  s.keys(L"C-c a");
  CHECK_EQ(s.point(), size_t{3});
  s.settings("{\"keys\": {\"C-c a\": \"beginning-of-line\"}}");
  s.keys(L"C-c a");
  CHECK_EQ(s.point(), size_t{0});
}

TEST(settings_missing_file_at_startup_uses_defaults_quietly) {
  test::Session s;
  s.ed.set_settings_path(test::kSettingsPath);
  s.ed.reload_settings();
  CHECK_EQ(s.message(), std::wstring());
  CHECK_EQ(command_of(s, L"C-f"), std::string("forward-char"));
  CHECK(s.ed.settings_errors().empty());
}

TEST(settings_removed_file_reverts_to_defaults_with_message) {
  test::Session s;
  s.settings("{\"keys\": {\"C-f\": \"end-of-line\"}}");
  CHECK_EQ(command_of(s, L"C-f"), std::string("end-of-line"));
  s.host.files.erase(L"c:\\work\\.geditor.json");
  s.ed.reload_settings();
  CHECK_EQ(s.message(), std::wstring(L"Settings file removed; using defaults"));
  CHECK_EQ(command_of(s, L"C-f"), std::string("forward-char"));
  // Still missing: no message again.
  s.keys(L"C-a");
  s.ed.reload_settings();
  CHECK_EQ(s.message(), std::wstring());
}

TEST(settings_unreadable_file_keeps_previous_settings) {
  test::Session s;
  s.settings("{\"keys\": {\"C-c a\": \"end-of-line\"}}");
  s.host.unreadable_paths.insert(L"c:\\work\\.geditor.json");
  s.ed.reload_settings();
  CHECK_EQ(s.message(),
           std::wstring(L"Cannot read C:\\work\\.geditor.json: Access is denied (settings kept)"));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("end-of-line"));
  // A path that cannot be inspected, and a folder of that name.
  s.host.unreadable_paths.clear();
  s.host.add_error_path(test::kSettingsPath);
  s.ed.reload_settings();
  CHECK_EQ(s.message(), std::wstring(L"Cannot access C:\\work\\.geditor.json (settings kept)"));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("end-of-line"));
  s.host.error_paths.clear();
  s.host.add_dir(test::kSettingsPath);
  s.ed.reload_settings();
  CHECK_EQ(s.message(), std::wstring(L"C:\\work\\.geditor.json is a directory (settings kept)"));
  CHECK_EQ(command_of(s, L"C-c a"), std::string("end-of-line"));
}

TEST(settings_reload_cancels_pending_keys_and_prefix_argument) {
  test::Session s;
  s.load(L"abcdef");
  s.settings("{}");
  s.keys(L"C-x");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-x-"));
  s.ed.reload_settings();
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
  s.keys(L"C-f");  // a key of its own, not C-x C-f
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.point(), size_t{1});
  s.keys(L"C-u 3");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring(L"C-u 3-"));
  s.ed.reload_settings();
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
  s.keys(L"C-f");
  CHECK_EQ(s.point(), size_t{2});  // moved once, not three times
}

TEST(settings_reload_keeps_the_minibuffer) {
  test::Session s;
  s.load(L"abc");
  s.settings("{}");
  s.keys(L"C-x C-f");
  s.type(L"name");
  s.settings("{\"keys\": {\"C-c a\": \"end-of-line\"}}");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"C:\\work\\name"));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Find file: "));
}

TEST(settings_corrected_file_clears_the_error_lines) {
  test::Session s;
  s.settings(unknown_items(3));
  CHECK_EQ(s.ed.echo_line_count(), 3);
  s.settings("{}");
  CHECK_EQ(s.message(), std::wstring());
  CHECK_EQ(s.ed.echo_line_count(), 1);
}

// ---- [M2-7] the echo area ----

TEST(echo_format_diagnostics_line_counts) {
  std::vector<Diagnostic> errors;
  for (int i = 0; i < 25; ++i) errors.push_back(Diagnostic{TextPos{i + 2, 3}, L"e" + std::to_wstring(i)});
  auto lines = [&](size_t n) {
    return geditor::format_diagnostics(std::span<const Diagnostic>(errors.data(), n), L"f.json", 10);
  };
  // (Vectors are kept in locals: CHECK_EQ binds a reference to its argument.)
  const std::vector<std::wstring> one = lines(1);
  const std::vector<std::wstring> ten = lines(10);
  const std::vector<std::wstring> eleven = lines(11);
  const std::vector<std::wstring> many = lines(25);
  CHECK_EQ(one.size(), size_t{1});
  CHECK_EQ(one[0], std::wstring(L"f.json:2:3: e0"));
  CHECK_EQ(ten.size(), size_t{10});
  CHECK_EQ(ten[9], std::wstring(L"f.json:11:3: e9"));
  CHECK_EQ(eleven.size(), size_t{10});
  CHECK_EQ(eleven[8], std::wstring(L"f.json:10:3: e8"));
  CHECK_EQ(eleven[9], std::wstring(L"... and 2 more"));
  CHECK_EQ(many.size(), size_t{10});
  CHECK_EQ(many[9], std::wstring(L"... and 16 more"));
}

TEST(echo_one_error_is_one_line) {
  test::Session s;
  s.settings(unknown_items(1));
  CHECK_EQ(s.ed.echo_line_count(), 1);
  CHECK_EQ(s.message(), std::wstring(L".geditor.json:2:3: Unknown setting \"unknown_0\""));
}

TEST(echo_ten_errors_are_ten_lines) {
  test::Session s;
  s.settings(unknown_items(10));
  CHECK_EQ(s.ed.echo_line_count(), 10);
  const std::vector<std::wstring> lines = s.ed.message_lines();
  CHECK_EQ(lines.size(), size_t{10});
  CHECK_EQ(lines[9], std::wstring(L".geditor.json:11:3: Unknown setting \"unknown_9\""));
}

TEST(echo_eleven_and_twenty_five_errors_end_with_and_more) {
  test::Session s;
  s.settings(unknown_items(11));
  CHECK_EQ(s.ed.echo_line_count(), 10);
  const std::vector<std::wstring> eleven = s.ed.message_lines();
  CHECK_EQ(eleven.back(), std::wstring(L"... and 2 more"));
  s.settings(unknown_items(25));
  CHECK_EQ(s.ed.echo_line_count(), 10);
  const std::vector<std::wstring> many = s.ed.message_lines();
  CHECK_EQ(many.back(), std::wstring(L"... and 16 more"));
  CHECK_EQ(many[8], std::wstring(L".geditor.json:10:3: Unknown setting \"unknown_8\""));
}

TEST(echo_next_key_returns_to_one_line_and_runs) {
  test::Session s;
  s.load(L"abc");
  s.settings(unknown_items(11));
  CHECK_EQ(s.ed.echo_line_count(), 10);
  s.type(L"x");
  CHECK_EQ(s.ed.echo_line_count(), 1);
  CHECK_EQ(s.text(), std::wstring(L"xabc"));
  s.settings(unknown_items(4));
  s.keys(L"C-e");
  CHECK_EQ(s.ed.echo_line_count(), 1);
  CHECK_EQ(s.point(), size_t{4});
}

TEST(echo_minibuffer_line_counts_toward_ten) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-x C-f");
  s.settings(unknown_items(25));
  CHECK_EQ(s.ed.echo_line_count(), 10);  // 9 message lines + the minibuffer
  const std::vector<std::wstring> lines = s.ed.message_lines();
  CHECK_EQ(lines.size(), size_t{9});
  CHECK_EQ(lines.back(), std::wstring(L"... and 17 more"));
}

TEST(echo_expanded_area_keeps_point_visible) {
  test::Session s;
  std::wstring text;
  for (int i = 0; i < 30; ++i) text += L"line " + std::to_wstring(i) + L"\n";
  s.load(text);
  s.host.echo_editor = &s.ed;
  s.host.lines = 10;
  s.keys(L"C-n C-n C-n C-n C-n C-n C-n C-n");  // line 8 of rows 0..9
  CHECK_EQ(s.ed.top_line(), size_t{0});
  s.settings(unknown_items(10));  // 9 more echo rows: one text row left
  CHECK_EQ(s.host.visible_lines(), 1);
  CHECK_EQ(s.ed.top_line(), size_t{8});
}

// ---- [M2-7] find-user-init-file ----

TEST(init_file_existing_goes_through_open_choice) {
  test::Session s;
  s.load(L"abc");
  s.type(L"x");  // modified: replacing needs the open choice and save question
  s.settings("{\"keys\": {}}");
  s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.host.save_answers.push_back(geditor::SaveChoice::kDontSave);
  s.ed.execute_command("find-user-init-file");
  CHECK_EQ(s.host.open_dialogs, 1);
  CHECK_EQ(s.ed.buffer().path, std::wstring(test::kSettingsPath));
  CHECK_EQ(s.text(), std::wstring(L"{\"keys\": {}}"));
  CHECK(!s.modified());
}

TEST(init_file_existing_new_window) {
  test::Session s;
  s.load(L"abc");
  s.settings("{}");
  s.host.open_answers.push_back(geditor::OpenChoice::kNewWindow);
  s.ed.execute_command("find-user-init-file");
  CHECK_EQ(s.host.launched.size(), size_t{1});
  CHECK_EQ(s.host.launched[0], std::wstring(test::kSettingsPath));
  CHECK_EQ(s.ed.buffer().path, std::wstring(L"C:\\work\\t.txt"));
}

TEST(init_file_missing_opens_template_without_writing) {
  test::Session s;
  s.ed.set_settings_path(test::kSettingsPath);
  s.ed.reload_settings();
  s.ed.execute_command("find-user-init-file");  // untitled empty buffer: no dialog
  CHECK_EQ(s.host.open_dialogs, 0);
  CHECK_EQ(s.ed.buffer().path, std::wstring(test::kSettingsPath));
  CHECK_EQ(s.text(), std::wstring(geditor::settings_template()));
  CHECK(s.modified());
  CHECK(!s.host.has_file(test::kSettingsPath));
  CHECK(s.host.written.empty());
  CHECK_EQ(s.point(), size_t{0});
  s.keys(L"C-/");  // the template is one edit
  CHECK_EQ(s.text(), std::wstring());
  CHECK(!s.modified());
}

TEST(init_file_missing_with_modified_buffer_asks_first) {
  test::Session s;
  s.load(L"abc");
  s.type(L"x");
  s.ed.set_settings_path(test::kSettingsPath);
  s.host.open_answers.push_back(geditor::OpenChoice::kCancel);
  s.ed.execute_command("find-user-init-file");
  CHECK_EQ(s.host.open_dialogs, 1);
  CHECK_EQ(s.text(), std::wstring(L"xabc"));
  s.host.open_answers.push_back(geditor::OpenChoice::kReplace);
  s.host.save_answers.push_back(geditor::SaveChoice::kDontSave);
  s.ed.execute_command("find-user-init-file");
  CHECK_EQ(s.text(), std::wstring(geditor::settings_template()));
  CHECK(s.modified());
  CHECK(!s.host.has_file(test::kSettingsPath));
}

TEST(init_file_saved_template_loads_with_no_errors_and_same_bindings) {
  test::Session s;
  s.ed.set_settings_path(test::kSettingsPath);
  s.ed.reload_settings();
  s.ed.execute_command("find-user-init-file");
  s.keys(L"C-x C-s");
  CHECK(s.host.has_file(test::kSettingsPath));
  CHECK(!s.modified());
  s.ed.reload_settings();  // what the watcher does after the save
  CHECK(s.ed.settings_errors().empty());
  CHECK_EQ(s.message(), std::wstring(L"Wrote C:\\work\\.geditor.json"));
  // Every default binding is unchanged.
  test::Session fresh;
  for (const auto& b : geditor::default_global_bindings()) {
    CHECK_EQ(command_of(s, b.keys), command_of(fresh, b.keys));
  }
  for (const auto& b : geditor::default_compat_bindings()) {
    CHECK_EQ(command_of(s, b.keys), std::string(b.command));
  }
  // The template text itself parses to the defaults.
  const geditor::ConfigParse parsed = geditor::parse_config(test::utf8(std::wstring(geditor::settings_template())));
  CHECK(parsed.syntax_ok);
  CHECK(parsed.errors.empty());
  CHECK(parsed.config.colors == geditor::Config().colors);
  CHECK_EQ(parsed.config.tab_width, geditor::Config().tab_width);
}

TEST(init_file_without_settings_path_fails) {
  test::Session s;
  s.ed.execute_command("find-user-init-file");
  CHECK_EQ(s.message(), std::wstring(L"No settings file: the user profile folder is unknown"));
}
