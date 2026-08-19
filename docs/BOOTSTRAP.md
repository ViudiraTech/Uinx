# Uinx Bootstrap and Self-Hosting Contract

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

Self-hosting is a reproducibility property, not a command name. A wrapper that invokes the existing C++ compiler does not make Uinx self-hosted.

## Current status

The canonical compiler in this repository is C++20 and produces native code through LLVM. Uinx itself has general control flow, mutable state, functions/recursion, generics, traits, ownership, raw-pointer/unsafe facilities, C FFI, and freestanding support, so the language surface is capable of expressing compiler-style programs.

A full compiler implementation written in Uinx is **not present in this source tree**, so Uinx 0.3 is not self-hosted.

## Required bootstrap stages

A release may call itself self-hosted only after all of these stages exist and are reproducible:

1. **stage0** — the trusted C++ compiler builds successfully from a clean tree.
2. **stage1 source** — a complete Uinx compiler implementation exists in Uinx source, including lexer, parser, name resolution, type/trait checking, ownership/borrow checking, MIR, code generation or a defined backend interface, diagnostics, and package-facing entry points.
3. **stage1 build** — stage0 compiles that Uinx compiler into a native stage1 compiler.
4. **stage2 build** — stage1 recompiles the same Uinx compiler source into stage2 without falling back to the C++ front end.
5. **semantic parity** — stage1/stage2 pass the same conformance, safety, codegen, and freestanding suites expected from stage0.
6. **reproducibility check** — stage1 and stage2 outputs are compared under a documented deterministic-build policy, with any permitted nondeterministic sections normalized and explained.
7. **bootstrapping documentation** — the exact trust root, LLVM/toolchain dependencies, commands, and artifact comparison procedure are documented and automated.

Until those criteria are satisfied, documentation should use “bootstrap target” rather than “self-hosted”.

## Why this boundary is strict

Compiler self-hosting exercises far more than Turing completeness. It requires enough standard-library/runtime surface for files, strings, collections, diagnostics, process/tool invocation, target descriptions, and build orchestration; it also requires the Uinx implementation to preserve the same safety and ABI rules as stage0. Treating a partial parser or a launcher as stage1 would make bootstrap claims impossible to audit.
