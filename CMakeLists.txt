cmake_minimum_required(VERSION 3.21)

# Static CRT on MSVC: no VC++ Redistributable needed (doc §13.3). Must precede project().
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Boringshot VERSION 0.1.0 LANGUAGES CXX)

# Single-config generators default to Release.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BORINGSHOT_BUILD_TESTS "Build unit tests" ON)

add_library(bs_warnings INTERFACE)
if(MSVC)
  target_compile_options(bs_warnings INTERFACE /W4 /WX /utf-8 /permissive-)
else()
  target_compile_options(bs_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Werror)
endif()

add_library(boringshot_core STATIC
  core/src/mleader.cpp
  core/src/command_stack.cpp
  core/src/watermark.cpp)
target_include_directories(boringshot_core PUBLIC core/include)
target_link_libraries(boringshot_core PRIVATE bs_warnings)

if(BORINGSHOT_BUILD_TESTS)
  enable_testing()
  add_executable(boringshot_tests
    tests/test_main.cpp
    tests/test_mleader.cpp
    tests/test_command_stack.cpp
    tests/test_watermark.cpp)
  target_link_libraries(boringshot_tests PRIVATE boringshot_core bs_warnings)
  add_test(NAME core COMMAND boringshot_tests)
endif()

if(WIN32)
  add_executable(boringshot WIN32
    app/win/main.cpp
    app/win/watermark_overlay.cpp)
  target_compile_definitions(boringshot PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
    BORINGSHOT_VERSION="${PROJECT_VERSION}")
  target_link_libraries(boringshot PRIVATE boringshot_core bs_warnings
    d2d1 dwrite shell32 user32 gdi32 advapi32)
  if(BORINGSHOT_BUILD_TESTS)
    # Platform layer self-check: create overlay, render once, tear down.
    add_test(NAME win_smoke COMMAND boringshot --smoke)
  endif()

  install(TARGETS boringshot RUNTIME DESTINATION .)
  install(FILES LICENSE README.md DESTINATION . OPTIONAL)
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_FILE_NAME "Boringshot-${PROJECT_VERSION}-win64-portable")
  include(CPack)
endif()
