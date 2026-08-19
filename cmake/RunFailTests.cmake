# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

file(GLOB TESTS "${TEST_DIR}/*.ux")
list(SORT TESTS)
foreach(T ${TESTS})
  file(READ "${T}" SOURCE)
  string(REGEX MATCH "EXPECT-DIAGNOSTIC:[ \t]*(E[0-9]+)" _ "${SOURCE}")
  if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "negative test has no EXPECT-DIAGNOSTIC contract: ${T}")
  endif()
  set(EXPECTED "${CMAKE_MATCH_1}")
  execute_process(
    COMMAND "${UINXC}" "${T}" --emit=check
    RESULT_VARIABLE R
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR
  )
  if(R EQUAL 0)
    message(FATAL_ERROR "negative test unexpectedly succeeded: ${T}")
  endif()
  string(FIND "${STDERR}" "error[${EXPECTED}]" POS)
  if(POS EQUAL -1)
    message(FATAL_ERROR "${T}: expected diagnostic ${EXPECTED}, got:\n${STDERR}")
  endif()
endforeach()
