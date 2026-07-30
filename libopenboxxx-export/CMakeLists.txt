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

# --- optional tier-2 ANLZ oracle test (needs python3 + pyrekordbox) ---
# Point at a specific interpreter with -DOPENBOXXX_ORACLE_PYTHON=/path/to/python
# (e.g. a venv). Falls back to python3 on PATH. Registered only if pyrekordbox
# imports, so environments without it simply skip the test.
if(OPENBOXXX_ORACLE_PYTHON)
  set(_oracle_py "${OPENBOXXX_ORACLE_PYTHON}")
else()
  find_program(_oracle_py python3)
endif()
if(_oracle_py)
  execute_process(COMMAND "${_oracle_py}" -c "import pyrekordbox"
                  RESULT_VARIABLE _has_prb OUTPUT_QUIET ERROR_QUIET)
  if(_has_prb EQUAL 0)
    add_test(NAME anlz_oracle COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:openboxxx_export_cli>
      -DPYTHON3=${_oracle_py}
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_anlz.py
      -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/oracle
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_anlz_oracle.cmake)
  else()
    message(STATUS "pyrekordbox not importable via ${_oracle_py}; skipping anlz_oracle test")
  endif()
endif()
