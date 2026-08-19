# Uinx 0.2 Release Status

**by JiTianYu391**

## Verified in this release

- Native C++20 compiler and command-line tools build in Release mode.
- Indentation-sensitive canonical syntax with migration compatibility for the 0.1 brace syntax.
- `need` / `dontneed` source directives, including `dontneed std` and package-tool source/link selection.
- Direct LLVM IR backend, object generation and separate link phase.
- Static typing, local inference, generic functions/struct layouts and trait-bound checking for tested forms.
- Trait implementation conformance and method calls.
- Ownership moves, non-copy mutable references, NLL-style last-use loan shortening, field-sensitive borrowing, conservative index borrowing, and tested stack-reference escape rejection.
- Safe Slice/SliceMut indexing with runtime bounds trap.
- Synchronous RAII Drop calls for tested concrete types.
- Scalar/pointer C FFI on the host target.
- x86_64 inline assembly input/output/inout/multi-output and clobbers.
- AArch64/RISC-V64 inline-asm target validation and object generation.
- x86_64/AArch64/RISC-V64 freestanding object generation and ELF link with `dontneed std` sources.
- Real async pending/resume/ready transition in the integration harness.
- POSIX runtime primitives exercised by runtime tests.
- Local path package dependency workflow and required CLI command names.
- Compiler/parser fuzz targets and benchmark target remain buildable project components.

## `UNVERIFIED`

The following items are intentionally not represented as passing merely because source declarations exist:

- `UNVERIFIED`: formal proof that all safe Uinx programs are free of data races and every lifetime/borrow edge case.
- `UNVERIFIED`: complete borrow-region solving across arbitrary irreducible CFGs and all loop/join patterns.
- `UNVERIFIED`: generic Drop specialization for every nested generic ownership pattern and panic/unwind destruction.
- `UNVERIFIED`: aggregate/bitfield/vector C ABI portability across all architectures and operating systems.
- `UNVERIFIED`: allocator-free async frame placement and kernel executor integration.
- `UNVERIFIED`: actual boot execution of produced freestanding ELFs on QEMU or hardware.
- `UNVERIFIED`: hosted runtime behavior on Windows/macOS; current runtime validation is POSIX/Linux.
- `UNVERIFIED`: full Unicode normalization, segmentation, collation and locale services.
- `UNVERIFIED`: production TLS/DNS/async-I/O reactor networking layer.
- `UNVERIFIED`: remote package registry, signed packages and network package distribution.
- `UNVERIFIED`: advanced LSP completion/navigation/refactoring features.
- `UNVERIFIED`: debug-info quality, exception/unwind integration, incremental compilation and distributed build caching.
- `UNVERIFIED`: long-duration fuzz campaigns and formal compiler verification.

These boundaries distinguish implemented and tested behavior from broader production claims that require substantially more validation.
