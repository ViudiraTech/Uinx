// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/compiler.hpp"
#include "uinx/lexer.hpp"
#include "uinx/parser.hpp"
#include "uinx/types.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string_view>

using namespace uinx;

namespace {

bool require(bool condition, std::string_view message) {
    if (condition)
        return true;
    std::cerr << "compiler unit failure: " << message << '\n';
    return false;
}

bool contains_token(const std::vector<Token>& tokens, TokenKind kind) {
    return std::any_of(
        tokens.begin(), tokens.end(), [kind](const Token& token) { return token.kind == kind; });
}

} // namespace

int main() {
    constexpr std::string_view source = R"(dontneed std
need core

func identity[T: Copy](value: T) -> T:
    return value

func main() -> i32:
    val answer = identity[i32](40 + 2)
    return answer - 42
)";

    std::ostringstream diagnostics_text;
    Diagnostics diagnostics(&diagnostics_text);
    Lexer lexer("compiler_unit.ux", source, diagnostics);
    auto tokens = lexer.lex();

    if (!require(!diagnostics.has_errors(), diagnostics_text.str()))
        return 1;
    if (!require(contains_token(tokens, TokenKind::Indent), "lexer did not emit Indent"))
        return 1;
    if (!require(contains_token(tokens, TokenKind::Dedent), "lexer did not emit Dedent"))
        return 1;
    if (!require(contains_token(tokens, TokenKind::KwNeed), "lexer did not recognize need"))
        return 1;
    if (!require(contains_token(tokens, TokenKind::KwDontNeed), "lexer did not recognize dontneed"))
        return 1;

    Parser parser(std::move(tokens), diagnostics);
    auto module = parser.parse_module("compiler_unit.ux");
    if (!require(!diagnostics.has_errors(), diagnostics_text.str()))
        return 1;
    if (!require(module.items.size() == 2, "parser did not produce two functions"))
        return 1;
    if (!require(module.no_std, "dontneed std did not set no_std semantics"))
        return 1;
    if (!require(module.needs == std::vector<std::string>{"core"}, "need core was not recorded"))
        return 1;
    if (!require(module.dontneeds == std::vector<std::string>{"std"},
                 "dontneed std was not recorded"))
        return 1;

    if (!require(Type::builtin("i32").str() == "i32",
                 "builtin type rendering changed unexpectedly"))
        return 1;
    if (!require(Type::builtin("u64").is_copy(), "u64 must remain Copy"))
        return 1;
    if (!require(Type::ref(Type::builtin("i32"), true).str() == "mutref i32",
                 "reference diagnostics must use canonical Uinx syntax")) {
        return 1;
    }

    {
        constexpr std::string_view smp_source = R"(dontneed std
smp auto

static var counter: u64 = 0
percpu var local_hits: u64 = 0

func helper() -> unit:
    counter += 1
    local_hits += 1
    return

func observer() -> u64:
    return counter

concurrent func irq_entry() -> u64:
    helper()
    return counter
)";
        std::ostringstream smp_diagnostics;
        CompileOptions options;
        options.emit = EmitKind::LLVMIR;
        options.opt_level = 2;
        Compiler compiler(&smp_diagnostics);
        const auto result =
            compiler.compile_source("smp_unit.ux", std::string(smp_source), options);
        if (!require(result.success, smp_diagnostics.str()))
            return 1;
        if (!require(result.llvm_ir.find("atomicrmw add") != std::string::npos,
                     "smp auto did not lower shared concurrent RMW atomically"))
            return 1;
        if (!require(result.llvm_ir.find("load atomic") != std::string::npos,
                     "smp auto did not lower shared concurrent load atomically"))
            return 1;
        const auto observer = result.llvm_ir.find("define i64 @observer");
        const auto observer_end =
            observer == std::string::npos ? std::string::npos : result.llvm_ir.find("}", observer);
        if (!require(
                observer != std::string::npos && observer_end != std::string::npos &&
                    result.llvm_ir.substr(observer, observer_end - observer).find("load atomic") !=
                        std::string::npos,
                "auto-shared global was not atomic in a non-concurrent observer"))
            return 1;
        if (!require(result.llvm_ir.find("@local_hits = thread_local(localexec) global") !=
                         std::string::npos,
                     "percpu did not lower to target TLS/per-CPU storage"))
            return 1;
    }

    {
        constexpr std::string_view manual_source = R"(dontneed std
smp manual

static var counter: u64 = 0

concurrent func bump() -> u64:
    counter += 1
    return counter
)";
        std::ostringstream manual_diagnostics;
        CompileOptions options;
        options.emit = EmitKind::LLVMIR;
        Compiler compiler(&manual_diagnostics);
        const auto result =
            compiler.compile_source("smp_manual.ux", std::string(manual_source), options);
        if (!require(result.success, manual_diagnostics.str()))
            return 1;
        if (!require(result.llvm_ir.find("atomicrmw") == std::string::npos &&
                         result.llvm_ir.find("load atomic") == std::string::npos,
                     "smp manual must not implicitly strengthen ordinary globals"))
            return 1;
    }

    {
        constexpr std::string_view strict_source = R"(dontneed std
smp strict

shared var counter: u64 = 0

concurrent func bump() -> u64:
    counter += 1
    fence seq_cst
    return counter
)";
        std::ostringstream strict_diagnostics;
        CompileOptions options;
        options.emit = EmitKind::LLVMIR;
        Compiler compiler(&strict_diagnostics);
        const auto result =
            compiler.compile_source("smp_strict.ux", std::string(strict_source), options);
        if (!require(result.success, strict_diagnostics.str()))
            return 1;
        if (!require(result.llvm_ir.find("atomicrmw add") != std::string::npos &&
                         result.llvm_ir.find("seq_cst") != std::string::npos,
                     "smp strict must use sequentially-consistent atomic ordering"))
            return 1;
        if (!require(result.llvm_ir.find("fence seq_cst") != std::string::npos,
                     "explicit fence was not emitted"))
            return 1;
    }

    {
        constexpr std::string_view optimization_source = R"(func calc(a: i32) -> i32:
    val one: i32 = 1
    val two: i32 = 2
    val sum: i32 = one + two
    var local: i32 = a
    val copy: i32 = local
    return copy + sum
)";
        CompileOptions o0;
        o0.emit = EmitKind::LLVMIR;
        o0.opt_level = 0;
        o0.verify_ir = false;
        o0.output = std::filesystem::temp_directory_path() / "uinx-unit-o0.ll";
        CompileOptions o2 = o0;
        o2.opt_level = 2;
        o2.output = std::filesystem::temp_directory_path() / "uinx-unit-o2.ll";
        std::ostringstream d0;
        std::ostringstream d2;
        Compiler c0(&d0);
        Compiler c2(&d2);
        const auto r0 = c0.compile_source("optimizer_o0.ux", std::string(optimization_source), o0);
        const auto r2 = c2.compile_source("optimizer_o2.ux", std::string(optimization_source), o2);
        if (!require(r0.success, d0.str()) || !require(r2.success, d2.str()))
            return 1;
        const auto count_text = [](std::string_view text, std::string_view needle) {
            std::size_t count = 0;
            for (std::size_t pos = 0; (pos = text.find(needle, pos)) != std::string_view::npos;
                 pos += needle.size())
                ++count;
            return count;
        };
        if (!require(count_text(r2.llvm_ir, "load i32") < count_text(r0.llvm_ir, "load i32"),
                     "O2 MIR optimization did not eliminate redundant local loads"))
            return 1;
        if (!require(r2.llvm_ir.find("add i32 %arg.a, 3") != std::string::npos,
                     "O2 MIR optimization did not fold and propagate constants"))
            return 1;
    }

    return 0;
}
