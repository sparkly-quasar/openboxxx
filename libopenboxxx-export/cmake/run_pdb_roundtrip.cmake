# SPDX-License-Identifier: GPL-2.0-or-later
# Tier-1 PDB round-trip test driver: generate a USB image with the CLI, compile
# the crate-digger .ksy to a throwaway Python parser, then verify export.pdb
# parses back. Invoked by ctest. Required -D vars: CLI, PYTHON3, KSC, KSY,
# SCRIPT, WORKDIR.

file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(COMMAND "${CLI}" --out "${WORKDIR}/usb" RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "openboxxx_export_cli failed (${r})")
endif()

# Generate the Python parser from the authoritative spec (test-time only).
execute_process(COMMAND "${KSC}" -t python --outdir "${WORKDIR}" "${KSY}" RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "kaitai-struct-compiler failed (${r})")
endif()

set(ENV{PYTHONPATH} "${WORKDIR}")
execute_process(COMMAND "${PYTHON3}" "${SCRIPT}" "${WORKDIR}/usb/PIONEER/rekordbox/export.pdb"
                RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "PDB round-trip verification failed (${r})")
endif()
