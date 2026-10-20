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
option(UINX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(UINX_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
  if(UINX_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(UINX_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
  if(UINX_ENABLE_SANITIZERS)
    # C++ only: the C runtime is linked into programs built by uinxc, which are
    # not sanitized.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>
                        $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

add_library(uinx_compiler
  src/diagnostic.cpp
  src/lexer.cpp
  src/parser.cpp
  src/types.cpp
  src/sema.cpp
  src/sema_resolve.cpp
  src/sema_smp.cpp
  src/sema_stmt.cpp
  src/sema_expr.cpp
  src/borrow.cpp
  src/borrow_places.cpp
  src/borrow_loans.cpp
  src/borrow_liveness.cpp
  src/borrow_flow.cpp
  src/mir.cpp
  src/mir_stmt.cpp
  src/mir_expr.cpp
  src/mir_optimizer.cpp
  src/codegen.cpp
  src/codegen_asm.cpp
  src/codegen_async.cpp
  src/codegen_function.cpp
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
target_compile_definitions(uinx_compiler PRIVATE UINX_SOURCE_RUNTIME_LIB="$<TARGET_FILE:uinx_runtime>")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
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
  add_subdirectory(tests)
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
