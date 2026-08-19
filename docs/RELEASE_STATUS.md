# Uinx 0.3 Release Status

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

## Verified in this release

- Extended canonical syntax with `const`, `static`, `for`, `loop`, `break`, `continue`, bitwise/shift operators and compound assignments.
- OS-aware SMP model with `concurrent`, `shared`, `percpu`, `smp auto/manual/strict`, explicit `fence` and `compiler_fence`.
- Call-graph concurrency propagation and automatic atomic promotion for compatible mutable global/field state.
- LLVM atomic load/store/RMW/fence lowering with acquire/release/acq_rel automatic ordering and seq_cst strict mode.
- Freestanding `core::mem`, volatile pointer primitives, atomic primitives, and `SpinLock`.
- Typed raw-pointer arithmetic and dereference assignment suitable for allocators, page tables and byte-memory code.
- `uinx new <name> --kernel=<arch>` creates and cross-links x86-64, AArch64 and RISC-V64 kernel ELF projects.
- MIR optimization now includes local load forwarding, constant folding and dead pure-SSA elimination in addition to unreachable-block removal.

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

- `UNVERIFIED`: formal proof that all safe Uinx programs are free of data races and every lifetime/borrow edge case; automatic SMP strengthening is tested but not a theorem over unsafe/FFI/external-agent code.
- `UNVERIFIED`: complete borrow-region solving across arbitrary irreducible CFGs and all loop/join patterns.
- `UNVERIFIED`: generic Drop specialization for every nested generic ownership pattern and panic/unwind destruction.
- `UNVERIFIED`: aggregate/bitfield/vector C ABI portability across all architectures and operating systems.
- `UNVERIFIED`: allocator-free async frame placement and kernel executor integration.
- `UNVERIFIED`: universal boot-protocol integration and actual execution of every produced freestanding ELF on QEMU or hardware; release tests verify object generation and link-level ELF construction.
- `UNVERIFIED`: hosted runtime behavior on Windows/macOS; current runtime validation is POSIX/Linux.
- `UNVERIFIED`: full Unicode normalization, segmentation, collation and locale services.
- `UNVERIFIED`: production TLS/DNS/async-I/O reactor networking layer.
- `UNVERIFIED`: remote package registry, signed packages and network package distribution.
- `UNVERIFIED`: advanced LSP completion/navigation/refactoring features.
- `UNVERIFIED`: debug-info quality, exception/unwind integration, incremental compilation and distributed build caching.
- `UNVERIFIED`: long-duration fuzz campaigns and formal compiler verification.

These boundaries distinguish implemented and tested behavior from broader production claims that require substantially more validation.
