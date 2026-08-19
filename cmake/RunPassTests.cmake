# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

file(GLOB TESTS "${TEST_DIR}/*.ux")
foreach(T ${TESTS})
  get_filename_component(N ${T} NAME_WE)
  execute_process(COMMAND "${UINXC}" "${T}" --emit=obj -o "${CMAKE_CURRENT_BINARY_DIR}/${N}.o" RESULT_VARIABLE R)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "compile-pass test failed: ${T}")
  endif()
endforeach()
