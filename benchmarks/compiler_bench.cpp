// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/compiler.hpp"
#include "uinx/lexer.hpp"
#include "uinx/parser.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
using bench_clock = std::chrono::steady_clock;
int main(int argc, char** argv) {
    int iterations = argc > 1 ? std::max(1, std::atoi(argv[1])) : 1000;
    std::string source = "fn add<T: Copy>(x: T) -> T { return x; } fn main() -> i32 { let a = "
                         "add<i32>(40); return a + 2 - 42; }";
    {
        auto begin = bench_clock::now();
        for (int i = 0; i < iterations; ++i) {
            std::ostringstream sink;
            uinx::Diagnostics d(&sink);
            uinx::Lexer l("bench.ux", source, d);
            (void)l.lex();
        }
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(bench_clock::now() - begin)
                      .count();
        std::cout << "lexer_ns_per_iter=" << (ns / iterations) << '\n';
    }
    {
        auto begin = bench_clock::now();
        for (int i = 0; i < iterations; ++i) {
            std::ostringstream sink;
            uinx::Diagnostics d(&sink);
            uinx::Lexer l("bench.ux", source, d);
            uinx::Parser p(l.lex(), d);
            (void)p.parse_module("bench.ux");
        }
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(bench_clock::now() - begin)
                      .count();
        std::cout << "parse_ns_per_iter=" << (ns / iterations) << '\n';
    }
    {
        int n = std::max(1, iterations / 20);
        auto begin = bench_clock::now();
        for (int i = 0; i < n; ++i) {
            uinx::Compiler c;
            uinx::CompileOptions o;
            o.emit = uinx::EmitKind::Check;
            auto r = c.compile_source("bench.ux", source, o);
            if (!r.success)
                return 1;
        }
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(bench_clock::now() - begin)
                      .count();
        std::cout << "check_pipeline_ns_per_iter=" << (ns / n) << '\n';
    }
    return 0;
}
