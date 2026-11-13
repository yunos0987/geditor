// JSONC parser ([M2-1]): comments, trailing commas, every JSON form, and the
// line / column of each kind of syntax error (column in code points).

#include <cstdio>
#include <string>

#include "jsonc.h"
#include "test_harness.h"

using geditor::JsonParse;
using geditor::JsonValue;
using geditor::TextPos;

namespace {

JsonParse parse(std::string_view text) {
  return geditor::parse_jsonc(text);
}

const JsonValue* member(const JsonValue& object, const wchar_t* key) {
  for (const auto& m : object.members) {
    if (m.key == key) return &m.value;
  }
  return nullptr;
}

// Checks a syntax error at line:column whose message contains `fragment`.
void check_error(std::string_view text, int line, int column, const wchar_t* fragment) {
  const JsonParse p = parse(text);
  CHECK(!p.ok);
  CHECK_EQ(p.error.pos.line, line);
  CHECK_EQ(p.error.pos.column, column);
  if (p.error.message.find(fragment) == std::wstring::npos) {
    std::printf("  message: %s\n", test::to_display(p.error.message).c_str());
    CHECK(false);
  }
}

}  // namespace

TEST(jsonc_line_comment) {
  const JsonParse p = parse("// leading comment\n{\"a\": 1 // after a value\n}\n");
  CHECK(p.ok);
  const JsonValue* a = member(p.value, L"a");
  CHECK(a != nullptr && a->integer && a->integer_value == 1);
}

TEST(jsonc_block_comment) {
  const JsonParse p = parse("{/* c1 */\"a\"/* c2\n spans lines */:/**/2}");
  CHECK(p.ok);
  const JsonValue* a = member(p.value, L"a");
  CHECK(a != nullptr && a->integer_value == 2);
}

TEST(jsonc_comment_at_end_of_file_without_newline) {
  const JsonParse p = parse("{\"a\": 1}// end");
  CHECK(p.ok);
  CHECK_EQ(p.value.members.size(), size_t{1});
}

TEST(jsonc_trailing_comma_in_object) {
  const JsonParse p = parse("{\"a\": 1, \"b\": 2,}");
  CHECK(p.ok);
  CHECK_EQ(p.value.members.size(), size_t{2});
}

TEST(jsonc_trailing_comma_in_array) {
  const JsonParse p = parse("{\"a\": [1, 2, ]}");
  CHECK(p.ok);
  const JsonValue* a = member(p.value, L"a");
  CHECK(a != nullptr && a->type == JsonValue::Type::kArray && a->items.size() == 2);
}

TEST(jsonc_nested_objects_and_arrays) {
  const JsonParse p = parse("{\"a\": {\"b\": [1, [2, {\"c\": true}], {}], \"d\": {}}}");
  CHECK(p.ok);
  const JsonValue* a = member(p.value, L"a");
  CHECK(a != nullptr && a->type == JsonValue::Type::kObject);
  if (a == nullptr) return;
  const JsonValue* b = member(*a, L"b");
  CHECK(b != nullptr && b->items.size() == 3);
  if (b == nullptr || b->items.size() != 3) return;
  const JsonValue& inner = b->items[1];
  CHECK(inner.type == JsonValue::Type::kArray && inner.items.size() == 2);
  if (inner.items.size() != 2) return;
  const JsonValue* c = member(inner.items[1], L"c");
  CHECK(c != nullptr && c->type == JsonValue::Type::kBool && c->boolean);
  CHECK(b->items[2].type == JsonValue::Type::kObject && b->items[2].members.empty());
}

TEST(jsonc_every_string_escape) {
  const JsonParse p = parse(
      "{\"s\": \"q\\\" b\\\\ s\\/ \\b\\f\\n\\r\\t u\\u0041\\u3042 pair\\ud83d\\ude00\"}");
  CHECK(p.ok);
  const JsonValue* s = member(p.value, L"s");
  CHECK(s != nullptr);
  if (s != nullptr) {
    CHECK_EQ(s->string, std::wstring(L"q\" b\\ s/ \b\f\n\r\t uA\u3042 pair\U0001F600"));
  }
}

TEST(jsonc_numbers) {
  const JsonParse p =
      parse("{\"i\": 42, \"n\": -7, \"z\": 0, \"f\": 3.25, \"e\": -1e3, \"E\": 2E+2, \"g\": 1.5e-1}");
  CHECK(p.ok);
  const JsonValue* i = member(p.value, L"i");
  const JsonValue* n = member(p.value, L"n");
  const JsonValue* z = member(p.value, L"z");
  const JsonValue* f = member(p.value, L"f");
  const JsonValue* e = member(p.value, L"e");
  const JsonValue* big_e = member(p.value, L"E");
  const JsonValue* g = member(p.value, L"g");
  CHECK(i && i->integer && i->integer_value == 42);
  CHECK(n && n->integer && n->integer_value == -7);
  CHECK(z && z->integer && z->integer_value == 0);
  CHECK(f && !f->integer && f->number == 3.25);
  CHECK(e && !e->integer && e->number == -1000.0);
  CHECK(big_e && !big_e->integer && big_e->number == 200.0);
  CHECK(g && !g->integer && g->number == 0.15);
}

