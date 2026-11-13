#include "config.h"

#include <algorithm>

#include "char_width.h"
#include "commands.h"
#include "unicode.h"

namespace geditor {
namespace {

constexpr const wchar_t* kWrapModes[] = {L"none", L"window", L"fixed", L"max"};
constexpr const wchar_t* kCaseFolds[] = {L"smart", L"always", L"never"};
constexpr const wchar_t* kEols[] = {L"lf", L"crlf", L"cr"};

struct ColorName {
  const wchar_t* name;
  ColorIndex index;
};

constexpr ColorName kColorNames[] = {
    {L"background", kColorBackground},
    {L"foreground", kColorForeground},
    {L"cursor", kColorCursor},
    {L"selection", kColorSelection},
    {L"line_number", kColorLineNumber},
    {L"line_number_current", kColorLineNumberCurrent},
    {L"search_match", kColorSearchMatch},
    {L"search_current", kColorSearchCurrent},
    {L"whitespace", kColorWhitespace},
    {L"eol", kColorEol},
    {L"eol_other", kColorEolOther},
    {L"mode_line_background", kColorModeLineBackground},
    {L"mode_line_foreground", kColorModeLineForeground},
};

constexpr const wchar_t* kKeyItems[kKeymapCount] = {L"keys", L"minibuffer_keys", L"isearch_keys",
                                                     L"query_replace_keys"};

std::wstring quoted(std::wstring_view s) {
  return L"\"" + std::wstring(s) + L"\"";
}

std::wstring join_choices(std::span<const wchar_t* const> choices) {
  std::wstring s;
  for (const wchar_t* c : choices) {
    if (!s.empty()) s += L", ";
    s += c;
  }
  return s;
}

// Command names are ASCII; anything else cannot name a command.
bool narrow_ascii(std::wstring_view s, std::string* out) {
  out->clear();
  for (const wchar_t c : s) {
    if (c <= 0 || c >= 0x80) return false;
    out->push_back(static_cast<char>(c));
  }
  return true;
}

class Validator {
 public:
  explicit Validator(std::vector<Diagnostic>& errors) : errors_(errors) {}

  void error(TextPos pos, std::wstring message) { errors_.push_back(Diagnostic{pos, std::move(message)}); }

  // Members of `object` without the earlier copies of a key written twice
  // (decision D4: the later one wins, the earlier one is reported).
  std::vector<const JsonMember*> unique_members(const JsonValue& object, std::wstring_view path) {
    std::vector<const JsonMember*> out;
    const std::vector<JsonMember>& m = object.members;
    for (size_t i = 0; i < m.size(); ++i) {
      bool later = false;
      for (size_t j = i + 1; j < m.size() && !later; ++j) later = m[j].key == m[i].key;
      if (later) {
        error(m[i].key_pos, L"Duplicate item " + quoted(item_path(path, m[i].key)) + L"; the later one is used");
        continue;
      }
      out.push_back(&m[i]);
    }
    return out;
  }

  static std::wstring item_path(std::wstring_view path, std::wstring_view key) {
    return path.empty() ? std::wstring(key) : std::wstring(path) + L"." + std::wstring(key);
  }

  void unknown(const JsonMember& m, std::wstring_view path) {
    error(m.key_pos, L"Unknown setting " + quoted(item_path(path, m.key)));
  }

  bool object(const JsonMember& m, std::wstring_view path) {
    if (m.value.type == JsonValue::Type::kObject) return true;
    error(m.value.pos, quoted(item_path(path, m.key)) + L" must be an object");
    return false;
  }

  void get_bool(const JsonMember& m, std::wstring_view path, bool* out) {
    if (m.value.type != JsonValue::Type::kBool) {
      error(m.value.pos, quoted(item_path(path, m.key)) + L" must be true or false");
      return;
    }
    *out = m.value.boolean;
  }

