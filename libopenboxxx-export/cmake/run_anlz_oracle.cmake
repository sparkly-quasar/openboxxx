# SPDX-License-Identifier: GPL-2.0-or-later
# Tier-2 ANLZ oracle test driver: generate a USB image with the CLI, then verify
# the ANLZ round-trips through pyrekordbox. Invoked by ctest (see CMakeLists.txt).
# Required -D vars: CLI, PYTHON3, SCRIPT, WORKDIR.

file(REMOVE_RECURSE "${WORKDIR}")
execute_process(COMMAND "${CLI}" --out "${WORKDIR}" RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "openboxxx_export_cli failed (${r})")
endif()

file(GLOB_RECURSE DATS "${WORKDIR}/*.DAT")
if(NOT DATS)
  message(FATAL_ERROR "no ANLZ .DAT produced under ${WORKDIR}")
endif()
list(GET DATS 0 DAT)

execute_process(COMMAND "${PYTHON3}" "${SCRIPT}" "${DAT}" RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "ANLZ oracle verification failed (${r})")
endif()
