// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Uinx Language

#include "uinx/compiler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace uinx {
namespace {

bool read_source_file(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return static_cast<bool>(file) || file.eof();
}

}  // namespace

std::string Compiler::shell_quote(std::string_view value) {
  std::string out = "'";
  for (const char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
  out += '\'';
  return out;
}

int Compiler::run_command(const std::vector<std::string>& args) {
  std::string command;
  for (const auto& arg : args) {
    if (!command.empty()) command += ' ';
    command += shell_quote(arg);
  }
  return std::system(command.c_str());
}

std::string Compiler::detect_host_triple(std::string_view clang) {
  const std::string command = shell_quote(clang) + " -dumpmachine 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) return "x86_64-unknown-linux-gnu";
  char buffer[256]{};
  std::string output;
  while (fgets(buffer, sizeof(buffer), pipe)) output += buffer;
  pclose(pipe);
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
  return output.empty() ? "x86_64-unknown-linux-gnu" : output;
}

CompileResult Compiler::compile_file(const std::filesystem::path& path, const CompileOptions& options) {
  return compile_files({path}, options);
}

CompileResult Compiler::compile_files(const std::vector<std::filesystem::path>& paths, const CompileOptions& options) {
  ast::Module merged;
  merged.file = paths.size() == 1 ? paths.front().string() : "<uinx-package>";
  if (paths.empty()) {
    SourceRange range;
    range.begin.file = merged.file;
    range.end = range.begin;
    diags_.error(range, "E0704", "no input source files");
    return {false, "", {}, diags_.all()};
  }

  for (const auto& path : paths) {
    std::string source;
    if (!read_source_file(path, source)) {
      SourceRange range;
      range.begin.file = path.string();
      range.end = range.begin;
      diags_.error(range, "E0700", "cannot open source file");
      continue;
    }
    Lexer lexer(path.string(), source, diags_);
    auto tokens = lexer.lex();
    if (diags_.has_errors()) continue;
    Parser parser(std::move(tokens), diags_);
    auto unit = parser.parse_module(path.string());
    if (unit.smp_mode != ast::SmpMode::Auto) {
      if (merged.smp_mode != ast::SmpMode::Auto && merged.smp_mode != unit.smp_mode) {
        SourceRange range; range.begin.file = path.string(); range.end = range.begin;
        diags_.error(range, "E0710", "conflicting smp modes across compilation units");
      } else {
        merged.smp_mode = unit.smp_mode;
      }
    }
    merged.no_std = merged.no_std || unit.no_std;
    for (auto& need : unit.needs) {
      if (std::find(merged.needs.begin(), merged.needs.end(), need) == merged.needs.end())
        merged.needs.push_back(std::move(need));
    }
    for (auto& disabled : unit.dontneeds) {
      if (std::find(merged.dontneeds.begin(), merged.dontneeds.end(), disabled) == merged.dontneeds.end())
        merged.dontneeds.push_back(std::move(disabled));
    }
    for (auto& item : unit.items) merged.items.emplace_back(std::move(item));
  }
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};
  const std::string logical_file = merged.file;
  return compile_module(std::move(merged), logical_file, options);
}

CompileResult Compiler::compile_source(std::string file, std::string source, const CompileOptions& options) {
  Lexer lexer(file, source, diags_);
  auto tokens = lexer.lex();
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};
  Parser parser(std::move(tokens), diags_);
  auto module = parser.parse_module(file);
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};
  return compile_module(std::move(module), std::move(file), options);
}

