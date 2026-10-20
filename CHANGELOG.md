# Changelog

## Unreleased — ownership and language hardening

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

- Borrow checker: implicit reborrow when a `mutref` is passed to a `mutref` parameter, explicit `borrow [mut] deref r` reborrows (including nested and through reference parameters), and checking of writes through references (`deref r = v`) against live loans and moves. `consume(first); consume(first)` on a `mutref` is now accepted, as in Rust; `val second = first` still moves.
- Split `borrow.cpp`, `sema.cpp`, `codegen.cpp` and `mir.cpp` into focused translation units (no behavior change; LLVM IR for all examples and tests is byte-identical).
- SMP check: a local or parameter that shadows a mutable global is no longer reported as a global access (`E0363`), and the call-graph and shared-state walkers now share one AST traversal so they cannot drift apart.
- Fixed a heap-use-after-free in `uinx::Type` copy assignment (`t = *t.pointee`) found by AddressSanitizer.
- Repository hygiene: added `CMakePresets.json`, GitHub Actions CI (GCC, Clang, ASan/UBSan, clang-format), issue/PR templates, `CONTRIBUTING.md`, `.editorconfig` and `.gitattributes`; moved `ARCHITECTURE.md` and `BUILD.md` into `docs/`; moved test registration into `tests/CMakeLists.txt`; added `UINX_WARNINGS_AS_ERRORS` and `UINX_ENABLE_SANITIZERS` options; removed the stale `MANIFEST.sha256` and checked-in `verification/` transcripts.
- Reworked borrow checking around backward liveness plus forward control-flow fixed points instead of block-local statement numbering.
- Added HIR `SymbolId`-based place identity so shadowed locals cannot erase or inherit another binding's move/loan state.
- Added partial move/reinitialization tracking, branch/loop joins, break/continue/return reachability handling, field-level reference provenance, method-receiver provenance, aggregate escape checking, and conservative async suspension checks.
- Added direct-read rejection while an overlapping mutable borrow is active and fail-closed `E0408` handling for dataflow non-convergence.
- Added compiler validation for explicit `Copy` implementations, preventing `Copy` on non-copy fields, exclusive mutable references, and concrete `Drop` types.
- Added explicit `move expression` syntax and `where` clauses for function/struct generic bounds; concrete struct instantiations now enforce their bounds.
- Switched verification policy to disposable build/probe artifacts rather than checked-in test transcripts.
- Refreshed the README, memory model, language specification, release-status boundaries, and bootstrap criteria.

## Uinx 0.3.0 — 2026-08-19

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

- Added `const`, `static`, `for`, `loop`, `break`, `continue`, bitwise/shift operations and their compound assignments to the canonical indentation syntax.
- Added the OS SMP language model: `concurrent`, `shared`, `percpu`, `smp auto/manual/strict`, `fence`, and `compiler_fence`.
- Added call-graph propagation from concurrent entries and automatic promotion of compatible mutable globals/fields to atomic storage.
- Added LLVM atomic load/store/RMW/fence lowering with acquire/release/acq_rel automatic ordering and seq_cst strict mode.
- Added local-exec TLS lowering for `percpu` state.
- Added compiler-lowered freestanding volatile and atomic intrinsics plus pure-Uinx `memcpy`, `memmove`, and `memset`.
- Added a freestanding `SpinLock` and fixed typed raw-pointer arithmetic and raw-pointer dereference assignment.
- Added `uinx new <name> --kernel=x86_64|aarch64|riscv64` with startup assembly, linker script, freestanding core selection and static ELF linking.
- Added RISC-V kernel `medany` code-model support for the high starter load address.
- Added MIR local-load forwarding, constant folding and dead SSA-value elimination.
- Extended conformance/unit/package integration tests for the new language, optimizer, SMP modes, automatic atomics, per-CPU TLS and three-architecture kernel projects.

## Uinx 0.2.0 — 2026-08-19

**by JiTianYu391**

- Reworked the canonical surface syntax around indentation instead of mandatory braces and semicolons.
- Added `need <component>` and `dontneed <component>` directives; `dontneed std` is the canonical freestanding form.
- Added readable canonical spellings including `func`, `val`, `var`, `public`, `extend`, `ref`, `mutref`, `ptr`, `mutptr`, `borrow`, `deref`, `new`, `scope`, `and`, `or`, and `not`.
- Kept the 0.1 Rust-like spellings as migration aliases so existing sources do not need an immediate flag day.
- Made the lexer indentation-aware with `INDENT`/`DEDENT`, Python-style `#` comments, delimiter-aware newlines, and indentation diagnostics.
- Updated package/build selection so `need`/`dontneed` controls standard-library layers, runtime linkage, and direct dependencies.
- Migrated the shipped standard library, examples, and tests to the canonical 0.2 syntax.
- Reorganized tests into unit, conformance, safety, codegen, and runtime suites with machine-readable expected diagnostics.
- Added a project `.clang-format`, CMake `format`/`format-check` targets, and indentation-aware `uinx fmt` behavior.
- Relicensed the project under BSD-3-Clause and added SPDX identifiers to source-controlled code.

## Uinx 0.1.0 — 2026-08-18

**by JiTianYu391**

- Added executable compiler pipeline from source through object/link.
- Added ownership/borrow checking, traits/generics, RAII, unsafe/raw pointers, C FFI and LLVM inline assembly.
- Added async future-frame lowering with suspend/resume testing.
- Added core/alloc/minimal/std source layers and hosted runtime primitives.
- Added package/build/test/fmt/lint/doc tooling and LSP server.
- Added compile-pass/fail, borrow-fail, runtime, cross-target, bare-metal, async, FFI and asm tests.
- Added fuzzers, benchmark and release verification logs.
