# Release Verification

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

Validation artifacts in `verification/` must describe the current source tree. Old release logs are not treated as evidence for 0.3; verification artifacts must be regenerated from this source tree.

## Test suite

The Uinx 0.3 CTest suite contains 18 top-level tests:

1. compiler unit tests
2. runtime unit tests
3. x86 inline-asm object emission
4. x86 asm input/output executable
5. x86 asm inout executable
6. x86 asm multiple-output executable
7. safe slice in-bounds executable
8. safe slice out-of-bounds trap
9. C FFI executable
10. three-architecture `dontneed std` object emission
11. AArch64/RISC-V cross-target asm objects
12. invalid AArch64 register rejection
13. three-architecture freestanding ELF linking
14. async suspend/resume executable harness
15. compile-pass conformance corpus
16. compile-fail conformance corpus with exact expected diagnostics
17. borrow-fail safety corpus with exact expected diagnostics
18. package tooling integration, including x86-64/AArch64/RISC-V64 kernel scaffolds and SMP policy overrides

The canonical test sources use the indentation-based 0.3 syntax rather than the migration aliases.

## Standard library aggregate check

All `.ux` files under `stdlib/` can be supplied to one `uinxc --emit=check` invocation. This validates parser, resolver, type, trait and ownership compatibility as one source set.

## Examples

Every shipped `.ux` example is checked individually with `uinxc --emit=check`; the result is recorded in `verification/examples-check.txt`.

## Fuzzing and benchmarks

Fuzzer and benchmark targets remain part of the project. A bounded or historical fuzz run is not represented as current release evidence unless it is regenerated against the exact source tree. Long-duration fuzz confidence remains `UNVERIFIED`.