  void get_int(const JsonMember& m, std::wstring_view path, int lo, int hi, int* out) {
    const JsonValue& v = m.value;
    if (v.type != JsonValue::Type::kNumber || !v.integer || v.integer_value < lo || v.integer_value > hi) {
      error(v.pos, quoted(item_path(path, m.key)) + L" must be a whole number from " + std::to_wstring(lo) +
                       L" to " + std::to_wstring(hi));
      return;
    }
    *out = static_cast<int>(v.integer_value);
  }

  void get_number(const JsonMember& m, std::wstring_view path, double lo, double hi, double* out) {
    const JsonValue& v = m.value;
    if (v.type != JsonValue::Type::kNumber || v.number < lo || v.number > hi) {
      error(v.pos, quoted(item_path(path, m.key)) + L" must be a number from " +
                       std::to_wstring(static_cast<int>(lo)) + L" to " + std::to_wstring(static_cast<int>(hi)));
      return;
    }
    *out = v.number;
  }

  void get_name(const JsonMember& m, std::wstring_view path, std::wstring* out) {
    const JsonValue& v = m.value;
    if (v.type != JsonValue::Type::kString || v.string.empty()) {
      error(v.pos, quoted(item_path(path, m.key)) + L" must be a non-empty string");
      return;
    }
    *out = v.string;
  }

  // Returns the index in `choices`, or -1 (reported).
  int get_choice(const JsonMember& m, std::wstring_view path, std::span<const wchar_t* const> choices) {
    const JsonValue& v = m.value;
    if (v.type == JsonValue::Type::kString) {
      for (size_t i = 0; i < choices.size(); ++i) {
        if (v.string == choices[i]) return static_cast<int>(i);
      }
    }
    error(v.pos, quoted(item_path(path, m.key)) + L" must be one of " + join_choices(choices));
    return -1;
  }

  void get_color(const JsonMember& m, std::wstring_view path, uint32_t* out) {
    const JsonValue& v = m.value;
    bool ok = v.type == JsonValue::Type::kString && v.string.size() == 7 && v.string[0] == L'#';
    uint32_t value = 0;
    for (size_t i = 1; ok && i < 7; ++i) {
      const wchar_t c = v.string[i];
      int h = -1;
      if (c >= L'0' && c <= L'9') h = c - L'0';
      if (c >= L'a' && c <= L'f') h = c - L'a' + 10;
      if (c >= L'A' && c <= L'F') h = c - L'A' + 10;
      ok = h >= 0;
      value = value * 16 + static_cast<uint32_t>(h);
    }
    if (!ok) {
      error(v.pos, L"Invalid colour for " + quoted(item_path(path, m.key)) + L": use \"#rrggbb\"");
      return;
    }
    *out = value;
  }

  // An end-of-line mark: one character of one or two cells.
  void get_mark(const JsonMember& m, std::wstring_view path, std::wstring* out) {
    const JsonValue& v = m.value;
    bool ok = v.type == JsonValue::Type::kString && !v.string.empty();
    if (ok) {
      size_t units = 0;
      const char32_t c = decode_at(v.string, 0, &units);
      const int w = (c >= 0xD800 && c <= 0xDFFF) ? 0 : char_width(c);
      ok = units == v.string.size() && c >= 0x20 && c != 0x7F && (w == 1 || w == 2);
    }
    if (!ok) {
      error(v.pos, quoted(item_path(path, m.key)) + L" must be one character");
      return;
    }
    *out = v.string;
  }

  // ---- Key bindings ----

