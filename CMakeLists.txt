cmake_minimum_required(VERSION 3.25)
project(geditor LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "geditor supports Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the exe must not depend on the VC++ runtime DLLs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(CheckIPOSupported)
check_ipo_supported(RESULT GEDITOR_IPO_SUPPORTED OUTPUT GEDITOR_IPO_OUTPUT LANGUAGES C CXX)
if(GEDITOR_IPO_SUPPORTED)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

include(cmake/pcre2.cmake)

add_library(geditor_options INTERFACE)
target_compile_definitions(geditor_options INTERFACE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(geditor_options INTERFACE /W4 /utf-8)

# Editing core: no Win32 UI dependency (tests drive it through a fake host).
add_library(geditor_core STATIC
  src/core/char_width.cpp
  src/core/commands.cpp
  src/core/config.cpp
  src/core/display.cpp
  src/core/editor.cpp
  src/core/encoding.cpp
  src/core/files.cpp
  src/core/jsonc.cpp
  src/core/key.cpp
  src/core/keymap.cpp
  src/core/layout.cpp
  src/core/menu.cpp
  src/core/minibuffer.cpp
  src/core/mouse.cpp
  src/core/path.cpp
  src/core/piece_tree.cpp
  src/core/undo.cpp
  src/core/unicode.cpp)
target_include_directories(geditor_core PUBLIC src/core)
target_link_libraries(geditor_core PRIVATE geditor_options)

# Pure logic of the Win32 layer (key translation, command line, clipboard
# text, file access, process start, the HMENUs of the menu model): shared by
# the app and the unit tests.
add_library(geditor_win_logic STATIC
  src/win/clipboard.cpp
  src/win/command_line.cpp
  src/win/echo_text.cpp
  src/win/file_io.cpp
  src/win/input.cpp
  src/win/menu_bar.cpp
  src/win/process.cpp)
target_include_directories(geditor_win_logic PUBLIC src/win)
target_link_libraries(geditor_win_logic PUBLIC geditor_core PRIVATE geditor_options)

# DirectWrite font faces and fallback: shared by the app and the unit tests
# (the tests check which font draws sample characters).
add_library(geditor_text STATIC
  src/win/text_measurer.cpp)
target_include_directories(geditor_text PUBLIC src/win)
target_link_libraries(geditor_text PUBLIC dwrite PRIVATE geditor_options)

add_executable(geditor WIN32
  src/win/config_watcher.cpp
  src/win/dialogs.cpp
  src/win/ime.cpp
  src/win/main.cpp
  src/win/measure.cpp
  src/win/renderer.cpp
  src/win/window.cpp
  src/win/geditor.manifest)
target_link_libraries(geditor PRIVATE
  geditor_options geditor_win_logic geditor_text geditor_core d2d1 dwrite imm32 comctl32 shell32
  delayimp)
# comctl32 (TaskDialog) and shell32 (drag and drop) load on first use only, so
# that they do not cost startup time.
target_link_options(geditor PRIVATE /DELAYLOAD:comctl32.dll /DELAYLOAD:shell32.dll)

enable_testing()
add_subdirectory(tests)
