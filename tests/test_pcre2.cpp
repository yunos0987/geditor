// Checks that the vendored PCRE2 builds as a 16-bit library with Unicode and
// JIT support, as docs/PLAN.md requires.

#include "test_harness.h"

#include <pcre2.h>

#include <string>

namespace {

PCRE2_SPTR16 sptr(const wchar_t* s) {
  return reinterpret_cast<PCRE2_SPTR16>(s);
}

pcre2_code_16* compile(const wchar_t* pattern, uint32_t options) {
  int error = 0;
  PCRE2_SIZE offset = 0;
  return pcre2_compile_16(sptr(pattern), PCRE2_ZERO_TERMINATED, options, &error, &offset,
                          nullptr);
}

}  // namespace

TEST(pcre2_reports_unicode_and_jit_support) {
  uint32_t unicode = 0;
  uint32_t jit = 0;
  CHECK_EQ(pcre2_config_16(PCRE2_CONFIG_UNICODE, &unicode), 0);
  CHECK_EQ(pcre2_config_16(PCRE2_CONFIG_JIT, &jit), 0);
  CHECK_EQ(unicode, 1u);
  CHECK_EQ(jit, 1u);
}

TEST(pcre2_utf_dot_matches_surrogate_pair_as_one_char) {
  pcre2_code_16* re = compile(L"a(.)c", PCRE2_UTF);
  CHECK(re != nullptr);
  if (re == nullptr) return;
  CHECK_EQ(pcre2_jit_compile_16(re, PCRE2_JIT_COMPLETE), 0);

  const wchar_t* subject = L"a\U0001F600c";  // 4 UTF-16 code units
  pcre2_match_data_16* md = pcre2_match_data_create_from_pattern_16(re, nullptr);
  const int rc = pcre2_match_16(re, sptr(subject), 4, 0, 0, md, nullptr);
  CHECK_EQ(rc, 2);
  if (rc == 2) {
    const PCRE2_SIZE* ov = pcre2_get_ovector_pointer_16(md);
    CHECK_EQ(ov[2], static_cast<PCRE2_SIZE>(1));
    CHECK_EQ(ov[3], static_cast<PCRE2_SIZE>(3));
  }
  pcre2_match_data_free_16(md);
  pcre2_code_free_16(re);
}

TEST(pcre2_substitute_uses_dollar_group_syntax) {
  pcre2_code_16* re = compile(L"(\\w+)@(\\w+)", PCRE2_UTF);
  CHECK(re != nullptr);
  if (re == nullptr) return;
  const std::wstring subject = L"user@host";
  wchar_t out[64] = {};
  PCRE2_SIZE out_len = 64;
  const int rc = pcre2_substitute_16(re, sptr(subject.c_str()), subject.size(), 0, 0, nullptr,
                                     nullptr, sptr(L"$2@$1"), PCRE2_ZERO_TERMINATED,
                                     reinterpret_cast<PCRE2_UCHAR16*>(out), &out_len);
  CHECK_EQ(rc, 1);
  CHECK_EQ(std::wstring(out, out_len), std::wstring(L"host@user"));
  pcre2_code_free_16(re);
}

TEST(pcre2_invalid_pattern_reports_error) {
  CHECK(compile(L"(abc", PCRE2_UTF) == nullptr);
}
