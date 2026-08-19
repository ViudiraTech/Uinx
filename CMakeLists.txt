# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 ViudiraTech
# By JiTianYu391

cmake_minimum_required(VERSION 3.20)
project(UinxLanguage VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UINX_BUILD_TESTS "Build Uinx compiler tests" ON)
option(UINX_BUILD_FUZZ "Build libFuzzer targets when supported" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_library(uinx_compiler
  src/diagnostic.cpp
  src/lexer.cpp
  src/parser.cpp
  src/types.cpp
  src/sema.cpp
  src/borrow.cpp
  src/mir.cpp
  src/codegen.cpp
  src/compiler.cpp
)
target_include_directories(uinx_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(uinxc tools/uinxc.cpp)
target_link_libraries(uinxc PRIVATE uinx_compiler)

add_executable(uinx tools/uinx.cpp)
target_link_libraries(uinx PRIVATE uinx_compiler)
target_compile_definitions(uinx PRIVATE UINX_SOURCE_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib" UINX_SOURCE_RUNTIME_LIB="$<TARGET_FILE:uinx_runtime>")

add_executable(uinx-lsp tools/uinx_lsp.cpp)
target_link_libraries(uinx-lsp PRIVATE uinx_compiler)

add_library(uinx_runtime STATIC runtime/runtime.c runtime/collections.c runtime/unicode.c runtime/sync.c runtime/process.c)
target_include_directories(uinx_runtime PUBLIC runtime)

install(TARGETS uinxc uinx uinx-lsp uinx_runtime RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(DIRECTORY stdlib/ DESTINATION lib/uinx/stdlib)
install(DIRECTORY include/uinx/ DESTINATION include/uinx)
install(FILES runtime/runtime.h DESTINATION include/uinx/runtime)

add_executable(uinx-compiler-bench benchmarks/compiler_bench.cpp)
target_link_libraries(uinx-compiler-bench PRIVATE uinx_compiler)

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-18 clang-format-17)
if(CLANG_FORMAT_EXECUTABLE)
  file(GLOB_RECURSE UINX_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.cpp)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${UINX_FORMAT_SOURCES}
    COMMENT "Formatting Uinx C/C++ sources")
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${UINX_FORMAT_SOURCES}
    COMMENT "Checking Uinx C/C++ formatting")
endif()

if(UINX_BUILD_TESTS)
  enable_testing()
  add_executable(uinx-unit-tests tests/unit/compiler_unit.cpp)
  target_link_libraries(uinx-unit-tests PRIVATE uinx_compiler)
  add_test(NAME unit.compiler COMMAND uinx-unit-tests)
  add_executable(uinx-runtime-tests tests/unit/runtime_unit.c)
  target_link_libraries(uinx-runtime-tests PRIVATE uinx_runtime)
  add_test(NAME unit.runtime COMMAND uinx-runtime-tests)
  add_test(NAME codegen.asm-object COMMAND $<TARGET_FILE:uinxc> ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/asm/x86_64_nop.ux --emit=obj -o ${CMAKE_CURRENT_BINARY_DIR}/asm-test.o)
  add_test(NAME codegen.asm-input-output COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/asm/x86_64_io.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/asm-io -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutableTest.cmake)
  add_test(NAME codegen.asm-inout COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/asm/x86_64_inout.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/asm-inout -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutableTest.cmake)
  add_test(NAME codegen.asm-multiple-outputs COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/asm/x86_64_multiout.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/asm-multiout -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutableTest.cmake)
  add_test(NAME runtime.slice-index-bounds COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/pass/slice_index.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/slice-index -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutableTest.cmake)
  add_test(NAME runtime.slice-index-oob-traps COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/pass/slice_oob.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/slice-oob -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExpectedRuntimeFailure.cmake)
  add_test(NAME runtime.c-ffi COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c_ffi.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/c-ffi -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunExecutableTest.cmake)
  add_test(NAME codegen.cross-target-objects COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/freestanding.ux -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCrossTargetTests.cmake)
  add_test(NAME codegen.asm-cross-targets COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/asm/cross_nop.ux -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAsmCrossTargets.cmake)
  add_test(NAME codegen.asm-invalid-target-register COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/asm/invalid_aarch64_reg.ux -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/asm-invalid.o -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInvalidAsmConstraint.cmake)
  add_test(NAME codegen.baremetal-cross-link COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DCLANG=clang -DLD_LLD=ld.lld -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/baremetal -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBareMetalTests.cmake)
  add_test(NAME runtime.async-suspend-resume COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/async/suspend_resume.ux -DUINX_OBJ=${CMAKE_CURRENT_BINARY_DIR}/async-test.o -DCLANG=clang -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime -DHARNESS=${CMAKE_CURRENT_SOURCE_DIR}/tests/support/async_harness.c -DRUNTIME_LIB=$<TARGET_FILE:uinx_runtime> -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/async-test -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAsyncTest.cmake)
  add_test(NAME conformance.positive COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/pass -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPassTests.cmake)
  add_test(NAME conformance.negative COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/fail -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailTests.cmake)
  add_test(NAME safety.borrow-negative COMMAND ${CMAKE_COMMAND} -DUINXC=$<TARGET_FILE:uinxc> -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/safety/borrow-fail -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailTests.cmake)
  add_test(NAME tooling.package-integration COMMAND ${CMAKE_COMMAND} -DUINX=$<TARGET_FILE:uinx> -DWORK=${CMAKE_CURRENT_BINARY_DIR}/package-tooling -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageToolingTest.cmake)
endif()

if(UINX_BUILD_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_executable(uinx-lexer-fuzz fuzz/lexer_fuzz.cpp)
  target_link_libraries(uinx-lexer-fuzz PRIVATE uinx_compiler)
  target_compile_options(uinx-lexer-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(uinx-lexer-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  add_executable(uinx-parser-fuzz fuzz/parser_fuzz.cpp)
  target_link_libraries(uinx-parser-fuzz PRIVATE uinx_compiler)
  target_compile_options(uinx-parser-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(uinx-parser-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
