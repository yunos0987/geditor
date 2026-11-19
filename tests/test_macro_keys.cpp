// The one-key defaults of the keyboard macro commands (2026-11-18):
// C-( kmacro-start-macro, C-) kmacro-end-macro, C-S-e
// kmacro-end-and-call-macro. C-x ( / <f3> / C-x ) / C-x e / <f4> stay.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "fake_host.h"
#include "keymap.h"

using test::Session;

namespace {

// ---- the default tables ----

std::set<std::wstring> default_keys_of(std::span<const geditor::DefaultBinding> table, const char* command) {
  std::set<std::wstring> keys;
  for (const geditor::DefaultBinding& b : table) {
    if (std::string(b.command) == command) keys.insert(b.keys);
  }
  return keys;
}

// ---- the same script with the old and with the new keys ----

enum class Op { kKeys, kType, kStart, kEnd, kCall };

struct Step {
  Op op;
  const wchar_t* text = L"";
};

struct Scene {
  const char* name;
  const wchar_t* content;
  std::vector<Step> steps;
  bool read_only = false;
};

struct Snapshot {
  std::wstring text;
  size_t point = 0;
  bool mark_active = false;
  size_t mark = 0;
  std::wstring message;
  std::wstring echo;
  std::wstring mode_line;
  size_t macro_size = 0;
  bool recording = false;
  bool isearch = false;
  bool minibuffer = false;
  bool query_replace = false;
  std::wstring minibuffer_text;
  bool modified = false;

