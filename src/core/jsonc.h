// JSONC parser for the settings file (docs/PLAN.md 1 and 4.5): JSON plus
// "//" and "/* */" comments and trailing commas in objects and arrays.
// Errors carry a 1-based line and column; the column counts code points.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace geditor {

struct TextPos {
  int line = 1;
  int column = 1;

  bool operator==(const TextPos&) const = default;
};

struct JsonMember;

struct JsonValue {
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Type type = Type::kNull;
  TextPos pos;               // first character of the value
  bool boolean = false;
  double number = 0;
  bool integer = false;      // a number without fraction or exponent that fits int64_t
  int64_t integer_value = 0;
  std::wstring string;       // UTF-16
  std::vector<JsonValue> items;
  // Members in file order; a key written twice appears twice.
  std::vector<JsonMember> members;
};

struct JsonMember {
  std::wstring key;
  TextPos key_pos;  // the opening quote of the key
  JsonValue value;
};

struct JsonError {
  TextPos pos;
  std::wstring message;
};

struct JsonParse {
  bool ok = false;
  JsonValue value;  // an object when ok
  JsonError error;  // the first syntax error when !ok
};

// Parses UTF-8 text (with or without a BOM). The top-level value must be an
// object; text that is empty or holds only comments and white space is an
// empty object. Invalid UTF-8 is a syntax error at the offending byte.
JsonParse parse_jsonc(std::string_view utf8);

}  // namespace geditor