CompileResult Compiler::compile_module(ast::Module module, std::string logical_file, const CompileOptions& options) {
  if (options.smp_mode_override) module.smp_mode = *options.smp_mode_override;
  NameResolver resolver(diags_);
  auto hir = resolver.resolve(module);
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};

  TypeChecker checker(diags_, std::move(hir));
  auto semantics = checker.check(module);
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};

  BorrowChecker borrow_checker(diags_, semantics);
  borrow_checker.check(module);
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};
  if (options.emit == EmitKind::Check) return {true, "", {}, diags_.all()};

  MIRLowerer lowerer(diags_, semantics);
  auto mir = lowerer.lower(module);
  if (diags_.has_errors()) return {false, "", {}, diags_.all()};
  MIROptimizer(options.opt_level).run(mir);

  const std::string triple = options.target_triple.empty() ? detect_host_triple(options.clang) : options.target_triple;
  LLVMCodegen codegen(diags_, TargetInfo::from_triple(triple));
  std::string ir = codegen.emit(mir);
  if (diags_.has_errors()) return {false, ir, {}, diags_.all()};

  std::filesystem::path output = options.output;
  if (output.empty()) {
    output = std::filesystem::path(logical_file).filename();
    if (options.emit == EmitKind::LLVMIR) output.replace_extension(".ll");
    else if (options.emit == EmitKind::Object) output.replace_extension(".o");
    else output = "a.out";
  }

  if (options.emit == EmitKind::LLVMIR) {
    std::ofstream file(output, std::ios::binary);
    file << ir;
    if (!file) {
      SourceRange range;
      range.begin.file = output.string();
      range.end = range.begin;
      diags_.error(range, "E0701", "failed to write LLVM IR output");
      return {false, ir, output, diags_.all()};
    }
    if (options.verify_ir) {
      CompileOptions verify = options;
      verify.emit = EmitKind::Object;
      verify.output = std::filesystem::temp_directory_path() / "uinx-verify.o";
      const bool valid = backend_compile(ir, verify, verify.output);
      std::error_code error;
      std::filesystem::remove(verify.output, error);
      if (!valid) return {false, ir, output, diags_.all()};
    }
    return {true, ir, output, diags_.all()};
  }

  const bool success = backend_compile(ir, options, output);
  return {success, ir, output, diags_.all()};
}

bool Compiler::backend_compile(const std::string& ir, const CompileOptions& options,
                               const std::filesystem::path& output) {
  static std::uint64_t sequence = 0;
  const auto id = std::to_string(++sequence);
  const auto temp_dir = std::filesystem::temp_directory_path();
  const auto ir_file = temp_dir / ("uinx-" + id + ".ll");
  const auto object_file = temp_dir / ("uinx-" + id + ".o");
  {
    std::ofstream file(ir_file, std::ios::binary);
    file << ir;
    if (!file) {
      SourceRange range;
      range.begin.file = ir_file.string();
      range.end = range.begin;
      diags_.error(range, "E0702", "unable to create temporary LLVM IR");
      return false;
    }
  }

  const std::string triple = options.target_triple.empty() ? detect_host_triple(options.clang) : options.target_triple;
  const auto cleanup = [&] {
    if (options.keep_temps) return;
    std::error_code error;
    std::filesystem::remove(ir_file, error);
    std::filesystem::remove(object_file, error);
  };
  const auto fail = [&](std::string phase, int status) {
    cleanup();
    SourceRange range;
    range.begin.file = output.string();
    range.end = range.begin;
    diags_.error(range, "E0703", std::move(phase) + " failed with status " + std::to_string(status));
    return false;
  };

  const std::filesystem::path compile_output = options.emit == EmitKind::Object ? output : object_file;
  std::vector<std::string> compile_args{
      options.clang,
      "--target=" + triple,
      "-x",
      "ir",
      "-O" + std::to_string(std::clamp(options.opt_level, 0, 3))};
  if (!options.code_model.empty()) compile_args.push_back("-mcmodel=" + options.code_model);
  compile_args.insert(compile_args.end(), {"-c", ir_file.string(), "-o", compile_output.string()});
  const int compile_status = run_command(compile_args);
  if (compile_status != 0) return fail("LLVM IR to object compilation", compile_status);

  if (options.emit == EmitKind::Object) {
    cleanup();
    return true;
  }

  std::vector<std::string> link_args{
      options.clang,
      "--target=" + triple,
      object_file.string(),
      "-o",
      output.string()};
  if (options.freestanding) {
    link_args.push_back("-nostdlib");
    link_args.push_back("-fuse-ld=lld");
  }
  for (const auto& arg : options.linker_args) link_args.push_back(arg);
  const int link_status = run_command(link_args);
  if (link_status != 0) return fail("object linking", link_status);
  cleanup();
  return true;
}

}  // namespace uinx
