// M-x (decision D8) and the command registry (decision D9): every command
// name of docs/PLAN.md 4.4 and 4.9 is registered, and the default keys of
// commands of later milestones show "<command> is not implemented yet".

#include <cstdio>
#include <string>

#include "commands.h"
#include "fake_host.h"
#include "unicode.h"

// ---- M-x ----

TEST(mx_tab_completes_command_names) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"M-x "));
  s.type(L"find-user");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"find-user-init-file"));
  s.ed.set_minibuffer_text(L"forw");
  s.keys(L"TAB");  // common prefix of forward-char and forward-word
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"forward-"));
  s.keys(L"TAB");
  CHECK_EQ(s.message(), std::wstring(L"{forward-char | forward-word}"));
  s.ed.set_minibuffer_text(L"text-scale-r");
  s.keys(L"TAB");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"text-scale-reset"));
  s.ed.set_minibuffer_text(L"zzz");
  s.keys(L"TAB");
  CHECK_EQ(s.message(), std::wstring(L"[No match]"));
}

TEST(mx_runs_the_command) {
  test::Session s;
  s.load(L"abc def");
  s.keys(L"M-x");
  s.type(L"end-of-line");
  s.keys(L"RET");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.point(), size_t{7});
  CHECK_EQ(s.ed.last_command(), std::string("end-of-line"));
}

TEST(mx_history) {
  test::Session s;
  s.load(L"abc def");
  s.keys(L"M-x");
  s.type(L"end-of-line");
  s.keys(L"RET");
  s.keys(L"M-x");
  s.type(L"beginning-of-line");
  s.keys(L"RET");
  s.keys(L"M-x M-p");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"beginning-of-line"));
  s.keys(L"M-p");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"end-of-line"));
  s.keys(L"M-p");
  CHECK_EQ(s.message(), std::wstring(L"Beginning of history; no preceding item"));
  s.keys(L"M-n M-n");
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring());
  s.keys(L"M-p RET");
  CHECK_EQ(s.point(), size_t{0});
  // The file name history is separate.
  s.keys(L"C-x C-f M-p");
  CHECK_EQ(s.message(), std::wstring(L"Beginning of history; no preceding item"));
}

TEST(mx_unknown_name_shows_no_match_and_keeps_the_minibuffer) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  s.type(L"no-such-command");
  s.keys(L"RET");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_text(), std::wstring(L"no-such-command"));
  CHECK_EQ(s.message(), std::wstring(L"[No match]"));
  CHECK_EQ(s.ed.echo_area_text(), std::wstring(L"M-x no-such-command [No match]"));
}

TEST(mx_not_implemented_command) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  s.type(L"goto-line");
  s.keys(L"RET");
  CHECK(!s.ed.minibuffer_active());  // no argument prompt for it either
  CHECK_EQ(s.message(), std::wstring(L"goto-line is not implemented yet"));
}

TEST(mx_asks_for_the_arguments_of_insert_text) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"M-x");
  s.type(L"insert-text");
  s.keys(L"RET");
  CHECK(s.ed.minibuffer_active());
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Text: "));
  s.type(L"hello");
  s.keys(L"RET");
  CHECK(!s.ed.minibuffer_active());
  CHECK_EQ(s.text(), std::wstring(L"helloabc"));
  s.keys(L"C-/");  // one undo group
  CHECK_EQ(s.text(), std::wstring(L"abc"));
}

TEST(mx_key_bound_to_command_without_its_arguments_asks_too) {
  test::Session s;
  s.load(L"abc");
  s.settings("{\"keys\": {\"C-c t\": \"insert-text\"}}");
  s.keys(L"C-c t");
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Text: "));
  s.type(L"z");
  s.keys(L"RET");
  CHECK_EQ(s.text(), std::wstring(L"zabc"));
}

TEST(mx_is_refused_inside_the_minibuffer) {
  test::Session s;
  s.load(L"abc");
  s.keys(L"C-x C-f");
  s.keys(L"M-x");
  CHECK_EQ(s.message(), std::wstring(L"Command attempted to use minibuffer while in minibuffer"));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"Find file: "));
  s.keys(L"C-g M-x");
  s.keys(L"M-x");
  CHECK_EQ(s.message(), std::wstring(L"Command attempted to use minibuffer while in minibuffer"));
  CHECK_EQ(s.ed.minibuffer_prompt(), std::wstring(L"M-x "));
}

TEST(mx_passes_the_prefix_argument_through) {
  test::Session s;
  s.load(L"0123456789");
  s.keys(L"C-u 3 M-x");
  CHECK_EQ(s.ed.pending_keys_text(), std::wstring());
  s.type(L"forward-char");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{3});
  s.keys(L"M-- M-x");
  s.type(L"forward-char");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{2});
  s.keys(L"M-x");  // without a prefix: once
  s.type(L"forward-char");
  s.keys(L"RET");
  CHECK_EQ(s.point(), size_t{3});
}

// ---- [C10] every command name of PLAN 4.4 and 4.9 is registered ----