  bool operator==(const Snapshot&) const = default;
};

Snapshot snapshot(const Session& s) {
  Snapshot v;
  v.text = s.text();
  v.point = s.point();
  v.mark_active = s.ed.buffer().mark_active;
  v.mark = s.ed.buffer().mark;
  v.message = s.message();
  v.echo = s.ed.echo_area_text();
  v.mode_line = s.ed.mode_line();
  v.macro_size = s.ed.macro_size();
  v.recording = s.ed.macro_recording();
  v.isearch = s.ed.isearch_active();
  v.minibuffer = s.ed.minibuffer_active();
  v.query_replace = s.ed.query_replace_active();
  v.minibuffer_text = s.ed.minibuffer_text();
  v.modified = s.modified();
  return v;
}

void print_snapshot(const char* label, const Snapshot& v) {
  std::printf("    %s: text \"%s\" point %zu mark %d/%zu message \"%s\" echo \"%s\" macro %zu rec %d "
              "isearch %d mini %d \"%s\" qr %d\n",
              label, test::to_display(v.text).c_str(), v.point, v.mark_active ? 1 : 0, v.mark,
              test::to_display(v.message).c_str(), test::to_display(v.echo).c_str(), v.macro_size,
              v.recording ? 1 : 0, v.isearch ? 1 : 0, v.minibuffer ? 1 : 0,
              test::to_display(v.minibuffer_text).c_str(), v.query_replace ? 1 : 0);
}

// The state after every step.
std::vector<Snapshot> run_scene(const Scene& scene, bool new_keys) {
  Session s;
  s.load(scene.content);
  if (scene.read_only) s.keys(L"C-x C-q");
  std::vector<Snapshot> states;
  for (const Step& step : scene.steps) {
    switch (step.op) {
      case Op::kKeys: s.keys(step.text); break;
      case Op::kType: s.type(step.text); break;
      case Op::kStart: s.keys(new_keys ? L"C-(" : L"C-x ("); break;
      case Op::kEnd: s.keys(new_keys ? L"C-)" : L"C-x )"); break;
      case Op::kCall: s.keys(new_keys ? L"C-S-e" : L"C-x e"); break;
    }
    states.push_back(snapshot(s));
  }
  return states;
}

const Step kStart{Op::kStart};
const Step kEnd{Op::kEnd};
const Step kCall{Op::kCall};

Step keys(const wchar_t* notation) {
  return Step{Op::kKeys, notation};
}

Step type(const wchar_t* text) {
  return Step{Op::kType, text};
}

std::vector<Scene> scenes() {
  return {
      {"plain record, run, e, M-3", L"", {kStart, type(L"ab"), kEnd, kCall, kCall, keys(L"e"), keys(L"M-3"), kCall}},
      {"a macro with isearch",
       L"foo bar foo bar\n",
       {kStart, keys(L"C-s"), type(L"bar"), keys(L"RET"), type(L"!"), kEnd, kCall}},
      {"a macro with minibuffer input",
       L"1\n2\n3\n4\n5\n6\n",
       {kStart, keys(L"M-g g"), type(L"5"), keys(L"RET"), keys(L"M-x"), type(L"end-of-line"), keys(L"RET"),
        type(L"!"), kEnd, keys(L"M-<"), kCall}},
      {"a macro with query-replace",
       L"a b a b a b\n",
       {kStart, keys(L"M-<"), keys(L"M-%"), type(L"a"), keys(L"RET"), type(L"z"), keys(L"RET"), type(L"yq"), kEnd,
        kCall}},
      {"the call key during isearch",
       L"foo bar\n",
       {kStart, type(L"x"), kEnd, keys(L"C-s"), type(L"bar"), kCall, keys(L"C-/")}},
      {"the call key inside the minibuffer",
       L"abc\n",
       {kStart, type(L"x"), kEnd, keys(L"M-x"), type(L"for"), kCall, keys(L"C-g")}},
      {"the end key inside the minibuffer while recording",
       L"abc\n",
       {kStart, type(L"q"), keys(L"M-x"), type(L"ab"), kEnd, keys(L"C-g"), kCall}},
      {"the start key inside the minibuffer", L"abc\n", {keys(L"M-x"), kStart, type(L"f"), kEnd, keys(L"C-g"), kCall}},
      {"the call key during query-replace",
       L"a a a\n",
       {keys(L"M->"), kStart, type(L"y"), kEnd, keys(L"M-<"), keys(L"M-%"), type(L"a"), keys(L"RET"), type(L"b"),
        keys(L"RET"), kCall, keys(L"q"), keys(L"C-/")}},
      {"the start and end keys during isearch",
       L"foo bar\n",
       {keys(L"C-s"), type(L"foo"), kStart, type(L"!"), keys(L"C-s"), type(L"bar"), kEnd, keys(L"M-<"), kCall}},
      {"calls in the wrong state", L"abc", {kCall, kEnd, kStart, kStart, type(L"z"), kStart, kEnd, kCall}},
      {"while recording the call key only ends", L"", {kStart, type(L"c"), kCall, kCall, kCall}},
      {"one run is one undo unit",
       L"abcdef",
       {kStart, type(L"xy"), keys(L"C-f"), type(L"z"), kEnd, kCall, keys(L"C-/"), keys(L"M-2"), kCall, keys(L"C-/"),
        type(L"q"), kCall, keys(L"C-/")}},
      {"an empty recording keeps the last macro", L"", {kStart, type(L"k"), kEnd, kStart, kEnd, kCall}},
      {"a failing macro stops", L"ab", {kStart, keys(L"C-f"), kEnd, keys(L"M-9"), kCall, kCall}},
      {"a read-only buffer", L"abc", {kStart, type(L"x"), kEnd, kCall}, true},
      {"C-g ends the recording", L"", {kStart, type(L"a"), kEnd, kStart, type(L"b"), keys(L"C-g"), kCall}},
  };
}

}  // namespace

// ---- [C1] the default tables ----

TEST(macro_keys_default_table_has_the_three_keys) {
  const auto global = geditor::default_global_bindings();
  CHECK(default_keys_of(global, "kmacro-start-macro") == (std::set<std::wstring>{L"C-x (", L"<f3>", L"C-("}));
  CHECK(default_keys_of(global, "kmacro-end-macro") == (std::set<std::wstring>{L"C-x )", L"C-)"}));
  CHECK(default_keys_of(global, "kmacro-end-and-call-macro") ==
        (std::set<std::wstring>{L"C-x e", L"<f4>", L"C-S-e"}));
  // Only the global table: the Windows compatible keys stay 12, and the
  // tables of the modes have none of the three commands.
  CHECK_EQ(geditor::default_compat_bindings().size(), size_t{12});
  for (const auto& table : {geditor::default_compat_bindings(), geditor::default_minibuffer_bindings(),
                            geditor::default_isearch_bindings(), geditor::default_query_replace_bindings()}) {
    for (const geditor::DefaultBinding& b : table) {
      const std::wstring key = b.keys;
      CHECK(key != L"C-(" && key != L"C-)" && key != L"C-S-e");
      CHECK(std::string(b.command).rfind("kmacro-", 0) != 0);
    }
  }
}

// ---- [C2] a: record and run ----

