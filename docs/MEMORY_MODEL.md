# Uinx Memory Safety Model

**by JiTianYu391**

## Goals

Safe Uinx is designed to reject use-after-move, overlapping mutable aliasing, mutation while shared-borrowed, invalid mutable borrowing of immutable places, and safe raw-memory access. The design uses ownership plus inferred loan lifetimes and does not require explicit lifetime parameters for the verified release syntax.

## Places

The borrow checker reasons about places rather than whole variables only. A place is represented as a root local followed by projections. Field projections are distinct, so `pair.left` and `pair.right` can be borrowed independently. Index projections are normalized conservatively because two runtime indices may be equal.

## Moves

Non-`Copy` values are affine. A by-value use transitions the place to moved state. A later read emits a use-after-move diagnostic. Assignment may reinitialize a local where permitted by type and mutability rules.

Mutable references are deliberately not `Copy`; this prevents duplicating exclusive capabilities through an ordinary assignment or function call.

Reference provenance is tracked through local reference bindings and reference assignment. Returning a reference that originates from a stack-owned place is rejected, including propagation through calls or reference-bearing structs. A reference stored in an outer binding may not outlive an inner local it points to.

## Loans

Shared loans permit additional shared loans but conflict with mutation and mutable borrowing of overlapping places. Mutable loans conflict with every overlapping active loan. Last-use analysis ends a loan when it is no longer needed, which permits patterns that a purely lexical borrow checker would reject.

## Destruction

MIR contains explicit Drop instructions. This makes destructor order visible before backend lowering and avoids relying on an ambient garbage collector. The runtime has no tracing GC.

## Unsafe boundary

Raw pointers do not carry safe aliasing guarantees. Dereference and arithmetic require unsafe context. Safe references may be converted to compatible raw pointers, but raw-pointer operations remain unsafe. Volatile MMIO access is supplied through explicit unsafe runtime primitives and inline assembly.

## Concurrency

`Send` and `Sync` are structurally inferred for named types from their fields, with raw pointers excluded from automatic satisfaction. This is a conservative building block rather than a complete concurrency theorem. Formal absence of every possible data race in all library and FFI combinations remains `UNVERIFIED`.

## Formal verification status

The implementation is tested with positive and negative suites, but it is not machine-proved. Whole-CFG region solving for every possible loop/join pattern, unsafe-code soundness proofs, and a formal weak-memory model are `UNVERIFIED`.