  // One call ["command", args...]; `in_sequence`: arguments are required.
  bool parse_call(const JsonValue& array, bool in_sequence, CommandCall* call) {
    if (array.items.empty() || array.items[0].type != JsonValue::Type::kString) {
      error(array.items.empty() ? array.pos : array.items[0].pos, L"Expected a command name");
      return false;
    }
    const JsonValue& name = array.items[0];
    const CommandInfo* info = nullptr;
    if (narrow_ascii(name.string, &call->name)) info = find_command(call->name);
    if (info == nullptr) {
      error(name.pos, L"Unknown command " + quoted(name.string));
      return false;
    }
    const size_t given = array.items.size() - 1;
    const size_t wanted = info->params.size();
    if (given == 0 && wanted > 0 && in_sequence) {
      error(array.pos, quoted(name.string) + L" needs " + std::to_wstring(wanted) +
                           (wanted == 1 ? L" argument" : L" arguments") + L" in a command sequence");
      return false;
    }
    if (given != 0 && given != wanted) {
      error(array.pos, quoted(name.string) + L" takes " +
                           (wanted == 0 ? std::wstring(L"no arguments")
                                        : std::to_wstring(wanted) + (wanted == 1 ? L" argument" : L" arguments")) +
                           L", not " + std::to_wstring(given));
      return false;
    }
    call->args.clear();
    for (size_t i = 0; i < given; ++i) {
      const JsonValue& a = array.items[i + 1];
      const Param& p = info->params[i];
      const std::wstring which = L"Argument " + std::to_wstring(i + 1) + L" of " + quoted(name.string);
      CommandArg arg;
      switch (p.type) {
        case ParamType::kText:
          if (a.type != JsonValue::Type::kString) {
            error(a.pos, which + L" must be a string");
            return false;
          }
          arg.text = a.string;
          break;
        case ParamType::kInteger:
          if (a.type != JsonValue::Type::kNumber || !a.integer) {
            error(a.pos, which + L" must be a whole number");
            return false;
          }
          arg.type = CommandArg::Type::kInteger;
          arg.integer = a.integer_value;
          break;
        case ParamType::kChoice: {
          const bool found = a.type == JsonValue::Type::kString &&
                             std::any_of(p.choices.begin(), p.choices.end(),
                                         [&](const wchar_t* c) { return a.string == c; });
          if (!found) {
            error(a.pos, which + L" must be one of " + join_choices(p.choices));
            return false;
          }
          arg.text = a.string;
          break;
        }
      }
      call->args.push_back(std::move(arg));
    }
    return true;
  }

  // The four value forms of PLAN 4.3.
  bool parse_binding(const JsonValue& v, KeySetting* setting) {
    switch (v.type) {
      case JsonValue::Type::kNull:
        setting->unbind = true;
        return true;
      case JsonValue::Type::kString: {
        CommandCall call;
        const CommandInfo* info = nullptr;
        if (narrow_ascii(v.string, &call.name)) info = find_command(call.name);
        if (info == nullptr) {
          error(v.pos, L"Unknown command " + quoted(v.string));
          return false;
        }
        setting->binding.calls.push_back(std::move(call));
        return true;
      }
      case JsonValue::Type::kArray: {
        if (v.items.empty()) {
          error(v.pos, L"Empty binding: write a command name, [command, arguments...], [[command], ...] or null");
          return false;
        }
        if (v.items[0].type == JsonValue::Type::kArray) {
          setting->binding.sequence = true;
          for (const JsonValue& item : v.items) {
            if (item.type != JsonValue::Type::kArray) {
              error(item.pos, L"Each command of a sequence must be an array [command, arguments...]");
              return false;
            }
            CommandCall call;
            if (!parse_call(item, true, &call)) return false;
            setting->binding.calls.push_back(std::move(call));
          }
          return true;
        }
        CommandCall call;
        if (!parse_call(v, false, &call)) return false;
        setting->binding.calls.push_back(std::move(call));
        return true;
      }
      default:
        error(v.pos, L"Invalid binding: write a command name, [command, arguments...], [[command], ...] or null");
        return false;
    }
  }

