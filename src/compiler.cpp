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
#include <unordered_set>

namespace uinx {
namespace {

#ifndef UINX_SOURCE_RUNTIME_LIB
    #define UINX_SOURCE_RUNTIME_LIB ""
#endif

bool read_source_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return static_cast<bool>(file) || file.eof();
}

struct SourceRepair {
    std::uint32_t offset{};
    std::string text;
    std::string description;
};

std::optional<SourceRepair> parser_repair(const Diagnostic& diagnostic) {
    if (diagnostic.code == "E0111")
        return SourceRepair{
            diagnostic.range.begin.offset, ";", "inserted a missing statement terminator"};

    if (diagnostic.code != "E0100")
        return std::nullopt;

    const bool at_boundary = diagnostic.message.find("found newline") != std::string::npos ||
                             diagnostic.message.find("found Newline") != std::string::npos ||
                             diagnostic.message.find("found dedent") != std::string::npos ||
                             diagnostic.message.find("found Dedent") != std::string::npos ||
                             diagnostic.message.find("found end of file") != std::string::npos ||
                             diagnostic.message.find("found End") != std::string::npos ||
                             diagnostic.message.find("found end") != std::string::npos;
    if (!at_boundary)
        return std::nullopt;

    const std::pair<std::string_view, std::string_view> delimiters[] = {
        {"expected ')'", ")"},
        {"expected ']'", "]"},
        {"expected '}'", "}"},
    };
    for (const auto& [expected, replacement] : delimiters) {
        if (diagnostic.message.find(expected) != std::string::npos)
            return SourceRepair{diagnostic.range.begin.offset,
                                std::string(replacement),
                                "inserted a missing closing delimiter"};
    }
    return std::nullopt;
}

bool repair_source(std::string_view file,
                   std::string& source,
                   Diagnostics& diagnostics,
                   std::size_t max_repairs = 32) {
    bool changed = false;
    for (std::size_t attempt = 0; attempt < max_repairs; ++attempt) {
        Diagnostics probe;
        Lexer lexer(std::string(file), source, probe);
        auto tokens = lexer.lex();
        if (probe.has_errors())
            break;
        Parser parser(std::move(tokens), probe);
        parser.parse_module(std::string(file));

        std::optional<SourceRepair> repair;
        SourceRange repair_range;
        for (const auto& diagnostic : probe.all()) {
            if (diagnostic.level != DiagLevel::Error)
                continue;
            auto candidate = parser_repair(diagnostic);
            if (!candidate || candidate->offset > source.size())
                continue;
            repair = std::move(candidate);
            repair_range = diagnostic.range;
            break;
        }
        if (!repair)
            break;

        source.insert(repair->offset, repair->text);
        diagnostics.note(repair_range,
                         "N0001",
                         "auto-repair: " + repair->description + " (source kept unchanged)");
        changed = true;
    }
    return changed;
}

bool ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

bool reserved_requirement(std::string_view name) {
    static const std::unordered_set<std::string_view> reserved{
        "std", "core", "alloc", "minimal", "runtime"};
    return reserved.contains(name);
}

bool same_function_signature(const ast::FunctionDecl& left, const ast::FunctionDecl& right) {
    if (left.name != right.name || left.return_type.str() != right.return_type.str() ||
        left.is_pub != right.is_pub || left.is_unsafe != right.is_unsafe ||
        left.is_async != right.is_async || left.is_extern != right.is_extern ||
        left.is_concurrent != right.is_concurrent || left.abi != right.abi ||
        left.generics.size() != right.generics.size() || left.params.size() != right.params.size())
        return false;
    for (std::size_t i = 0; i < left.generics.size(); ++i) {
        if (left.generics[i].name != right.generics[i].name ||
            left.generics[i].bounds != right.generics[i].bounds)
            return false;
    }
    for (std::size_t i = 0; i < left.params.size(); ++i)
        if (left.params[i].type.str() != right.params[i].type.str())
            return false;
    return true;
}

bool append_function(ast::Module& module, ast::FunctionDecl function, Diagnostics& diagnostics) {
    for (auto& item : module.items) {
        auto* previous = std::get_if<ast::FunctionDecl>(&item);
        if (!previous || previous->name != function.name)
            continue;
        if (!same_function_signature(*previous, function)) {
            diagnostics.error(function.range,
                              "E0714",
                              "conflicting declarations for function '" + function.name + "'");
            return false;
        }
        if (previous->body && function.body) {
            diagnostics.error(function.range,
                              "E0715",
                              "duplicate definition of function '" + function.name + "'");
            return false;
        }
        if (!previous->body && function.body)
            previous->body = std::move(function.body);
        return true;
    }
    module.items.emplace_back(std::move(function));
    return true;
}

void append_module_items(ast::Module& target, ast::Module source, Diagnostics& diagnostics) {
    for (auto& item : source.items) {
        if (auto* function = std::get_if<ast::FunctionDecl>(&item)) {
            append_function(target, std::move(*function), diagnostics);
            continue;
        }
        target.items.emplace_back(std::move(item));
    }
}

void merge_module_metadata(ast::Module& target,
                           const ast::Module& source,
                           const CompileOptions& options,
                           Diagnostics& diagnostics) {
    if (source.smp_mode != ast::SmpMode::Auto) {
        if (target.smp_mode != ast::SmpMode::Auto && target.smp_mode != source.smp_mode) {
            SourceRange range;
            range.begin.file = source.file;
            range.end = range.begin;
            diagnostics.error(range, "E0710", "conflicting smp modes across compilation units");
        } else {
            target.smp_mode = source.smp_mode;
        }
    }
    if (options.smp_mode_override)
        target.smp_mode = *options.smp_mode_override;
    target.no_std = target.no_std || source.no_std;
    for (const auto& need : source.needs)
        if (std::find(target.needs.begin(), target.needs.end(), need) == target.needs.end())
            target.needs.push_back(need);
    for (const auto& disabled : source.dontneeds)
        if (std::find(target.dontneeds.begin(), target.dontneeds.end(), disabled) ==
            target.dontneeds.end())
            target.dontneeds.push_back(disabled);
}

std::optional<std::filesystem::path> resolve_header(std::string name,
                                                    const std::filesystem::path& origin,
                                                    const CompileOptions& options) {
    std::vector<std::filesystem::path> directories;
    if (!origin.empty())
        directories.push_back(origin);
    directories.insert(directories.end(), options.include_dirs.begin(), options.include_dirs.end());

    std::vector<std::string> names{name};
    if (!ends_with(name, ".uxh"))
        names.push_back(name + ".uxh");
    for (const auto& directory : directories) {
        for (const auto& candidate_name : names) {
            std::error_code error;
            const auto candidate = directory / candidate_name;
            if (!std::filesystem::is_regular_file(candidate, error))
                continue;
            const auto canonical = std::filesystem::weakly_canonical(candidate, error);
            return error ? std::optional<std::filesystem::path>{}
                         : std::optional<std::filesystem::path>{canonical};
        }
    }
    return std::nullopt;
}

class ModuleLoader {
  public:
    ModuleLoader(const CompileOptions& options, Diagnostics& diagnostics)
        : options_(options), diagnostics_(diagnostics) {
    }

