# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
project(libopenboxxx_export LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- the standalone writer library (no Qt / no Mixxx) ---
add_library(openboxxx_export
  src/exporter.cpp
  src/pdb/device_sql_string.cpp
  src/pdb/pdb_writer.cpp
  src/anlz/anlz_writer.cpp
  src/usb/usb_layout.cpp
  src/mapping/mapping.cpp
  src/verify/verify.cpp
  src/diag/diag.cpp
)
target_include_directories(openboxxx_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(openboxxx_export PRIVATE -Wall -Wextra)

# --- CLI harness (Phase 0 driver) ---
add_executable(openboxxx_export_cli tools/openboxxx_export.cpp)
target_link_libraries(openboxxx_export_cli PRIVATE openboxxx_export)

# --- tests ---
enable_testing()
add_executable(openboxxx_export_tests tests/tests.cpp)
target_link_libraries(openboxxx_export_tests PRIVATE openboxxx_export)
target_include_directories(openboxxx_export_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME openboxxx_export_tests COMMAND openboxxx_export_tests)
