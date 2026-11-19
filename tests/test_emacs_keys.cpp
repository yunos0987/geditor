// Emacs's default keys added on 2026-11-19 (lisp/bindings.el), each an
// alias of a key that geditor already had:
//   S-<insert> yank (C-y)            C-<insert> kill-ring-save (M-w)
//   S-<delete> kill-region (C-w)     C-<delete> kill-word (M-d)
//   C-DEL backward-kill-word (M-DEL) M-<left> / M-<right> (M-b / M-f)
//   C-@ set-mark-command (C-SPC)     C-<digit> / C-M-<digit> (M-<digit>)
//   C-- / C-M-- negative-argument (M--)
// The scripts run with the default settings only; the settings tests are
// at the end.

#include <cstdio>
#include <string>
#include <vector>

#include "fake_host.h"
#include "menu.h"

using test::Session;

namespace {

// ---- the same script with the old and with the new key ----

struct Step {
  bool key = true;  // key notation; false: typed text
  const wchar_t* text = L"";
};

constexpr const wchar_t* kX = L"\x1";  // the key under test

struct Scene {
  const char* name;
  const wchar_t* content;
  size_t point;
  std::vector<Step> steps;
};

struct Pair {
  const wchar_t* new_key;
  const wchar_t* old_key;
  std::vector<Scene> scenes;
};

struct Snapshot {
  std::wstring text;
  size_t point = 0;
  bool mark_active = false;
  size_t mark = 0;
  bool shift_mark = false;
  std::wstring message;
  std::wstring echo;
  std::wstring mode_line;
  bool isearch = false;
  bool minibuffer = false;
  bool query_replace = false;
  std::wstring minibuffer_text;
  bool modified = false;
  std::wstring clipboard;
  int clipboard_writes = 0;

  bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const Session& s) {
  Snapshot v;
  v.text = s.text();
  v.point = s.point();
  v.mark_active = s.ed.buffer().mark_active;
  v.mark = s.ed.buffer().mark;
  v.shift_mark = s.ed.buffer().shift_mark;
  v.message = s.message();
  v.echo = s.ed.echo_area_text();
  v.mode_line = s.ed.mode_line();
  v.isearch = s.ed.isearch_active();
  v.minibuffer = s.ed.minibuffer_active();
  v.query_replace = s.ed.query_replace_active();
  v.minibuffer_text = s.ed.minibuffer_text();
  v.modified = s.modified();
  v.clipboard = s.host.clipboard;
  v.clipboard_writes = s.host.clipboard_writes;
  return v;
}

void print_snapshot(const char* label, const Snapshot& v) {
  std::printf("    %s: text \"%s\" point %zu mark %d/%zu message \"%s\" echo \"%s\" isearch %d mini %d \"%s\" "
              "qr %d clip \"%s\" writes %d\n",
              label, test::to_display(v.text).c_str(), v.point, v.mark_active ? 1 : 0, v.mark,
              test::to_display(v.message).c_str(), test::to_display(v.echo).c_str(), v.isearch ? 1 : 0,
              v.minibuffer ? 1 : 0, test::to_display(v.minibuffer_text).c_str(), v.query_replace ? 1 : 0,
              test::to_display(v.clipboard).c_str(), v.clipboard_writes);
}

void start(Session& s, const Scene& scene) {
  s.host.clipboard = L"CLIP";
  s.host.clipboard_has_text = true;
  s.load(scene.content);
  s.set_point(scene.point);
}

std::vector<Snapshot> run_scene(const Scene& scene, const wchar_t* key) {
  Session s;
  start(s, scene);
  std::vector<Snapshot> states;
  for (const Step& step : scene.steps) {
    if (!step.key) {
      s.type(step.text);
    } else {
      s.keys(std::wstring(step.text) == kX ? key : step.text);
    }
    states.push_back(snapshot(s));
  }
  return states;
}

Step k(const wchar_t* notation) {
  return Step{true, notation};
}

Step t(const wchar_t* text) {
  return Step{false, text};
}

const Step X{true, kX};

// Scenes that every pair runs: the key alone, with a prefix argument, after
// C-k (a kill in a row), inside a macro, the minibuffer, isearch and
// query-replace, and on a read-only buffer.
std::vector<Scene> common_scenes() {
  return {
      {"alone", L"one two three\nfour five\n", 8, {X, X, k(L"C-/")}},
      {"with a region", L"one two three\nfour five\n", 4, {k(L"C-SPC"), k(L"C-e"), X, k(L"C-/"), X}},
      {"prefix argument", L"one two three\nfour five six\n", 8, {k(L"M-2"), X, k(L"C-/")}},
      {"after C-k", L"one two three\nfour five\n", 4, {k(L"C-k"), X, k(L"C-y")}},
      {"in a macro",
       L"aa bb cc dd ee ff\n",
       9,
       {k(L"C-x ("), X, t(L"!"), k(L"C-x )"), k(L"C-x e"), k(L"e"), k(L"C-/")}},
      {"inside the minibuffer", L"abc def\n", 0, {k(L"M-x"), t(L"for wo"), X, X, k(L"C-g")}},
      {"during isearch", L"foo bar baz\n", 0, {k(L"C-s"), t(L"ba"), X, t(L"z")}},
      {"during query-replace",
       L"a b a b a b\n",
       0,
       {k(L"M-%"), t(L"a"), k(L"RET"), t(L"z"), k(L"RET"), t(L"y"), X, t(L"y"), k(L"q")}},
      {"read-only", L"one two three\n", 8, {k(L"C-x C-q"), X, k(L"C-x C-q"), X}},
  };
}

std::vector<Pair> pairs() {
  return {
      {L"S-<insert>", L"C-y", {}},
      {L"C-<insert>", L"M-w", {}},
      {L"S-<delete>", L"C-w", {}},
      {L"C-<delete>", L"M-d", {}},
      {L"C-DEL", L"M-DEL", {}},
      {L"M-<left>", L"M-b", {}},
      {L"M-<left>", L"C-<left>", {}},
      {L"M-<right>", L"M-f", {}},
      {L"C-@", L"C-SPC", {}},
      {L"C-3", L"M-3", {{"digits then a", L"x\n", 0, {X, t(L"a")}}, {"two digits", L"x\n", 0, {X, k(L"M-1"), t(L"b")}}}},
      {L"C-M-3", L"M-3", {{"digits then a", L"x\n", 0, {X, t(L"a")}}}},
      {L"C-0", L"M-0", {{"zero then a", L"x\n", 0, {k(L"M-1"), X, t(L"a")}}}},
      {L"C--", L"M--", {{"minus then C-f", L"abcdef\n", 4, {X, k(L"C-f")}}, {"minus 2 C-d", L"abcdef\n", 4, {X, k(L"M-2"), k(L"C-d")}}}},
      {L"C-M--", L"M--", {{"minus then M-d", L"one two three\n", 13, {X, k(L"M-d")}}}},
  };
}

// The steps of a scene with a key, written for a failure message.
std::string steps_text(const Scene& scene, const wchar_t* key) {
  std::wstring out;
  for (const Step& step : scene.steps) {
    if (!out.empty()) out += L" | ";
    if (!step.key) out += L"\"";
    out += std::wstring(step.text) == kX ? key : step.text;
    if (!step.key) out += L"\"";
  }
  return test::to_display(out);
}

}  // namespace

TEST(emacs_keys_scripts_equal_the_old_keys_step_by_step) {
  size_t states = 0;
  size_t scenes_run = 0;
  for (Pair& pair : pairs()) {
    std::vector<Scene> scenes = common_scenes();
    for (Scene& extra : pair.scenes) scenes.push_back(extra);
    for (const Scene& scene : scenes) {
      const std::vector<Snapshot> a = run_scene(scene, pair.new_key);
      const std::vector<Snapshot> b = run_scene(scene, pair.old_key);
      CHECK_EQ(a.size(), b.size());
      for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (!(a[i] == b[i])) {
          std::printf("  %s / %s, scene \"%s\", step %zu: %s\n", test::to_display(pair.new_key).c_str(),
                      test::to_display(pair.old_key).c_str(), scene.name, i + 1,
                      steps_text(scene, pair.new_key).c_str());
          print_snapshot("new", a[i]);
          print_snapshot("old", b[i]);
        }
        CHECK(a[i] == b[i]);
        ++states;
      }
      ++scenes_run;
    }
  }
  std::printf("  %zu scenes, %zu states compared\n", scenes_run, states);
  CHECK(scenes_run >= 130);
}