  void parse_keys(const JsonMember& m, std::vector<KeySetting>* out) {
    if (!object(m, L"")) return;
    struct Parsed {
      const JsonMember* member;
      KeySequence keys;
    };
    std::vector<Parsed> parsed;
    for (const JsonMember& k : m.value.members) {
      std::optional<KeySequence> keys = parse_key_sequence(k.key);
      if (!keys) {
        error(k.key_pos, L"Invalid key " + quoted(k.key) + L" in " + quoted(m.key));
        continue;
      }
      parsed.push_back(Parsed{&k, std::move(*keys)});
    }
    for (size_t i = 0; i < parsed.size(); ++i) {
      bool later = false;
      for (size_t j = i + 1; j < parsed.size() && !later; ++j) later = parsed[j].keys == parsed[i].keys;
      if (later) {
        error(parsed[i].member->key_pos, L"Duplicate key " + quoted(parsed[i].member->key) + L" in " +
                                             quoted(m.key) + L"; the later one is used");
        continue;
      }
      KeySetting setting;
      setting.keys = parsed[i].keys;
      setting.pos = parsed[i].member->key_pos;
      if (parse_binding(parsed[i].member->value, &setting)) out->push_back(std::move(setting));
    }
  }

  // ---- Items ----

  void parse_font(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"font")) {
      if (m->key == L"family") {
        get_name(*m, L"font", &c->font_family);
      } else if (m->key == L"size") {
        get_number(*m, L"font", 1, 200, &c->font_size);
      } else if (m->key == L"fallback") {
        const JsonValue& v = m->value;
        bool ok = v.type == JsonValue::Type::kArray;
        for (const JsonValue& f : v.items) ok = ok && f.type == JsonValue::Type::kString && !f.string.empty();
        if (!ok) {
          error(v.pos, L"\"font.fallback\" must be an array of font names");
          continue;
        }
        c->font_fallback.clear();
        for (const JsonValue& f : v.items) c->font_fallback.push_back(f.string);
      } else {
        unknown(*m, L"font");
      }
    }
  }

  void parse_colors(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"colors")) {
      const ColorName* found = nullptr;
      for (const ColorName& n : kColorNames) {
        if (m->key == n.name) found = &n;
      }
      if (found == nullptr) {
        unknown(*m, L"colors");
        continue;
      }
      get_color(*m, L"colors", &c->colors[found->index]);
    }
  }

  void parse_wrap(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"wrap")) {
      if (m->key == L"mode") {
        const int i = get_choice(*m, L"wrap", kWrapModes);
        if (i >= 0) c->wrap_mode = static_cast<WrapMode>(i);
      } else if (m->key == L"column") {
        get_int(*m, L"wrap", 1, 10000, &c->wrap_column);
      } else {
        unknown(*m, L"wrap");
      }
    }
  }

  void parse_whitespace(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"whitespace")) {
      if (m->key == L"space") {
        get_bool(*m, L"whitespace", &c->whitespace_space);
      } else if (m->key == L"tab") {
        get_bool(*m, L"whitespace", &c->whitespace_tab);
      } else if (m->key == L"fullwidth_space") {
        get_bool(*m, L"whitespace", &c->whitespace_fullwidth_space);
      } else if (m->key == L"trailing") {
        get_bool(*m, L"whitespace", &c->whitespace_trailing);
      } else {
        unknown(*m, L"whitespace");
      }
    }
  }

  void parse_eol_marks(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"eol_marks")) {
      if (m->key == L"show") {
        get_bool(*m, L"eol_marks", &c->eol_marks_show);
      } else if (m->key == L"lf") {
        get_mark(*m, L"eol_marks", &c->eol_mark_lf);
      } else if (m->key == L"crlf") {
        get_mark(*m, L"eol_marks", &c->eol_mark_crlf);
      } else if (m->key == L"cr") {
        get_mark(*m, L"eol_marks", &c->eol_mark_cr);
      } else {
        unknown(*m, L"eol_marks");
      }
    }
  }

  void parse_new_file(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"new_file")) {
      if (m->key == L"encoding") {
        const int i = get_choice(*m, L"new_file", encoding_names());
        if (i >= 0) c->new_file_encoding = encoding_names()[static_cast<size_t>(i)];
      } else if (m->key == L"eol") {
        const int i = get_choice(*m, L"new_file", kEols);
        if (i >= 0) c->new_file_eol = kEols[i];
      } else {
        unknown(*m, L"new_file");
      }
    }
  }

  void parse_search(const JsonMember& item, Config* c) {
    if (!object(item, L"")) return;
    for (const JsonMember* m : unique_members(item.value, L"search")) {
      if (m->key == L"case_fold") {
        const int i = get_choice(*m, L"search", kCaseFolds);
        if (i >= 0) c->case_fold = static_cast<CaseFold>(i);
      } else {
        unknown(*m, L"search");
      }
    }
  }

  void parse_top(const JsonValue& root, Config* c) {
    for (const JsonMember* m : unique_members(root, L"")) {
      const std::wstring& k = m->key;
      bool is_keys = false;
      for (int i = 0; i < kKeymapCount; ++i) {
        if (k == kKeyItems[i]) {
          parse_keys(*m, &c->keys[static_cast<size_t>(i)]);
          is_keys = true;
        }
      }
      if (is_keys) continue;
      if (k == L"font") {
        parse_font(*m, c);
      } else if (k == L"colors") {
        parse_colors(*m, c);
      } else if (k == L"tab_width") {
        get_int(*m, L"", 1, 32, &c->tab_width);
      } else if (k == L"line_numbers") {
        get_bool(*m, L"", &c->line_numbers);
      } else if (k == L"menu_bar") {
        get_bool(*m, L"", &c->menu_bar);
      } else if (k == L"wrap") {
        parse_wrap(*m, c);
      } else if (k == L"whitespace") {
        parse_whitespace(*m, c);
      } else if (k == L"eol_marks") {
        parse_eol_marks(*m, c);
      } else if (k == L"new_file") {
        parse_new_file(*m, c);
      } else if (k == L"backup") {
        get_bool(*m, L"", &c->backup);
      } else if (k == L"search") {
        parse_search(*m, c);
      } else {
        unknown(*m, L"");
      }
    }
  }

 private:
  std::vector<Diagnostic>& errors_;
};