namespace {

// Copied from the PLAN 4.4 tables (Windows compatible keys, Windows editing
// keys, Emacs global keys) and the 4.9 menu table, in that order; names that
// appear more than once are listed once.
constexpr const char* kPlanCommands[] = {
    // 4.4 Windows compatible keys
    "select-all", "copy", "cut", "paste", "undo", "redo", "save-buffer", "quit", "isearch-forward",
    "query-replace", "goto-line",
    // 4.4 Windows editing keys
    "backward-char", "forward-char", "previous-line", "next-line", "beginning-of-line", "end-of-line",
    "scroll-down-command", "scroll-up-command", "backward-word", "forward-word", "beginning-of-buffer",
    "end-of-buffer", "delete-char", "delete-backward-char",
    // 4.4 Emacs: movement
    "recenter-top-bottom",
    // mark
    "set-mark-command", "exchange-point-and-mark", "mark-whole-buffer", "keyboard-quit",
    // delete and kill
    "kill-line", "kill-word", "backward-kill-word", "kill-region", "kill-ring-save", "yank",
    // input
    "newline", "open-line", "insert-tab", "quoted-insert", "transpose-chars", "upcase-word",
    "downcase-word", "capitalize-word",
    // search and replace
    "isearch-backward", "query-replace-regexp",
    // files
    "find-file", "write-file", "read-only-mode",
    // encodings
    "set-buffer-file-coding-system", "revert-buffer-with-coding-system", "find-file-with-encoding",
    "set-eol",
    // view
    "text-scale-increase", "text-scale-decrease", "text-scale-reset", "toggle-line-numbers",
    "toggle-eol-marks", "toggle-whitespace-marks", "set-wrap-mode",
    // menu, prefix argument, macros, other
    "menu-bar-open", "universal-argument", "digit-argument", "negative-argument", "kmacro-start-macro",
    "kmacro-end-macro", "kmacro-end-and-call-macro", "execute-extended-command",
    // 4.9 menu (not listed above)
    "find-user-init-file",
};

// Names of Emacs operations that PLAN 4.4 mentions only as what a geditor
// key replaces (the third column of the Windows compatible key table, and
// text-scale-adjust): geditor has no such command.
constexpr const char* kReplacedEmacsOperations[] = {
    "beginning-of-defun", "exit-recursive-edit", "eval-defun",   "scroll-other-window",
    "isearch-forward-regexp", "append-next-kill", "forward-sexp", "mark-defun",
    "isearch-backward-regexp", "text-scale-adjust",
};

// Commands of later milestones (D9), with their default keys.
struct LaterKey {
  const wchar_t* key;
  const char* command;
};

constexpr LaterKey kLaterKeys[] = {
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
    // <f10> menu-bar-open: implemented in M3b (test_menu.cpp).
    {L"C-x (", "kmacro-start-macro"},
    {L"<f3>", "kmacro-start-macro"},
    {L"C-x )", "kmacro-end-macro"},
    {L"C-x e", "kmacro-end-and-call-macro"},
    {L"<f4>", "kmacro-end-and-call-macro"},
    {L"C-M-f", "isearch-forward"},
    {L"C-M-h", "query-replace"},
    {L"C-M-r", "query-replace"},
    {L"C-M-g", "goto-line"},
};

// The View commands of M3a (menu View / M-x, text-scale also by key).
constexpr const char* kViewCommands[] = {
    "toggle-line-numbers", "toggle-eol-marks",    "toggle-whitespace-marks", "set-wrap-mode",
    "text-scale-increase", "text-scale-decrease", "text-scale-reset",
};

}  // namespace

TEST(registry_has_every_plan_command) {
  for (const char* name : kPlanCommands) {
    if (geditor::find_command(name) == nullptr) std::printf("  missing %s\n", name);
    CHECK(geditor::find_command(name) != nullptr);
  }
  for (const char* name : kReplacedEmacsOperations) {
    CHECK(geditor::find_command(name) == nullptr);
  }
}

TEST(registry_names_are_unique_and_params_are_well_formed) {
  const auto commands = geditor::all_commands();
  for (size_t i = 0; i < commands.size(); ++i) {
    for (size_t j = i + 1; j < commands.size(); ++j) {
      CHECK(std::string(commands[i].name) != commands[j].name);
    }
    for (const geditor::Param& p : commands[i].params) {
      CHECK(p.prompt != nullptr);
      CHECK((p.type == geditor::ParamType::kChoice) == !p.choices.empty());
    }
  }
  // M3c (decision G9): 9 encodings (was 13).
  CHECK_EQ(geditor::encoding_names().size(), size_t{9});
}

TEST(registry_later_commands_are_not_implemented) {
  for (const LaterKey& k : kLaterKeys) {
    const geditor::CommandInfo* info = geditor::find_command(k.command);
    CHECK(info != nullptr && info->fn == nullptr);
  }
}

TEST(registry_view_commands_are_implemented) {
  for (const char* name : kViewCommands) {
    const geditor::CommandInfo* info = geditor::find_command(name);
    CHECK(info != nullptr && info->fn != nullptr);
  }
}

TEST(registry_later_commands_keys_show_not_implemented) {
  test::Session s;
  s.load(L"abc");
  for (const LaterKey& k : kLaterKeys) {
    s.keys(k.key);
    const std::wstring expected = geditor::widen(k.command) + L" is not implemented yet";
    if (s.message() != expected) std::printf("  key %s\n", test::to_display(k.key).c_str());
    CHECK_EQ(s.message(), expected);
    CHECK(!s.ed.minibuffer_active());
    CHECK_EQ(s.text(), std::wstring(L"abc"));
  }
}

TEST(registry_argument_signatures_are_declared) {
  auto params = [](const char* name) { return geditor::find_command(name)->params.size(); };
  CHECK_EQ(params("insert-text"), size_t{1});
  CHECK_EQ(params("set-eol"), size_t{1});
  CHECK_EQ(params("set-wrap-mode"), size_t{1});
  CHECK_EQ(params("set-buffer-file-coding-system"), size_t{1});
  CHECK_EQ(params("revert-buffer-with-coding-system"), size_t{1});
  CHECK_EQ(params("find-file-with-encoding"), size_t{2});
  CHECK_EQ(params("goto-line"), size_t{1});
  CHECK_EQ(params("forward-char"), size_t{0});
}
