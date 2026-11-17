add_executable(geditor_tests
  test_main.cpp
  test_harness_selftest.cpp
  test_pcre2.cpp
  fake_host.cpp
  test_c_h_c_u.cpp
  test_char_width.cpp
  test_commands_edit.cpp
  test_commands_move.cpp
  test_deferred_fixes.cpp
  test_display.cpp
  test_encoding.cpp
  test_encoding_stretches.cpp
  test_encoding_whole.cpp
  test_files.cpp
  test_grapheme.cpp
  test_grapheme_commands.cpp
  test_jsonc.cpp
  test_key.cpp
  test_keymap.cpp
  test_m3c_keys.cpp
  test_m3c_retry1.cpp
  test_m4a_eol.cpp
  test_m4a_fix.cpp
  test_m4a_menu.cpp
  test_m4a_save.cpp
  test_m4b_commands.cpp
  test_m4b_encoding.cpp
  test_m5_search.cpp
  test_m6.cpp
  test_readme.cpp
  test_layout_cache.cpp
  test_layout_lone.cpp
  test_layout_wrap.cpp
  test_menu.cpp
  test_minibuffer.cpp
  test_mouse.cpp
  test_mx.cpp
  test_piece_tree.cpp
  test_prefix_arg.cpp
  test_quit_keys.cpp
  test_retry1.cpp
  test_review_fixes.cpp
  test_search_backward.cpp
  test_session_clipboard.cpp
  test_settings.cpp
  test_shift_translation.cpp
  test_undo.cpp
  test_visual_line.cpp
  test_win_echo.cpp
  test_win_font.cpp
  test_win_headless.cpp
  test_win_input.cpp
  test_win_logic.cpp
  test_win_menu.cpp
  test_win_save.cpp
  test_win_settings.cpp)
target_link_libraries(geditor_tests PRIVATE
  geditor_options geditor_core geditor_win_logic geditor_text pcre2_16 user32)
# test_readme.cpp reads README.md of the source tree ([M6-3]).
target_compile_definitions(geditor_tests PRIVATE GEDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME geditor_tests COMMAND geditor_tests)

# Performance checks on a generated 100MB text ([M1] perf). Not run by ctest;
# run build/release/tests/geditor_perf.exe by hand.
add_executable(geditor_perf perf/perf_main.cpp)
target_link_libraries(geditor_perf PRIVATE geditor_options geditor_core)
