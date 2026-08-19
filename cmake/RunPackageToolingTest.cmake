# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

# Uinx Language

if(NOT DEFINED UINX OR NOT DEFINED WORK)
  message(FATAL_ERROR "UINX and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_checked working_dir)
  execute_process(
    COMMAND ${ARGN}
    WORKING_DIRECTORY "${working_dir}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "command failed (${status}): ${ARGN}\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

function(run_expected_failure working_dir expected_text)
  execute_process(
    COMMAND ${ARGN}
    WORKING_DIRECTORY "${working_dir}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(status EQUAL 0)
    message(FATAL_ERROR "command unexpectedly succeeded: ${ARGN}")
  endif()
  string(CONCAT combined "${stdout}" "\n" "${stderr}")
  if(NOT combined MATCHES "${expected_text}")
    message(FATAL_ERROR "expected failure did not contain '${expected_text}':\n${combined}")
  endif()
endfunction()

run_checked("${WORK}" "${UINX}" new dep --lib)
file(WRITE "${WORK}/dep/src/lib.ux" "public func dep_answer() -> i32:\n    return 42\n")
run_checked("${WORK}" "${UINX}" new app)
file(WRITE "${WORK}/app/src/main.ux" "func main() -> i32:\n    return dep_answer() - 42\n")
run_checked("${WORK}/app" "${UINX}" add dep --path ../dep)
run_checked("${WORK}/app" "${UINX}" fetch)
run_checked("${WORK}/app" "${UINX}" check)
run_checked("${WORK}/app" "${UINX}" build)
run_checked("${WORK}/app" "${UINX}" run)
run_checked("${WORK}/app" "${UINX}" fmt)
run_checked("${WORK}/app" "${UINX}" fmt --check)
run_checked("${WORK}/app" "${UINX}" lint)
run_checked("${WORK}/app" "${UINX}" doc)
file(MAKE_DIRECTORY "${WORK}/app/tests")
file(WRITE "${WORK}/app/tests/basic.ux" "func main() -> i32:\n    return dep_answer() - 42\n")
run_checked("${WORK}/app" "${UINX}" test)

if(NOT EXISTS "${WORK}/app/uinx.lock" OR NOT EXISTS "${WORK}/app/target/debug/app" OR NOT EXISTS "${WORK}/app/target/doc/api.md")
  message(FATAL_ERROR "package tooling did not produce required artifacts")
endif()
file(READ "${WORK}/app/uinx.lock" lockfile)
if(NOT lockfile MATCHES "name = \\\"dep\\\"")
  message(FATAL_ERROR "dependency missing from lockfile")
endif()
# `need std` must override a no-std package manifest. If source selection is
# ignored, std_version_major remains unresolved and this check fails.
run_checked("${WORK}" "${UINX}" new need_std --freestanding)
file(WRITE "${WORK}/need_std/src/main.ux" "need std\n\nfunc main() -> i32:\n    return std_version_major()\n")
run_checked("${WORK}/need_std" "${UINX}" check)

# `dontneed std` must override the default full-std manifest. The explicit
# unresolved call gives this test an observable failure mode if std is removed.
run_checked("${WORK}" "${UINX}" new dontneed_std)
file(WRITE "${WORK}/dontneed_std/src/main.ux" "dontneed std\n\nfunc main() -> i32:\n    return std_version_major()\n")
run_expected_failure("${WORK}/dontneed_std" "E0204" "${UINX}" check)

# A disabled direct dependency must not be compiled. The dependency is
# intentionally invalid, so a successful app check proves exclusion happened.
run_checked("${WORK}" "${UINX}" new broken_dep --lib --freestanding)
file(WRITE "${WORK}/broken_dep/src/lib.ux" "public func broken(value: MissingType) -> i32:\n    return 1\n")
run_checked("${WORK}" "${UINX}" new dontneed_dep --freestanding)
run_checked("${WORK}/dontneed_dep" "${UINX}" add broken_dep --path ../broken_dep)
file(WRITE "${WORK}/dontneed_dep/src/main.ux" "dontneed std\ndontneed broken_dep\n\nfunc main() -> i32:\n    return 0\n")
run_checked("${WORK}/dontneed_dep" "${UINX}" check)


# Kernel packages must be genuinely freestanding and cross-linkable through the
# package tool, not just generated source templates.
foreach(ARCH IN ITEMS x86_64 aarch64 riscv64)
  set(KERNEL_NAME "kernel_${ARCH}")
  run_checked("${WORK}" "${UINX}" new "${KERNEL_NAME}" "--kernel=${ARCH}")
  run_checked("${WORK}/${KERNEL_NAME}" "${UINX}" check)
  run_checked("${WORK}/${KERNEL_NAME}" "${UINX}" build --release)
  if(NOT EXISTS "${WORK}/${KERNEL_NAME}/target/release/${KERNEL_NAME}.elf")
    message(FATAL_ERROR "kernel scaffold did not produce ${ARCH} ELF")
  endif()
endforeach()

# Command-line SMP policy override must work for package builds/checks too.
run_checked("${WORK}/kernel_x86_64" "${UINX}" check --smp=manual)
run_checked("${WORK}/kernel_x86_64" "${UINX}" check --smp=strict)