    bool load_file(const std::filesystem::path& path, ast::Module& output) {
        return load_file_impl(path, false, output);
    }

    bool load_source(std::string file, std::string source, ast::Module& output) {
        if (options_.auto_repair)
            repair_source(file, source, diagnostics_);
        const std::filesystem::path origin = std::filesystem::path(file).parent_path();
        return parse_and_merge(std::move(file), std::move(source), origin, output);
    }

  private:
    bool load_file_impl(const std::filesystem::path& path, bool is_header, ast::Module& output) {
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        const auto identity = error ? path.lexically_normal() : canonical;
        if (is_header && !loaded_headers_.insert(identity.string()).second)
            return true;

        std::string source;
        if (!read_source_file(path, source)) {
            SourceRange range;
            range.begin.file = path.string();
            range.end = range.begin;
            diagnostics_.error(range, "E0700", "cannot open source file");
            return false;
        }
        if (options_.auto_repair)
            repair_source(path.string(), source, diagnostics_);
        return parse_and_merge(path.string(), std::move(source), path.parent_path(), output);
    }

    bool parse_and_merge(std::string file,
                         std::string source,
                         const std::filesystem::path& origin,
                         ast::Module& output) {
        Lexer lexer(file, source, diagnostics_);
        auto tokens = lexer.lex();
        if (diagnostics_.has_errors())
            return false;
        Parser parser(std::move(tokens), diagnostics_);
        auto unit = parser.parse_module(file);
        if (diagnostics_.has_errors())
            return false;

        std::vector<std::string> component_needs;
        for (const auto& need : unit.needs) {
            const bool explicit_header = ends_with(need, ".uxh");
            const bool try_header =
                explicit_header || (options_.enable_header && !reserved_requirement(need));
            if (!try_header) {
                component_needs.push_back(need);
                continue;
            }
            if (!options_.enable_header) {
                SourceRange range;
                range.begin.file = file;
                range.end = range.begin;
                diagnostics_.error(range,
                                   "E0712",
                                   "header requirement '" + need +
                                       "' requires -enable-header and an -I search directory");
                continue;
            }
            auto header = resolve_header(need, origin, options_);
            if (!header) {
                if (explicit_header) {
                    SourceRange range;
                    range.begin.file = file;
                    range.end = range.begin;
                    diagnostics_.error(
                        range, "E0713", "cannot find header '" + need + "' in include paths");
                } else {
                    component_needs.push_back(need);
                }
                continue;
            }
            if (!load_file_impl(*header, true, output))
                return false;
        }
        unit.needs = std::move(component_needs);
        merge_module_metadata(output, unit, options_, diagnostics_);
        append_module_items(output, std::move(unit), diagnostics_);
        return !diagnostics_.has_errors();
    }