constexpr wchar_t kTemplate[] =
    L"// geditor settings (JSONC: \"//\" and \"/* */\" comments and trailing commas are allowed).\n"
    L"// Items that are not written keep their built-in values. To change one, copy\n"
    L"// its line from the example below into the object at the end of this file\n"
    L"// and edit it. The file is read again as soon as it is saved.\n"
    L"//\n"
    L"// Key bindings take four forms:\n"
    L"//   \"C-f\": \"forward-char\",                        // command name\n"
    L"//   \"C-c d\": [\"insert-text\", \"2026-11-10\"],       // command with arguments\n"
    L"//   \"C-c l\": [[\"beginning-of-line\"],              // command sequence\n"
    L"//             [\"insert-text\", \"// \"],\n"
    L"//             [\"next-line\"]],\n"
    L"//   \"C-z\": null                                   // unbind\n"
    L"//\n"
    L"// {\n"
    L"//   \"font\": {\n"
    L"//     \"family\": \"Consolas\",\n"
    L"//     \"size\": 12,                                  // points\n"
    L"//     \"fallback\": [\"BIZ UDGothic\", \"Segoe UI Emoji\"]\n"
    L"//   },\n"
    L"//   \"colors\": {\n"
    L"//     \"background\": \"#1e1e1e\", \"foreground\": \"#d4d4d4\",\n"
    L"//     \"cursor\": \"#ffffff\", \"selection\": \"#264f78\",\n"
    L"//     \"line_number\": \"#858585\", \"line_number_current\": \"#c6c6c6\",\n"
    L"//     \"search_match\": \"#613214\", \"search_current\": \"#9e6a03\",\n"
    L"//     \"whitespace\": \"#404040\",\n"
    L"//     \"eol\": \"#505050\", \"eol_other\": \"#d7ba7d\",  // eol_other: line ending that differs from the default\n"
    L"//     \"mode_line_background\": \"#007acc\", \"mode_line_foreground\": \"#ffffff\"\n"
    L"//   },\n"
    L"//   \"tab_width\": 4,\n"
    L"//   \"line_numbers\": true,\n"
    L"//   \"menu_bar\": true,\n"
    L"//   \"wrap\": { \"mode\": \"max\", \"column\": 80 },       // none | window | fixed | max\n"
    L"//   \"whitespace\": { \"space\": false, \"tab\": true, \"fullwidth_space\": true, \"trailing\": true },\n"
    L"//   \"eol_marks\": { \"show\": true, \"lf\": \"↓\", \"crlf\": \"↵\", \"cr\": \"←\" },\n"
    L"//   \"new_file\": { \"encoding\": \"utf-8\", \"eol\": \"lf\" },\n"
    L"//   \"backup\": true,\n"
    L"//   \"search\": { \"case_fold\": \"smart\" },            // smart | always | never\n"
    L"//   \"keys\": { },\n"
    L"//   \"minibuffer_keys\": { },\n"
    L"//   \"isearch_keys\": { },\n"
    L"//   \"query_replace_keys\": { }\n"
    L"// }\n"
    L"{\n"
    L"}\n";

}  // namespace

