# Builds the vendored PCRE2 (third_party/pcre2) as a static 16-bit library
# with Unicode and JIT support, following NON-AUTOTOOLS-BUILD of PCRE2.

set(GEDITOR_PCRE2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pcre2)
set(GEDITOR_PCRE2_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/pcre2_gen)

configure_file(${GEDITOR_PCRE2_DIR}/src/config.h.generic
               ${GEDITOR_PCRE2_GEN_DIR}/config.h COPYONLY)
configure_file(${GEDITOR_PCRE2_DIR}/src/pcre2.h.generic
               ${GEDITOR_PCRE2_GEN_DIR}/pcre2.h COPYONLY)
configure_file(${GEDITOR_PCRE2_DIR}/src/pcre2_chartables.c.dist
               ${GEDITOR_PCRE2_GEN_DIR}/pcre2_chartables.c COPYONLY)

set(GEDITOR_PCRE2_SOURCES
  auto_possess chkdint compile compile_cgroup compile_class config context
  convert dfa_match error extuni find_bracket jit_compile maketables match
  match_data match_next newline ord2utf pattern_info script_run serialize
  string_utils study substitute substring tables ucd valid_utf xclass)
list(TRANSFORM GEDITOR_PCRE2_SOURCES PREPEND ${GEDITOR_PCRE2_DIR}/src/pcre2_)
list(TRANSFORM GEDITOR_PCRE2_SOURCES APPEND .c)
list(APPEND GEDITOR_PCRE2_SOURCES ${GEDITOR_PCRE2_GEN_DIR}/pcre2_chartables.c)

add_library(pcre2_16 STATIC ${GEDITOR_PCRE2_SOURCES})
target_include_directories(pcre2_16
  PUBLIC ${GEDITOR_PCRE2_GEN_DIR}
  PRIVATE ${GEDITOR_PCRE2_DIR}/src)
target_compile_definitions(pcre2_16
  PUBLIC PCRE2_STATIC PCRE2_CODE_UNIT_WIDTH=16
  PRIVATE HAVE_CONFIG_H SUPPORT_PCRE2_16 SUPPORT_UNICODE SUPPORT_JIT
          HAVE_STDINT_H HAVE_INTTYPES_H HAVE_LIMITS_H HAVE_STRING_H
          HAVE_STDLIB_H HAVE_WINDOWS_H)
# Third-party code: do not report its warnings.
target_compile_options(pcre2_16 PRIVATE /w)