    const CompileOptions& options_;
    Diagnostics& diagnostics_;
    std::unordered_set<std::string> loaded_headers_;
};

} // namespace

std::string Compiler::shell_quote(std::string_view value) {
    std::string out = "'";
    for (const char c : value)
        out += c == '\'' ? "'\\''" : std::string(1, c);
    out += '\'';
    return out;
}

int Compiler::run_command(const std::vector<std::string>& args) {
    std::string command;
    for (const auto& arg : args) {
        if (!command.empty())
            command += ' ';
        command += shell_quote(arg);
    }
    return std::system(command.c_str());
}

std::string Compiler::detect_host_triple(std::string_view clang) {
    const std::string command = shell_quote(clang) + " -dumpmachine 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return "x86_64-unknown-linux-gnu";
    char buffer[256]{};
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    return output.empty() ? "x86_64-unknown-linux-gnu" : output;
}

CompileResult Compiler::compile_file(const std::filesystem::path& path,
                                     const CompileOptions& options) {
    return compile_files({path}, options);
}

CompileResult Compiler::compile_files(const std::vector<std::filesystem::path>& paths,
                                      const CompileOptions& options) {
    ast::Module merged;
    merged.file = paths.size() == 1 ? paths.front().string() : "<uinx-package>";
    if (paths.empty()) {
        SourceRange range;
        range.begin.file = merged.file;
        range.end = range.begin;
        diags_.error(range, "E0704", "no input source files");
        return {false, "", {}, diags_.all()};
    }

    ModuleLoader loader(options, diags_);
    for (const auto& path : paths)
        loader.load_file(path, merged);
    if (diags_.has_errors())
        return {false, "", {}, diags_.all()};
    const std::string logical_file = merged.file;
    return compile_module(std::move(merged), logical_file, options);
}

CompileResult
Compiler::compile_source(std::string file, std::string source, const CompileOptions& options) {
    ast::Module module;
    module.file = file;
    ModuleLoader loader(options, diags_);
    loader.load_source(std::move(file), std::move(source), module);
    if (diags_.has_errors())
        return {false, "", {}, diags_.all()};
    return compile_module(std::move(module), std::move(file), options);
}

