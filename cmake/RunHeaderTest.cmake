# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

execute_process(
  COMMAND "${UINXC}" "${SOURCE}" -enable-header -I "${INCLUDE_DIR}" --emit=exe -o "${OUTPUT}"
  RESULT_VARIABLE CR
  OUTPUT_VARIABLE CO
  ERROR_VARIABLE CE
)
if(NOT CR EQUAL 0)
  message(FATAL_ERROR "header compile failed for ${SOURCE}:\n${CO}\n${CE}")
endif()
execute_process(COMMAND "${OUTPUT}" RESULT_VARIABLE RR OUTPUT_VARIABLE RO ERROR_VARIABLE RE)
if(NOT RR EQUAL 0)
  message(FATAL_ERROR "header program returned ${RR}:\n${RO}\n${RE}")
endif()
