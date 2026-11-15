// The parts of --headless that need no window (docs/PLAN.md 2 "headless"):
// the flag, new windows that inherit it, the question builder shared by the
// task dialogs and the headless channel, and the WM_COPYDATA payloads.

#include <windows.h>

#include <string>
#include <vector>

#include "command_line.h"
#include "headless.h"
#include "process.h"
#include "question.h"
#include "test_harness.h"

using geditor::OpenChoice;
using geditor::SaveChoice;
using geditor::win::CommandLine;
using geditor::win::parse_command_line;
using geditor::win::Question;

namespace {

// The values of the state lines of `text` that start with `key` + "=",
// unescaped as tools/app_driver.py does.
std::vector<std::wstring> state_values(const std::wstring& text, const std::wstring& key) {
  std::vector<std::wstring> values;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(L'\n', start);
    if (end == std::wstring::npos) end = text.size();
    const std::wstring line = text.substr(start, end - start);
    if (line.rfind(key + L"=", 0) == 0) {
      std::wstring value;
      for (size_t i = key.size() + 1; i < line.size(); ++i) {
        if (line[i] == L'\\' && i + 1 < line.size()) {
          ++i;
          value += line[i] == L'n' ? L'\n' : line[i] == L'r' ? L'\r' : line[i];
        } else {
          value += line[i];
        }
      }
      values.push_back(value);
    }
    start = end + 1;
  }
  return values;
}

// The task dialog and the headless state of `q` show the same text and the
// same labels in the same order.
void check_one_source(const Question& q) {
  const std::vector<TASKDIALOG_BUTTON> buttons = geditor::win::task_dialog_buttons(q);
  const TASKDIALOGCONFIG config = geditor::win::task_dialog_config(q, buttons, nullptr, L"geditor");
  const std::wstring state = geditor::win::question_state(q);

  const std::wstring dialog_instruction = config.pszMainInstruction;
  const std::wstring dialog_content = config.pszContent != nullptr ? config.pszContent : L"";
  const std::vector<std::wstring> instructions = state_values(state, L"instruction");
  const std::vector<std::wstring> contents = state_values(state, L"content");
  CHECK_EQ(instructions.size(), size_t{1});
  CHECK_EQ(contents.size(), size_t{1});
  if (instructions.size() == 1) {
    const std::wstring headless_instruction = instructions[0];
    CHECK_EQ(headless_instruction, dialog_instruction);
  }
  if (contents.size() == 1) {
    const std::wstring headless_content = contents[0];
    CHECK_EQ(headless_content, dialog_content);
  }

  const std::vector<std::wstring> labels = state_values(state, L"button");
  CHECK_EQ(static_cast<size_t>(config.cButtons), labels.size());
  CHECK(config.pButtons == buttons.data());
  for (size_t i = 0; i < labels.size() && i < buttons.size(); ++i) {
    const std::wstring dialog_label = buttons[i].pszButtonText;
    const std::wstring headless_label = labels[i];
    CHECK_EQ(headless_label, dialog_label);
    CHECK_EQ(geditor::win::question_button_id(q, headless_label), buttons[i].nButtonID);
  }
  // No common (localised) buttons; the default is one of the labels.
  CHECK_EQ(static_cast<unsigned long>(config.dwCommonButtons), 0ul);
  const std::vector<std::wstring> defaults = state_values(state, L"default");
  CHECK_EQ(defaults.size(), size_t{1});
  if (defaults.size() == 1) {
    const std::wstring default_label = defaults[0];
    CHECK_EQ(geditor::win::question_button_id(q, default_label), config.nDefaultButton);
  }
}

}  // namespace

TEST(win_headless_flag_is_parsed_without_a_message) {
  const CommandLine c = parse_command_line(L"geditor.exe --headless --settings=s.json a.txt");
  CHECK(c.headless);
  CHECK(c.errors.empty());
  CHECK_EQ(c.file, std::wstring(L"a.txt"));
  CHECK_EQ(c.settings, std::wstring(L"s.json"));
  CHECK(!parse_command_line(L"geditor.exe a.txt").headless);
  // After "--" it is a file name.
  const CommandLine file = parse_command_line(L"geditor.exe -- --headless");
  CHECK(!file.headless);
  CHECK_EQ(file.file, std::wstring(L"--headless"));
}