TEST(jsonc_true_false_null) {
  const JsonParse p = parse("{\"t\": true, \"f\": false, \"n\": null}");
  CHECK(p.ok);
  const JsonValue* t = member(p.value, L"t");
  const JsonValue* f = member(p.value, L"f");
  const JsonValue* n = member(p.value, L"n");
  CHECK(t && t->type == JsonValue::Type::kBool && t->boolean);
  CHECK(f && f->type == JsonValue::Type::kBool && !f->boolean);
  CHECK(n && n->type == JsonValue::Type::kNull);
}

TEST(jsonc_utf8_bom) {
  const JsonParse p = parse("\xEF\xBB\xBF{\"a\": \"\xE6\x97\xA5\"}");
  CHECK(p.ok);
  const JsonValue* a = member(p.value, L"a");
  CHECK(a && a->string == L"\u65e5");
  // The BOM is not a column: an error right after it is at column 2.
  check_error("\xEF\xBB\xBF{x}", 1, 2, L"Expected a key");
}

TEST(jsonc_empty_file_is_empty_object) {
  const JsonParse p = parse("");
  CHECK(p.ok);
  CHECK(p.value.type == JsonValue::Type::kObject && p.value.members.empty());
}

TEST(jsonc_comment_only_file_is_empty_object) {
  const JsonParse p = parse("// nothing here\n  /* or here */\n\n");
  CHECK(p.ok);
  CHECK(p.value.type == JsonValue::Type::kObject && p.value.members.empty());
}

TEST(jsonc_duplicate_keys_are_kept_in_order) {
  const JsonParse p = parse("{\"a\": 1, \"a\": 2}");
  CHECK(p.ok);
  CHECK_EQ(p.value.members.size(), size_t{2});
  CHECK_EQ(p.value.members[1].key_pos.column, 10);
}

// ---- Errors: exact line and column ----

TEST(jsonc_error_unterminated_string) {
  check_error("{\n  \"abc\n}", 2, 3, L"Unterminated string");
  check_error("{\"abc", 1, 2, L"Unterminated string");
}

TEST(jsonc_error_unterminated_block_comment) {
  check_error("{\n /* never closed", 2, 2, L"Unterminated comment");
}

TEST(jsonc_error_missing_colon) {
  check_error("{\"a\" 1}", 1, 6, L"Expected ':'");
}

TEST(jsonc_error_missing_comma) {
  check_error("{\"a\": 1 \"b\": 2}", 1, 9, L"Expected ',' or '}'");
  check_error("{\"a\": [1 2]}", 1, 10, L"Expected ',' or ']'");
}

TEST(jsonc_error_leading_comma) {
  check_error("{, \"a\": 1}", 1, 2, L"Expected a key or '}'");
  check_error("{\"a\": [, 1]}", 1, 8, L"Expected a value or ']'");
}

TEST(jsonc_error_double_comma) {
  check_error("{\"a\": 1,, \"b\": 2}", 1, 9, L"Expected a key or '}'");
  check_error("{\"a\": [1,, 2]}", 1, 10, L"Expected a value or ']'");
}

TEST(jsonc_error_unexpected_character) {
  check_error("{\"a\": @}", 1, 7, L"Unexpected '@'");
  check_error("{\"a\": tru}", 1, 7, L"Unexpected word 'tru'");
}

TEST(jsonc_error_lone_surrogate_escape) {
  check_error("{\"a\": \"x\\ud800y\"}", 1, 9, L"Lone surrogate");
  check_error("{\"a\": \"\\udc00\"}", 1, 8, L"Lone surrogate");
}

TEST(jsonc_error_raw_control_character_in_string) {
  check_error("{\"a\": \"x\ty\"}", 1, 9, L"Control character U+0009");
}

TEST(jsonc_error_invalid_utf8_byte) {
  // { " a " : space " then U+3042 (3 bytes, column 8), then 0xFF: column 9.
  check_error("{\"a\": \"\xE3\x81\x82\xFF\"}", 1, 9, L"Invalid UTF-8");
  check_error("{}\n\xC0\xAF", 2, 1, L"Invalid UTF-8");  // overlong form
}

TEST(jsonc_error_top_level_not_object) {
  check_error("[1, 2]", 1, 1, L"Expected '{'");
  check_error("  \n 42", 2, 2, L"Expected '{'");
}

TEST(jsonc_error_text_after_top_level_value) {
  check_error("{}\n x", 2, 2, L"after the top-level object");
  check_error("{} {}", 1, 4, L"after the top-level object");
}

TEST(jsonc_error_column_counts_code_points) {
  // Three CJK characters (U+65E5 U+672C U+8A9E) are 9 bytes but 3 columns;
  // the missing colon is at column 8.
  check_error("{\"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\" 1}", 1, 8, L"Expected ':'");
  // A surrogate pair character (U+1F600, 4 bytes) is one column too.
  check_error("{\"\xF0\x9F\x98\x80\" 1}", 1, 6, L"Expected ':'");
}
