add_executable(geditor_tests
  test_main.cpp
  test_harness_selftest.cpp
  test_pcre2.cpp
  fake_host.cpp
  test_char_width.cpp
  test_commands_edit.cpp
  test_commands_move.cpp
  test_encoding.cpp
  test_files.cpp
  test_key.cpp
  test_keymap.cpp
  test_minibuffer.cpp
  test_piece_tree.cpp
  test_review_fixes.cpp
  test_session_clipboard.cpp
  test_shift_translation.cpp
  test_undo.cpp
  test_win_echo.cpp
  test_win_input.cpp
  test_win_logic.cpp)
target_link_libraries(geditor_tests PRIVATE
  geditor_options geditor_core geditor_win_logic pcre2_16 user32)

add_test(NAME geditor_tests COMMAND geditor_tests)

# Performance checks on a generated 100MB text ([M1] perf). Not run by ctest;
# run build/release/tests/geditor_perf.exe by hand.
add_executable(geditor_perf perf/perf_main.cpp)
target_link_libraries(geditor_perf PRIVATE geditor_options geditor_core)