TEST(win_new_window_of_a_headless_window_is_headless) {
  const std::wstring line =
      geditor::win::editor_command_line(L"C:\\bin\\geditor.exe", L"C:\\a b\\f.txt", L"C:\\s\\s.json", true);
  CHECK_EQ(line, std::wstring(L"\"C:\\bin\\geditor.exe\" \"--headless\" \"--settings=C:\\s\\s.json\" "
                              L"\"C:\\a b\\f.txt\""));
  const CommandLine parsed = parse_command_line(line);
  CHECK(parsed.headless);
  CHECK(parsed.errors.empty());
  CHECK_EQ(parsed.settings, std::wstring(L"C:\\s\\s.json"));
  CHECK_EQ(parsed.file, std::wstring(L"C:\\a b\\f.txt"));
  // Not headless: nothing is added.
  CHECK_EQ(geditor::win::editor_command_line(L"C:\\g.exe", L"C:\\f.txt", L"", false),
           std::wstring(L"\"C:\\g.exe\" \"C:\\f.txt\""));
}

TEST(win_questions_give_the_task_dialog_and_the_headless_state_one_text) {
  check_one_source(geditor::win::open_choice_question(L"C:\\dir\\other.txt"));
  check_one_source(geditor::win::save_changes_question(L"a.txt"));
  check_one_source(geditor::win::yes_no_question(L"File C:\\x.txt exists; overwrite?"));
}

TEST(win_question_texts_and_labels) {
  const Question open = geditor::win::open_choice_question(L"C:\\dir\\other.txt");
  CHECK_EQ(open.instruction, std::wstring(L"Open other.txt?"));
  CHECK_EQ(open.content, std::wstring(L"C:\\dir\\other.txt"));
  const std::wstring open_state = geditor::win::question_state(open);
  CHECK_EQ(open_state, std::wstring(L"question=open_choice\ninstruction=Open other.txt?\n"
                                    L"content=C:\\\\dir\\\\other.txt\nbutton=Open in this window\n"
                                    L"button=Open in a new window\nbutton=Cancel\n"
                                    L"default=Open in this window\n"));
  const Question save = geditor::win::save_changes_question(L"a.txt");
  CHECK_EQ(geditor::win::question_state(save),
           std::wstring(L"question=save_changes\ninstruction=Save changes to a.txt?\ncontent=\n"
                        L"button=Save\nbutton=Don't Save\nbutton=Cancel\ndefault=Save\n"));
  const Question yes_no = geditor::win::yes_no_question(L"Overwrite?");
  CHECK_EQ(geditor::win::question_state(yes_no),
           std::wstring(L"question=yes_no\ninstruction=Overwrite?\ncontent=\nbutton=Yes\nbutton=No\n"
                        L"default=No\n"));
}

TEST(win_question_answers_map_to_the_host_choices) {
  const Question open = geditor::win::open_choice_question(L"C:\\a.txt");
  CHECK(geditor::win::open_choice_for(geditor::win::question_button_id(open, L"Open in this window")) ==
        OpenChoice::kReplace);
  CHECK(geditor::win::open_choice_for(geditor::win::question_button_id(open, L"Open in a new window")) ==
        OpenChoice::kNewWindow);
  CHECK(geditor::win::open_choice_for(geditor::win::question_button_id(open, L"Cancel")) == OpenChoice::kCancel);
  const Question save = geditor::win::save_changes_question(L"a.txt");
  CHECK(geditor::win::save_choice_for(geditor::win::question_button_id(save, L"Save")) == SaveChoice::kSave);
  CHECK(geditor::win::save_choice_for(geditor::win::question_button_id(save, L"Don't Save")) ==
        SaveChoice::kDontSave);
  CHECK(geditor::win::save_choice_for(geditor::win::question_button_id(save, L"Cancel")) == SaveChoice::kCancel);
  const Question yes_no = geditor::win::yes_no_question(L"?");
  CHECK(geditor::win::yes_no_for(geditor::win::question_button_id(yes_no, L"Yes")) == true);
  CHECK(geditor::win::yes_no_for(geditor::win::question_button_id(yes_no, L"No")) == false);
  // Esc / the close button of the task dialog.
  CHECK(geditor::win::yes_no_for(IDCANCEL) == false);
  // A label that is no button (case and spelling matter), and "no answer".
  CHECK_EQ(geditor::win::question_button_id(save, L"save"), 0);
  CHECK_EQ(geditor::win::question_button_id(save, L"Yes"), 0);
  CHECK_EQ(geditor::win::question_button_id(save, L""), 0);
  CHECK(!geditor::win::save_choice_for(0).has_value());
  CHECK(!geditor::win::open_choice_for(0).has_value());
  CHECK(!geditor::win::yes_no_for(0).has_value());
}