CompileResult Compiler::compile_module(ast::Module module,
                                       std::string logical_file,
                                       const CompileOptions& options) {
    if (options.smp_mode_override)
        module.smp_mode = *options.smp_mode_override;
    NameResolver resolver(diags_);
    auto hir = resolver.resolve(module);
    if (diags_.has_errors())
        return {false, "", {}, diags_.all()};

    TypeChecker checker(diags_, std::move(hir));
    auto semantics = checker.check(module);
    if (diags_.has_errors())
        return {false, "", {}, diags_.all()};

    BorrowChecker borrow_checker(diags_, semantics);
    borrow_checker.check(module);
    if (diags_.has_errors())
        return {false, "", {}, diags_.all()};
    if (options.emit == EmitKind::Check)
        return {true, "", {}, diags_.all()};

    MIRLowerer lowerer(diags_, semantics);
    auto mir = lowerer.lower(module);
    if (diags_.has_errors())
        return {false, "", {}, diags_.all()};
    MIROptimizer(options.opt_level).run(mir);

    const std::string triple =
        options.target_triple.empty() ? detect_host_triple(options.clang) : options.target_triple;
    LLVMCodegen codegen(diags_, TargetInfo::from_triple(triple));
    std::string ir = codegen.emit(mir);
    if (diags_.has_errors())
        return {false, ir, {}, diags_.all()};

    std::filesystem::path output = options.output;
    if (output.empty()) {
        output = std::filesystem::path(logical_file).filename();
        if (options.emit == EmitKind::LLVMIR)
            output.replace_extension(".ll");
        else if (options.emit == EmitKind::Object)
            output.replace_extension(".o");
        else
            output = "a.out";
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
            if (!valid)
                return {false, ir, output, diags_.all()};
        }
        return {true, ir, output, diags_.all()};
    }

    const bool success = backend_compile(ir, options, output);
    return {success, ir, output, diags_.all()};
}

bool Compiler::backend_compile(const std::string& ir,
                               const CompileOptions& options,
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

    const std::string triple =
        options.target_triple.empty() ? detect_host_triple(options.clang) : options.target_triple;
    const auto cleanup = [&] {
        if (options.keep_temps)
            return;
        std::error_code error;
        std::filesystem::remove(ir_file, error);
        std::filesystem::remove(object_file, error);
    };
    const auto fail = [&](std::string phase, int status) {
        cleanup();
        SourceRange range;
        range.begin.file = output.string();
        range.end = range.begin;
        diags_.error(
            range, "E0703", std::move(phase) + " failed with status " + std::to_string(status));
        return false;
    };

    const std::filesystem::path compile_output =
        options.emit == EmitKind::Object ? output : object_file;
    std::vector<std::string> compile_args{options.clang,
                                          "--target=" + triple,
                                          "-x",
                                          "ir",
                                          "-O" +
                                              std::to_string(std::clamp(options.opt_level, 0, 3))};
    if (!options.code_model.empty())
        compile_args.push_back("-mcmodel=" + options.code_model);
    compile_args.insert(compile_args.end(),
                        {"-c", ir_file.string(), "-o", compile_output.string()});
    const int compile_status = run_command(compile_args);
    if (compile_status != 0)
        return fail("LLVM IR to object compilation", compile_status);

    if (options.emit == EmitKind::Object) {
        cleanup();
        return true;
    }

    std::vector<std::string> link_args{
        options.clang, "--target=" + triple, object_file.string(), "-o", output.string()};
    if (options.freestanding) {
        link_args.push_back("-nostdlib");
        link_args.push_back("-fuse-ld=lld");
    } else if (options.target_triple.empty()) {
        const std::filesystem::path runtime_library{UINX_SOURCE_RUNTIME_LIB};
        const bool already_linked = std::any_of(
            options.linker_args.begin(), options.linker_args.end(), [](const auto& arg) {
                return arg.find("libuinx_runtime") != std::string::npos;
            });
        std::error_code error;
        if (!already_linked && std::filesystem::is_regular_file(runtime_library, error))
            link_args.push_back(runtime_library.string());
    }
    for (const auto& arg : options.linker_args)
        link_args.push_back(arg);
    const int link_status = run_command(link_args);
    if (link_status != 0)
        return fail("object linking", link_status);
    cleanup();
    return true;
}

} // namespace uinx
