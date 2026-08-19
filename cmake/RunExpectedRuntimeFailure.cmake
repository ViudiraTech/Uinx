# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

# Uinx Language
if(NOT DEFINED UINXC OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "UINXC, SOURCE and OUTPUT are required")
endif()
execute_process(COMMAND "${UINXC}" "${SOURCE}" --emit=exe -o "${OUTPUT}" RESULT_VARIABLE compile_status OUTPUT_VARIABLE compile_out ERROR_VARIABLE compile_err)
if(NOT compile_status EQUAL 0)
  message(FATAL_ERROR "compilation failed: ${compile_out}\n${compile_err}")
endif()
execute_process(COMMAND "${OUTPUT}" RESULT_VARIABLE run_status OUTPUT_VARIABLE run_out ERROR_VARIABLE run_err)
if(run_status EQUAL 0)
  message(FATAL_ERROR "program unexpectedly succeeded; bounds check did not stop out-of-range access")
endif()