TEST(win_copy_data_payloads) {
  CHECK(!geditor::win::copy_data_text(nullptr).has_value());
  wchar_t text[] = L"C:\\x.bmp";
  COPYDATASTRUCT data{};
  data.dwData = geditor::win::kCopyDataState;
  data.lpData = text;
  data.cbData = 8 * sizeof(wchar_t);
  const std::optional<std::wstring> good = geditor::win::copy_data_text(&data);
  CHECK(good.has_value());
  if (good) CHECK_EQ(*good, std::wstring(L"C:\\x.bmp"));
  data.cbData = 7;  // odd
  CHECK(!geditor::win::copy_data_text(&data).has_value());
  data.cbData = 4;
  data.lpData = nullptr;  // size without data
  CHECK(!geditor::win::copy_data_text(&data).has_value());
  data.cbData = 0;  // empty: an empty text
  const std::optional<std::wstring> empty = geditor::win::copy_data_text(&data);
  CHECK(empty.has_value() && empty->empty());
  data.cbData = static_cast<DWORD>((geditor::win::kMaxCopyDataUnits + 1) * sizeof(wchar_t));
  data.lpData = text;  // never read: the size is checked first
  CHECK(!geditor::win::copy_data_text(&data).has_value());
}

TEST(win_copy_data_kind_text) {
  CHECK_EQ(geditor::win::copy_data_kind_text(geditor::win::kCopyDataState), std::wstring(L"GSTA"));
  CHECK_EQ(geditor::win::copy_data_kind_text(geditor::win::kCopyDataAnswer), std::wstring(L"GANS"));
  CHECK_EQ(geditor::win::copy_data_kind_text(geditor::win::kCopyDataClipboardWrite), std::wstring(L"GCBW"));
  CHECK_EQ(geditor::win::copy_data_kind_text(0x50414347), std::wstring(L"GCAP"));
  CHECK_EQ(geditor::win::copy_data_kind_text(1), std::wstring(L"0x1"));
}

TEST(win_state_line_keeps_every_value_on_one_line) {
  CHECK_EQ(geditor::win::state_line(L"clipboard_text", L"a\\b\r\nc"),
           std::wstring(L"clipboard_text=a\\\\b\\r\\nc\n"));
  CHECK_EQ(geditor::win::state_line(L"message", L""), std::wstring(L"message=\n"));
}

// M4a fix F2: the modifiers of a headless key come from its lParam bits
// (25 Ctrl, 26 Shift, 27 Alt) and match tools/app_driver.py
// HEADLESS_KEY_BITS; the other bits of a real key message mean nothing.
TEST(win_headless_key_modifiers_come_from_lparam_bits) {
  using geditor::win::headless_key_modifiers;
  CHECK_EQ(geditor::win::kHeadlessKeyCtrl, LPARAM{1} << 25);
  CHECK_EQ(geditor::win::kHeadlessKeyShift, LPARAM{1} << 26);
  CHECK_EQ(geditor::win::kHeadlessKeyAlt, LPARAM{1} << 27);
  const LPARAM plain = 1 | (0x15 << 16);  // 'Y': repeat 1, scan code
  geditor::win::ModifierState m = headless_key_modifiers(plain);
  CHECK(!m.ctrl && !m.shift && !m.alt);
  // The context bit (29) and the transition bits are not Alt.
  m = headless_key_modifiers(plain | (LPARAM{1} << 29) | (LPARAM{1} << 30) | (LPARAM{1} << 31));
  CHECK(!m.ctrl && !m.shift && !m.alt);
  m = headless_key_modifiers(plain | geditor::win::kHeadlessKeyCtrl);
  CHECK(m.ctrl && !m.shift && !m.alt);
  m = headless_key_modifiers(plain | geditor::win::kHeadlessKeyShift | geditor::win::kHeadlessKeyAlt);
  CHECK(!m.ctrl && m.shift && m.alt);
}
