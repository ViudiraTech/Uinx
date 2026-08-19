// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/borrow.hpp"
#include "uinx/codegen.hpp"
#include "uinx/lexer.hpp"
#include "uinx/parser.hpp"

namespace uinx {
enum class EmitKind { Check, LLVMIR, Object, Executable };
struct CompileOptions {
    EmitKind emit{EmitKind::Executable};
    std::filesystem::path output;
    std::string target_triple;
    std::string clang{"clang"};
    int opt_level{2};
    bool keep_temps{false};
    bool verify_ir{true};
    bool freestanding{false};
    std::vector<std::string> linker_args;
};
struct CompileResult {
    bool success{false};
    std::string llvm_ir;
    std::filesystem::path output;
    std::vector<Diagnostic> diagnostics;
};
class Compiler {
  public:
    explicit Compiler(std::ostream* diagnostics_stream = nullptr) : diags_(diagnostics_stream) {
    }
    CompileResult compile_file(const std::filesystem::path& path, const CompileOptions& options);
    CompileResult compile_files(const std::vector<std::filesystem::path>& paths,
                                const CompileOptions& options);
    CompileResult
    compile_source(std::string file, std::string source, const CompileOptions& options);
    static std::string detect_host_triple(std::string_view clang = "clang");

  private:
    CompileResult
    compile_module(ast::Module module, std::string logical_file, const CompileOptions& options);
    bool backend_compile(const std::string& ir,
                         const CompileOptions& options,
                         const std::filesystem::path& output);
    static std::string shell_quote(std::string_view s);
    static int run_command(const std::vector<std::string>& args);
    Diagnostics diags_;
};
} // namespace uinx