TEST(macro_keys_record_with_c_paren_and_run_with_c_s_e) {
  Session s;
  s.load(L"");
  s.keys(L"C-(");
  CHECK_EQ(s.message(), std::wstring(L"Defining kbd macro..."));
  CHECK(s.ed.macro_recording());
  CHECK(s.ed.mode_line().ends_with(L"  Def"));
  s.type(L"ab");
  s.keys(L"C-)");
  CHECK_EQ(s.message(), std::wstring(L"Keyboard macro defined"));
  CHECK(!s.ed.macro_recording());
  CHECK(!s.ed.mode_line().ends_with(L"  Def"));
  CHECK_EQ(s.ed.macro_size(), size_t{2});  // "a" and "b"; not C-)
  CHECK_EQ(s.text(), std::wstring(L"ab"));
  s.keys(L"C-S-e");
  CHECK_EQ(s.text(), std::wstring(L"abab"));
  CHECK_EQ(s.message(), std::wstring(L"(Type e to repeat macro)"));
}

// ---- [C2] b: C-S-e while recording ----

TEST(macro_keys_c_s_e_while_recording_only_ends_the_recording) {
  Session s;
  s.load(L"");
  s.keys(L"C-(");
  s.type(L"c");
  s.keys(L"C-S-e");
  CHECK(!s.ed.macro_recording());
  CHECK_EQ(s.message(), std::wstring(L"Keyboard macro defined"));
  CHECK_EQ(s.ed.macro_size(), size_t{1});  // "c"; not C-S-e
  CHECK_EQ(s.text(), std::wstring(L"c"));  // not run
  s.keys(L"C-S-e");
  CHECK_EQ(s.text(), std::wstring(L"cc"));
}

// ---- [C2] c: counts and repeats ----

TEST(macro_keys_m_3_c_s_e_runs_three_times_and_e_repeats) {
  Session s;
  s.load(L"");
  s.keys(L"C-(");
  s.type(L"x");
  s.keys(L"C-)");
  s.keys(L"M-3 C-S-e");
  CHECK_EQ(s.text(), std::wstring(L"xxxx"));
  s.keys(L"e");
  s.keys(L"e");
  CHECK_EQ(s.text(), std::wstring(L"xxxxxx"));
  s.keys(L"C-S-e");  // the key again: one more run each time
  CHECK_EQ(s.text(), std::wstring(L"xxxxxxx"));
  s.keys(L"C-S-e");
  CHECK_EQ(s.text(), std::wstring(L"xxxxxxxx"));
  s.keys(L"C-a e");  // another key ends the repeats: e is a character again
  CHECK_EQ(s.text(), std::wstring(L"exxxxxxxx"));
}

// ---- [C2] d: the wrong state ----

TEST(macro_keys_messages_in_the_wrong_state) {
  Session s;
  s.load(L"abc");
  s.keys(L"C-S-e");
  CHECK_EQ(s.message(), std::wstring(L"No kbd macro has been defined"));
  s.keys(L"C-)");
  CHECK_EQ(s.message(), std::wstring(L"Not defining kbd macro"));
  s.keys(L"C-(");
  s.type(L"z");
  s.keys(L"C-(");
  CHECK_EQ(s.message(), std::wstring(L"Already defining keyboard macro"));
  CHECK(s.ed.macro_recording());
  s.keys(L"C-)");
  CHECK_EQ(s.ed.macro_size(), size_t{1});  // "z": the second C-( is not recorded
  CHECK_EQ(s.text(), std::wstring(L"zabc"));
  CHECK_EQ(s.point(), size_t{1});
}

// ---- [C2] e: the same as C-x ( / C-x ) / C-x e ----

TEST(macro_keys_scripts_equal_the_c_x_keys_step_by_step) {
  int compared = 0;
  for (const Scene& scene : scenes()) {
    const std::vector<Snapshot> old_keys = run_scene(scene, false);
    const std::vector<Snapshot> new_keys = run_scene(scene, true);
    CHECK_EQ(old_keys.size(), scene.steps.size());
    CHECK_EQ(new_keys.size(), old_keys.size());
    for (size_t i = 0; i < old_keys.size() && i < new_keys.size(); ++i) {
      ++compared;
      if (!(old_keys[i] == new_keys[i])) {
        std::printf("  scene \"%s\" step %zu differs\n", scene.name, i);
        print_snapshot("C-x keys", old_keys[i]);
        print_snapshot("new keys", new_keys[i]);
      }
      CHECK(old_keys[i] == new_keys[i]);
    }
  }
  std::printf("  %zu scenes, %d states compared\n", scenes().size(), compared);
  CHECK(scenes().size() >= 6);
}

