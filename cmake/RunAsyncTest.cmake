# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

# Uinx Language — by JiTianYu391
execute_process(COMMAND "${UINXC}" "${SOURCE}" --emit=obj -o "${UINX_OBJ}" RESULT_VARIABLE CR OUTPUT_VARIABLE CO ERROR_VARIABLE CE)
if(NOT CR EQUAL 0)
  message(FATAL_ERROR "async Uinx object compile failed:\n${CO}\n${CE}")
endif()
execute_process(COMMAND "${CLANG}" -std=c11 -I "${RUNTIME_INCLUDE}" "${HARNESS}" "${UINX_OBJ}" "${RUNTIME_LIB}" -pthread -o "${OUTPUT}" RESULT_VARIABLE LR OUTPUT_VARIABLE LO ERROR_VARIABLE LE)
if(NOT LR EQUAL 0)
  message(FATAL_ERROR "async harness link failed:\n${LO}\n${LE}")
endif()
execute_process(COMMAND "${OUTPUT}" RESULT_VARIABLE RR OUTPUT_VARIABLE RO ERROR_VARIABLE RE)
if(NOT RR EQUAL 0)
  message(FATAL_ERROR "async suspend/resume test failed rc=${RR}:\n${RO}\n${RE}")
endif()
