# Uinx 0.3 Release Status

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

## Verified in this release

- Extended canonical syntax with `const`, `static`, `for`, `loop`, `break`, `continue`, explicit `move`, `where` bounds, bitwise/shift operators and compound assignments.
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
- Static typing, local inference, generic functions/struct layouts, inline/`where` trait bounds, and concrete struct-bound checking for tested forms.
- Trait implementation conformance and method calls, with compiler-enforced `Copy` eligibility that rejects mutable-reference fields and `Copy`/`Drop` conflicts.
- Ownership moves and explicit `move`, affine mutable references, HIR binding-identity places, partial move/reinitialization, field-sensitive borrowing, conservative index aliasing, reference provenance through aggregates/calls/method receivers, backward liveness-based loan expiry, branch/loop fixed-point state merging, and stack-reference escape rejection for implemented forms.
- Borrow dataflow uses a fail-closed non-convergence diagnostic instead of silently accepting an incomplete fixed point.
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

- `UNVERIFIED`: formal proof that all safe Uinx programs are memory-safe/data-race-free in every lifetime, provenance, async, FFI and weak-memory edge case; implemented checks are conservative engineering mechanisms, not a theorem.
- `UNVERIFIED`: rustc-equivalent region inference/Polonius semantics, two-phase borrows, every irreducible CFG shape, and formal equivalence to Rust's complete safe-reference model.
- `UNVERIFIED`: self-hosting. The canonical compiler is C++20; no complete Uinx-written stage1 compiler is shipped yet. `BOOTSTRAP.md` defines the required stage0/stage1/stage2 criteria.
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