// The scenes do what their names say (the comparison above would also pass
// if both key sets did nothing).
TEST(macro_keys_scripts_reach_the_states_they_are_named_for) {
  const std::vector<Scene> all = scenes();
  auto last = [&](const char* name) {
    for (const Scene& scene : all) {
      if (std::string(scene.name) == name) return run_scene(scene, true).back();
    }
    CHECK(false);
    return Snapshot();
  };
  auto at = [&](const char* name, size_t step) {
    for (const Scene& scene : all) {
      if (std::string(scene.name) == name) return run_scene(scene, true).at(step);
    }
    CHECK(false);
    return Snapshot();
  };
  // Typed once, then 1 + 1 + 1 (e) + 3 (M-3) runs.
  CHECK_EQ(last("plain record, run, e, M-3").text, std::wstring(L"ababababababab"));
  CHECK_EQ(last("a macro with isearch").text, std::wstring(L"foo bar! foo bar!\n"));
  CHECK_EQ(last("a macro with minibuffer input").text, std::wstring(L"1\n2\n3\n4\n5!!\n6\n"));
  CHECK_EQ(last("a macro with query-replace").text, std::wstring(L"z b z b a b\n"));
  // During isearch the call key ends the search at the match and runs.
  const Snapshot in_isearch = at("the call key during isearch", 5);
  CHECK(!in_isearch.isearch);
  CHECK_EQ(in_isearch.text, std::wstring(L"xfoo barx\n"));
  CHECK_EQ(in_isearch.message, std::wstring(L"(Type e to repeat macro)"));
  CHECK_EQ(last("the call key during isearch").text, std::wstring(L"xfoo bar\n"));  // C-/: the run only
  // Inside the minibuffer the macro types into the minibuffer.
  const Snapshot in_minibuffer = at("the call key inside the minibuffer", 5);
  CHECK(in_minibuffer.minibuffer);
  CHECK_EQ(in_minibuffer.minibuffer_text, std::wstring(L"forx"));
  CHECK_EQ(in_minibuffer.text, std::wstring(L"xabc\n"));
  // Ending inside the minibuffer drops the keys from M-x on.
  const Snapshot ended = at("the end key inside the minibuffer while recording", 4);
  CHECK(!ended.recording);
  CHECK_EQ(ended.macro_size, size_t{1});
  CHECK_EQ(last("the end key inside the minibuffer while recording").text, std::wstring(L"qqabc\n"));
  // During query-replace the macro's "y" answers the question.
  const Snapshot answered = at("the call key during query-replace", 10);
  CHECK(answered.query_replace);
  CHECK_EQ(answered.text, std::wstring(L"b a a\ny"));
  // The start key ends isearch and starts recording.
  const Snapshot started = at("the start and end keys during isearch", 2);
  CHECK(!started.isearch && started.recording);
  // The end key ends the second isearch too; the macro is "!" C-s b a r, and
  // its run from the start leaves that isearch open at "bar".
  const Snapshot rerun = last("the start and end keys during isearch");
  CHECK_EQ(rerun.text, std::wstring(L"!foo! bar\n"));
  CHECK(rerun.isearch);
  CHECK_EQ(rerun.point, size_t{9});
  CHECK_EQ(rerun.macro_size, size_t{5});
  CHECK_EQ(last("calls in the wrong state").text, std::wstring(L"zzabc"));
  CHECK_EQ(last("while recording the call key only ends").text, std::wstring(L"ccc"));
  CHECK_EQ(last("one run is one undo unit").text, std::wstring(L"xyazqbcdef"));
  CHECK_EQ(last("an empty recording keeps the last macro").text, std::wstring(L"kk"));
  const Snapshot failed = last("a failing macro stops");
  CHECK_EQ(failed.point, size_t{2});
  CHECK_EQ(last("a read-only buffer").text, std::wstring(L"abc"));
  CHECK_EQ(last("a read-only buffer").message, std::wstring(L"Buffer is read-only"));
  CHECK_EQ(last("C-g ends the recording").text, std::wstring(L"aba"));
}

// ---- [C2] f: C-S-e is not shift-translated to C-e any more ----

