# Release Verification

**by JiTianYu391**

Validation artifacts in `verification/` must describe the current source tree. Old 0.1 logs are intentionally not carried forward as evidence for 0.2.

## Test suite

The Uinx 0.2 CTest suite contains 18 tests:

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
18. two-package tooling integration

The canonical test sources use the indentation-based 0.2 syntax rather than the migration aliases.

## Standard library aggregate check

All `.ux` files under `stdlib/` can be supplied to one `uinxc --emit=check` invocation. This validates parser, resolver, type, trait and ownership compatibility as one source set.

## Examples

Every shipped `.ux` example is checked individually with `uinxc --emit=check`; the result is recorded in `verification/examples-check.txt`.

## Fuzzing and benchmarks

Fuzzer and benchmark targets remain part of the project. A bounded or historical fuzz run is not represented as current release evidence unless it is regenerated against the exact source tree. Long-duration fuzz confidence remains `UNVERIFIED`.
