# Changelog

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