TEST(macro_keys_c_s_e_does_not_select_to_the_end_of_the_line) {
  Session s;
  s.load(L"abc\ndef");
  s.keys(L"C-S-e");
  CHECK_EQ(s.point(), size_t{0});
  CHECK(!s.ed.buffer().mark_active);
  CHECK(!s.ed.buffer().shift_mark);
  CHECK_EQ(s.message(), std::wstring(L"No kbd macro has been defined"));
  CHECK_EQ(s.text(), std::wstring(L"abc\ndef"));
}

TEST(macro_keys_other_shift_keys_are_unchanged) {
  {
    Session s;  // S-<end> selects to the end of the line
    s.load(L"abc\ndef");
    s.keys(L"S-<end>");
    CHECK_EQ(s.point(), size_t{3});
    CHECK(s.ed.buffer().mark_active && s.ed.buffer().shift_mark);
    CHECK_EQ(s.ed.buffer().mark, size_t{0});
  }
  {
    Session s;  // C-S-b: C-b with a shift selection
    s.load(L"abc\ndef");
    s.set_point(2);
    s.keys(L"C-S-b");
    CHECK_EQ(s.point(), size_t{1});
    CHECK(s.ed.buffer().mark_active && s.ed.buffer().shift_mark);
    CHECK_EQ(s.ed.buffer().mark, size_t{2});
  }
  {
    Session s;  // C-S-a: the Windows key select-all
    s.load(L"abc\ndef");
    s.set_point(2);
    s.keys(L"C-S-a");
    CHECK(s.ed.buffer().mark_active);
    CHECK_EQ(s.ed.buffer().mark, size_t{0});
    CHECK_EQ(s.point(), size_t{7});
  }
  {
    Session s;  // C-e: the end of the line, no selection
    s.load(L"abc\ndef");
    s.keys(L"C-e");
    CHECK_EQ(s.point(), size_t{3});
    CHECK(!s.ed.buffer().mark_active);
  }
  {
    // C-8 / C-9 without Shift are not the macro keys: digit-argument since
    // 2026-11-19 (Emacs's default), so C-8 C-9 a types 89 a's.
    Session s;
    s.load(L"");
    s.keys(L"C-8");
    CHECK(!s.ed.macro_recording());
    s.keys(L"C-9");
    CHECK(!s.ed.macro_recording());
    s.keys(L"a");
    CHECK_EQ(s.text(), std::wstring(89, L'a'));
    CHECK(!s.ed.macro_recording());
  }
}

// ---- [C2] g: the settings ----

TEST(macro_keys_settings_null_unbinds_without_shift_translation) {
  Session s;
  s.settings(R"({"keys": {"C-S-e": null, "C-(": null}})");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"abc");
  s.keys(L"C-S-e");
  CHECK_EQ(s.message(), std::wstring(L"C-S-e is undefined"));  // not C-e
  CHECK_EQ(s.point(), size_t{0});
  CHECK(!s.ed.buffer().mark_active);
  s.keys(L"C-(");
  CHECK_EQ(s.message(), std::wstring(L"C-( is undefined"));
  CHECK(!s.ed.macro_recording());
  s.keys(L"C-x (");  // the other keys stay
  CHECK(s.ed.macro_recording());
  s.keys(L"C-)");
  CHECK(!s.ed.macro_recording());
}

TEST(macro_keys_settings_bindings_win) {
  Session s;
  // A delimiter: the text has )" in it.
  s.settings(R"j({"keys": {"C-S-e": "end-of-line", "C-(": "forward-char", "C-)": ["insert-text", "!"]}})j");
  CHECK(s.ed.settings_errors().empty());
  s.load(L"abc\ndef");
  s.keys(L"C-S-e");
  CHECK_EQ(s.point(), size_t{3});  // end-of-line, without a selection
  CHECK(!s.ed.buffer().mark_active);
  s.keys(L"M-<");
  s.keys(L"C-(");
  CHECK_EQ(s.point(), size_t{1});
  CHECK(!s.ed.macro_recording());
  s.keys(L"C-)");
  CHECK_EQ(s.text(), std::wstring(L"a!bc\ndef"));
}

// "C-S-8" is not a key notation: a character that Shift gives is written as
// that character.
TEST(macro_keys_settings_c_s_8_is_an_invalid_key) {
  Session s;
  s.settings(R"({"keys": {"C-S-8": "kmacro-start-macro"}})");
  CHECK_EQ(s.ed.settings_errors().size(), size_t{1});
  CHECK(!geditor::parse_key(L"C-S-8").has_value());
  CHECK(geditor::parse_key(L"C-(").has_value());
  CHECK(geditor::parse_key(L"C-)").has_value());
}