// The comparison above would pass if both keys did nothing: each new key
// does what its command does.
TEST(emacs_keys_do_what_their_commands_do) {
  {
    Session s;  // S-<insert>: yank
    s.host.clipboard = L"CLIP";
    s.host.clipboard_has_text = true;
    s.load(L"ab");
    s.set_point(1);
    s.keys(L"S-<insert>");
    CHECK_EQ(s.text(), std::wstring(L"aCLIPb"));
    CHECK_EQ(s.point(), size_t{5});
  }
  {
    Session s;  // C-<insert>: kill-ring-save
    s.load(L"one two");
    s.keys(L"C-SPC");
    s.keys(L"C-e");
    s.keys(L"C-<insert>");
    CHECK_EQ(s.host.clipboard, std::wstring(L"one two"));
    CHECK_EQ(s.text(), std::wstring(L"one two"));
    CHECK(!s.ed.buffer().mark_active);
  }
  {
    Session s;  // S-<delete>: kill-region
    s.load(L"one two");
    s.keys(L"C-SPC");
    s.keys(L"M-f");
    s.keys(L"S-<delete>");
    CHECK_EQ(s.text(), std::wstring(L" two"));
    CHECK_EQ(s.host.clipboard, std::wstring(L"one"));
  }
  {
    Session s;  // C-<delete>: kill-word
    s.load(L"one two");
    s.keys(L"C-<delete>");
    CHECK_EQ(s.text(), std::wstring(L" two"));
    CHECK_EQ(s.host.clipboard, std::wstring(L"one"));
  }
  {
    Session s;  // C-DEL: backward-kill-word
    s.load(L"one two");
    s.set_point(7);
    s.keys(L"C-DEL");
    CHECK_EQ(s.text(), std::wstring(L"one "));
    CHECK_EQ(s.host.clipboard, std::wstring(L"two"));
  }
  {
    Session s;  // M-<left> / M-<right>: words
    s.load(L"one two three");
    s.set_point(13);
    s.keys(L"M-<left>");
    CHECK_EQ(s.point(), size_t{8});
    s.keys(L"M-<left>");
    CHECK_EQ(s.point(), size_t{4});
    s.keys(L"M-<right>");
    CHECK_EQ(s.point(), size_t{7});
    CHECK(!s.ed.buffer().mark_active);
  }
  {
    Session s;  // C-@: set-mark-command
    s.load(L"one two");
    s.set_point(4);
    s.keys(L"C-@");
    CHECK(s.ed.buffer().mark_active);
    CHECK_EQ(s.ed.buffer().mark, size_t{4});
    CHECK_EQ(s.message(), std::wstring(L"Mark set"));
  }
  {
    Session s;  // C-1 C-2 a, C-M-3 b, M-1 C-0 c: digit-argument
    s.load(L"");
    s.keys(L"C-1");
    s.keys(L"C-2");
    s.keys(L"a");
    s.keys(L"C-M-3");
    s.keys(L"b");
    s.keys(L"M-1");
    s.keys(L"C-0");
    s.keys(L"c");
    CHECK_EQ(s.text(), std::wstring(12, L'a') + L"bbb" + std::wstring(10, L'c'));
  }
  {
    Session s;  // C-- C-f / C-M-- C-f: negative-argument
    s.load(L"abcdef");
    s.set_point(4);
    s.keys(L"C--");
    s.keys(L"C-f");
    CHECK_EQ(s.point(), size_t{3});
    s.keys(L"C-M--");
    s.keys(L"C-f");
    CHECK_EQ(s.point(), size_t{2});
  }
}

// S-<delete> was shift-translated to <delete> (delete-char) before
// 2026-11-19; now it is kill-region, which deletes nothing without a mark.
TEST(emacs_keys_s_delete_without_a_region_does_not_delete_a_character) {
  Session s;
  s.load(L"abc");
  s.set_point(1);
  s.keys(L"S-<delete>");
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  Session c;  // the same message as C-w
  c.load(L"abc");
  c.set_point(1);
  c.keys(L"C-w");
  CHECK_EQ(s.message(), c.message());
  CHECK(!s.message().empty());
  // <delete> itself is unchanged.
  s.keys(L"<delete>");
  CHECK_EQ(s.text(), std::wstring(L"ac"));
}

// As C-w, S-<delete> kills from an inactive mark to the cursor.
TEST(emacs_keys_s_delete_kills_from_an_inactive_mark) {
  Session s;
  s.load(L"abcdef");
  s.set_point(1);
  s.keys(L"C-SPC C-SPC");  // mark at 1, inactive
  CHECK(!s.ed.buffer().mark_active);
  s.set_point(4);
  s.keys(L"S-<delete>");
  CHECK_EQ(s.text(), std::wstring(L"aef"));
  CHECK_EQ(s.host.clipboard, std::wstring(L"bcd"));
}

