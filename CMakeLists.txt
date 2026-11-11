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
  src/core/editor.cpp
  src/core/encoding.cpp
  src/core/files.cpp
  src/core/key.cpp
  src/core/keymap.cpp
  src/core/layout.cpp
  src/core/minibuffer.cpp
  src/core/path.cpp
  src/core/piece_tree.cpp
  src/core/undo.cpp
  src/core/unicode.cpp)
target_include_directories(geditor_core PUBLIC src/core)
target_link_libraries(geditor_core PRIVATE geditor_options)

add_executable(geditor WIN32 src/win/main.cpp)
target_link_libraries(geditor PRIVATE geditor_options d2d1 dwrite)

enable_testing()
add_subdirectory(tests)
