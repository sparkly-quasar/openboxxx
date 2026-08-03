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

# --- Phase 1 reader adapter: mixxxdb.sqlite -> ExportModel (needs SQLite) ---
# Optional: the core writer library has no SQLite dependency, so environments
# without SQLite still build everything above. When SQLite is present we also
# build the adapter library + the `openboxxx_from_mixxx` CLI a tester runs.
find_package(SQLite3)
if(SQLite3_FOUND)
  add_library(openboxxx_mixxxdb
    src/adapter/mixxxdb_reader.cpp
    src/adapter/mixxx_beats.cpp
  )
  target_include_directories(openboxxx_mixxxdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(openboxxx_mixxxdb PUBLIC openboxxx_export SQLite3::SQLite3)
  target_compile_options(openboxxx_mixxxdb PRIVATE -Wall -Wextra)

  add_executable(openboxxx_from_mixxx tools/openboxxx_from_mixxx.cpp)
  target_link_libraries(openboxxx_from_mixxx PRIVATE openboxxx_mixxxdb)

  add_executable(openboxxx_mixxxdb_tests tests/mixxxdb_tests.cpp)
  target_link_libraries(openboxxx_mixxxdb_tests PRIVATE openboxxx_mixxxdb SQLite3::SQLite3)
  target_include_directories(openboxxx_mixxxdb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME openboxxx_mixxxdb_tests COMMAND openboxxx_mixxxdb_tests)
else()
  message(STATUS "SQLite3 not found; skipping mixxxdb reader adapter + CLI")
endif()

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

  # Tier-1 PDB round-trip also needs kaitai-struct-compiler + the kaitaistruct
  # runtime (to compile third_party/crate-digger/rekordbox_pdb.ksy at test time).
  find_program(_ksc kaitai-struct-compiler)
  execute_process(COMMAND "${_oracle_py}" -c "import kaitaistruct"
                  RESULT_VARIABLE _has_ks OUTPUT_QUIET ERROR_QUIET)
  if(_ksc AND _has_ks EQUAL 0)
    add_test(NAME pdb_roundtrip COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:openboxxx_export_cli>
      -DPYTHON3=${_oracle_py}
      -DKSC=${_ksc}
      -DKSY=${CMAKE_CURRENT_SOURCE_DIR}/third_party/crate-digger/rekordbox_pdb.ksy
      -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_pdb.py
      -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/pdbrt
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_pdb_roundtrip.cmake)
  else()
    message(STATUS "kaitai-struct-compiler or kaitaistruct missing; skipping pdb_roundtrip test")
  endif()
endif()