// Shift added to the new keys: M-<left> / M-<right> select (shift
// translation of a movement command); the others run their command.
TEST(emacs_keys_shift_variants) {
  {
    Session s;
    s.load(L"one two three");
    s.set_point(13);
    s.keys(L"S-M-<left>");
    CHECK_EQ(s.point(), size_t{8});
    CHECK(s.ed.buffer().mark_active && s.ed.buffer().shift_mark);
    CHECK_EQ(s.ed.buffer().mark, size_t{13});
    s.keys(L"S-M-<right>");
    CHECK_EQ(s.point(), size_t{13});
    CHECK(s.ed.buffer().mark_active);
  }
  {
    Session s;  // C-S-<delete>: kill-word through shift translation
    s.load(L"one two");
    s.keys(L"C-S-<delete>");
    CHECK_EQ(s.text(), std::wstring(L" two"));
    CHECK(!s.ed.buffer().mark_active);
  }
  {
    Session s;  // <insert> alone stays undefined (Emacs: overwrite-mode)
    s.load(L"abc");
    s.keys(L"<insert>");
    CHECK_EQ(s.message(), std::wstring(L"<insert> is undefined"));
    CHECK_EQ(s.text(), std::wstring(L"abc"));
  }
}

// The menu's Emacs slot keeps the older keys (ties go to the first key in
// the default table).
TEST(emacs_keys_menu_texts_keep_the_older_keys) {
  Session s;
  s.load(L"abc");
  const geditor::MenuKeyTexts keys(s.ed);
  auto text_of = [&](const char* command) {
    geditor::MenuItem item{};
    item.kind = geditor::MenuItemKind::kCommand;
    item.command = command;
    return keys.text_for(item);
  };
  CHECK_EQ(text_of("cut"), std::wstring(L"C-S-x  C-w"));
  CHECK_EQ(text_of("copy"), std::wstring(L"C-S-c  M-w"));
  CHECK_EQ(text_of("paste"), std::wstring(L"C-S-v  C-y"));
}

// ---- the settings ----

TEST(emacs_keys_settings_null_unbinds_without_shift_translation) {
  Session s;
  s.settings(R"({"keys": {"S-<delete>": null, "S-<insert>": null, "C-5": null}})");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"abc");
  s.keys(L"S-<delete>");
  CHECK_EQ(s.message(), std::wstring(L"S-<delete> is undefined"));  // not <delete>
  CHECK_EQ(s.text(), std::wstring(L"abc"));
  s.keys(L"S-<insert>");
  CHECK_EQ(s.message(), std::wstring(L"S-<insert> is undefined"));
  s.keys(L"C-5");
  CHECK_EQ(s.message(), std::wstring(L"C-5 is undefined"));
  s.keys(L"C-<insert>");  // the other new keys stay
  CHECK(s.message() != std::wstring(L"C-<insert> is undefined"));
}

// isearch looks a key up in its own table first and shift-translates there
// (Editor::lookup), as for C-S-e: an isearch_keys binding of <insert> runs
// on S-<insert>.
TEST(emacs_keys_isearch_keys_binding_of_insert_runs_on_s_insert) {
  for (const bool bound : {false, true}) {
    Session s;
    if (bound) {
      s.settings(R"({"isearch_keys": {"<insert>": "isearch-exit"}})");
      CHECK(s.ed.settings_errors().empty());
    }
    s.host.clipboard = L"CLIP";
    s.host.clipboard_has_text = true;
    s.load(L"foo bar");
    s.keys(L"C-s");
    s.type(L"bar");
    s.keys(L"S-<insert>");
    CHECK(!s.ed.isearch_active());
    CHECK_EQ(s.text(), bound ? std::wstring(L"foo bar") : std::wstring(L"foo barCLIP"));
  }
}

TEST(emacs_keys_settings_bring_back_delete_char_on_s_delete) {
  Session s;
  s.settings(R"({"keys": {"S-<delete>": "delete-char"}})");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"abc");
  s.set_point(1);
  s.keys(L"S-<delete>");
  CHECK_EQ(s.text(), std::wstring(L"ac"));
}