ConfigParse parse_config(std::string_view utf8) {
  ConfigParse result;
  JsonParse json = parse_jsonc(utf8);
  if (!json.ok) {
    result.errors.push_back(Diagnostic{json.error.pos, json.error.message + L" (file not applied)"});
    return result;
  }
  result.syntax_ok = true;
  Validator v(result.errors);
  v.parse_top(json.value, &result.config);
  return result;
}

std::span<const DefaultBinding> emacs_layer(KeymapKind kind) {
  switch (kind) {
    case KeymapKind::kGlobal: return default_global_bindings();
    case KeymapKind::kMinibuffer: return default_minibuffer_bindings();
    case KeymapKind::kIsearch: return default_isearch_bindings();
    case KeymapKind::kQueryReplace: return default_query_replace_bindings();
  }
  return {};
}

std::span<const DefaultBinding> compat_layer(KeymapKind kind) {
  return kind == KeymapKind::kGlobal ? default_compat_bindings() : std::span<const DefaultBinding>();
}

Keymap build_keymap(std::span<const DefaultBinding> emacs, std::span<const DefaultBinding> compat,
                    std::span<const KeySetting> settings, std::vector<Diagnostic>* errors) {
  Keymap map = make_keymap(emacs);
  for (const DefaultBinding& b : compat) map.bind(b.keys, b.command);
  for (const KeySetting& s : settings) {
    const bool ok = s.unbind ? map.unbind(s.keys) : map.bind(s.keys, s.binding);
    if (ok || errors == nullptr) continue;
    const std::wstring name = key_sequence_to_string(s.keys);
    if (map.lookup(s.keys).result == Keymap::Result::kPrefix) {
      errors->push_back(Diagnostic{s.pos, quoted(name) + L" is a prefix key; binding ignored"});
      continue;
    }
    // A key before the last one runs a command, so it cannot be a prefix.
    KeySequence head;
    for (size_t i = 0; i + 1 < s.keys.size(); ++i) {
      head.push_back(s.keys[i]);
      if (map.lookup(head).result == Keymap::Result::kCommand) break;
    }
    errors->push_back(Diagnostic{s.pos, quoted(name) + L": " + quoted(key_sequence_to_string(head)) +
                                            L" runs a command; binding ignored"});
  }
  return map;
}

std::vector<std::wstring> format_diagnostics(std::span<const Diagnostic> errors,
                                             std::wstring_view file_name, size_t max_lines) {
  std::vector<std::wstring> lines;
  if (max_lines == 0) return lines;
  const size_t shown = errors.size() <= max_lines ? errors.size() : max_lines - 1;
  for (size_t i = 0; i < shown; ++i) {
    const Diagnostic& d = errors[i];
    lines.push_back(std::wstring(file_name) + L":" + std::to_wstring(d.pos.line) + L":" +
                    std::to_wstring(d.pos.column) + L": " + d.message);
  }
  if (shown < errors.size()) {
    lines.push_back(L"... and " + std::to_wstring(errors.size() - shown) + L" more");
  }
  return lines;
}

std::wstring_view settings_template() {
  return kTemplate;
}

}  // namespace geditor
