// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Uinx Language

#include "uinx/compiler.hpp"

#include <iostream>

using namespace uinx;

namespace {
void usage() {
    std::cout << "uinxc <file.ux> [more.ux ...] [-o output] [--emit=check|llvm-ir|obj|exe] "
                 "[-O0..3] [--target=TRIPLE] [--clang=PATH] [--smp=auto|manual|strict] "
                 "[--freestanding] [--repair] [--keep-temps] [--no-verify-ir]\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    CompileOptions options;
    std::vector<std::filesystem::path> inputs;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        }
        if (arg == "-o" && i + 1 < argc) {
            options.output = argv[++i];
            continue;
        }
        if (arg.rfind("--emit=", 0) == 0) {
            const auto value = arg.substr(7);
            if (value == "check")
                options.emit = EmitKind::Check;
            else if (value == "llvm-ir")
                options.emit = EmitKind::LLVMIR;
            else if (value == "obj")
                options.emit = EmitKind::Object;
            else if (value == "exe")
                options.emit = EmitKind::Executable;
            else {
                std::cerr << "unknown emit kind: " << value << '\n';
                return 2;
            }
            continue;
        }
        if (arg.size() == 3 && arg[0] == '-' && arg[1] == 'O' && arg[2] >= '0' && arg[2] <= '3') {
            options.opt_level = arg[2] - '0';
            continue;
        }
        if (arg.rfind("--target=", 0) == 0) {
            options.target_triple = arg.substr(9);
            continue;
        }
        if (arg.rfind("--clang=", 0) == 0) {
            options.clang = arg.substr(8);
            continue;
        }
        if (arg.rfind("--smp=", 0) == 0) {
            const auto value = arg.substr(6);
            if (value == "auto")
                options.smp_mode_override = ast::SmpMode::Auto;
            else if (value == "manual")
                options.smp_mode_override = ast::SmpMode::Manual;
            else if (value == "strict")
                options.smp_mode_override = ast::SmpMode::Strict;
            else {
                std::cerr << "unknown SMP mode: " << value << '\n';
                return 2;
            }
            continue;
        }
        if (arg == "--freestanding") {
            options.freestanding = true;
            continue;
        }
        if (arg == "--repair" || arg == "--fix") {
            options.auto_repair = true;
            continue;
        }
        if (arg == "--keep-temps") {
            options.keep_temps = true;
            continue;
        }
        if (arg == "--no-verify-ir") {
            options.verify_ir = false;
            continue;
        }
        if (arg.rfind("-Wl,", 0) == 0) {
            options.linker_args.push_back(arg);
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << '\n';
            return 2;
        }
        inputs.emplace_back(arg);
    }
    if (inputs.empty()) {
        usage();
        return 2;
    }
    if (options.emit == EmitKind::Check)
        options.output.clear();
    Compiler compiler(&std::cerr);
    const auto result = compiler.compile_files(inputs, options);
    if (!result.success)
        return 1;
    if (options.emit != EmitKind::Check)
        std::cout << result.output.string() << '\n';
    return 0;
}
