add_executable(geditor_tests
  test_main.cpp
  test_harness_selftest.cpp
  test_pcre2.cpp)
target_link_libraries(geditor_tests PRIVATE geditor_options pcre2_16)

add_test(NAME geditor_tests COMMAND geditor_tests)
