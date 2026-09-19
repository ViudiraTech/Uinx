# Uinx Memory and SMP Model — Rust + C++20 + LKMM fusion, explicit-shared

Uinx combines ownership/borrowing with an explicit weak-memory model for
systems software. There is no implicit atomic promotion: only explicitly
`shared` state and explicit atomic APIs lower to LLVM atomics. MMIO, per-CPU
storage, and protocol-level synchronization stay explicit.

Data-race definition (Rust + C++20): in safe code a data race is undefined
behavior. A data race is conflicting non-synchronized accesses where at least
one is non-atomic; conflicting means overlapping memory with at least one
write; non-synchronized means neither happens-before the other. Mixed-size
overlapping atomic accesses follow the C++ limitation and are rejected.

## Ownership and aliasing

Safe references are `ref T` and `mutref T`. Raw pointers are `ptr T` and
`mutptr T` and require an `unsafe` boundary for dereference or arithmetic.
Non-`Copy` values move by value, mutable references are affine, and the
borrow checker tracks places down to structure fields.

```uinx
val shared = borrow value
val unique = borrow mut value
unsafe:
    deref raw = 42
```

### Borrow-checking invariants (Polonius-alpha + NLL + two-phase)

The safe-reference checker is flow-sensitive over HIR `SymbolId` identity with
backward liveness and Polonius-style loan liveness (loan live only if its
borrower may be used later via root or precise-place liveness plus subset
edges). Place conflict is Rust-style: sibling fields disjoint, `[*]`
conservative, parent/child overlap, `<external:>` conservative.

For the currently implemented language forms, the checker enforces before
MIR/code generation:

- a non-`Copy` place cannot be used after move until fully reinitialized;
- a moved parent cannot be resurrected by initializing only one child field;
- an active `mutref` excludes overlapping reads, writes, moves, and borrows;
- an active shared `ref` excludes overlapping writes/moves and mutable borrows;
- sibling struct fields are independent places, dynamic indexes alias;
- two-phase borrows: `receiver.mut_method(args)` reserves receiver as
  shared-like, inspects args, then activates to exclusive. This accepts
  `v.push(v.len())`. Function `borrow mut` args and `x += x` compound
  assignment use the same reservation/activation protocol;
- reference provenance through bindings, aggregates, calls, method receivers;
- stack references cannot escape scope/function (`E0404/E0405`);
- branch/loop fixed-point merging; NLL-style loan expiry via backward
  root + precise-place liveness; `await` stack-borrow rejection (`E0407`);
- fail-closed `E0408` on non-convergence.

`unsafe` permits raw-pointer/assembly/FFI invariants. It never turns safe
references into unchecked aliases.

### `Copy` is not a user assertion

An explicit `Copy` is accepted only when every concrete field is copyable.
`mutref` fields are never `Copy`; `Copy` + `Drop` on the same concrete type
is rejected.

## Declaring concurrency

```uinx
public unsafe concurrent func secondary_cpu_entry() -> unit:
    scheduler_tick()
    return
```

Concurrency propagates through the visible call graph. Every `concurrent`
parameter is a transfer boundary: by-value/`mutref` must be `Send`;
`ref T` must be `Send` (i.e. `T: Sync`). Raw pointers satisfy neither
without a reviewed `unsafe send/sync` wrapper. `extern` declarations are
exempt: they are FFI trust boundaries without bodies, and the `unsafe` block
at the Uinx call site is the audit point — this keeps raw-pointer-based
`uinx_atomic_*` intrinsics usable from exactly the concurrent paths they
exist for, while Uinx-level callers stay checked.

### Automatic `Send` and `Sync`

- `ref T: Send` requires `T: Sync`; `mutref T: Send` requires `T: Send`;
- named types require all concrete fields; generics use declared bounds;
- raw pointers satisfy neither by default.

```uinx
unsafe send Box[T] where T: Send
unsafe sync Box[T] where T: Send + Sync
```

## Shared state (explicit-only)

```uinx
shared var online_cpus: u64 = 0

struct RunQueue:
    shared wakeups: u64
    local_hint: u64
```

Only explicitly `shared` integer/raw-pointer globals/fields lower to
LLVM atomics (`bool` uses byte-sized `AtomicBool` over `u8` because LLVM `i1`
atomics are invalid). `smp auto/manual/strict` never promotes implicitly. Any
concurrent access to a mutable non-`shared` non-`percpu` global/field is
`E0363`: mark it `shared`, use a lock, or use `percpu`. Multi-field
invariants are never magically safe via independent atomics.

## SMP policy

```uinx
smp auto
```

| Mode | Meaning |
|---|---|
| `smp auto` | Explicit `shared` uses acquire/release/acq_rel. No implicit promotion. |
| `smp manual` | Same as auto for explicit state (no promotion). |
| `smp strict` | Explicit `shared` uses seq_cst (debugging/max ordering). |

`--smp=auto|manual|strict` overrides. Ordering for explicit lowering:

- loads: acquire (strict: seq_cst);
- stores: release (strict: seq_cst);
- RMW (`+=,-=,&=,|=,^=` → `atomicrmw`): acq_rel (strict: seq_cst).

Unsupported compound ops are rejected (`E0509`); use `compare_exchange` or a
lock. LKMM roach-motel lock ordering applies: acquire on lock, release on
unlock; observers without the lock need explicit fences.

## Explicit fences

```uinx
fence acquire
fence release
fence acq_rel
fence seq_cst

compiler_fence acquire
compiler_fence release
compiler_fence acq_rel
compiler_fence seq_cst
```

Lowered at MIR/LLVM level (`fence` vs `fence syncscope("singlethread")`).
C++20 validation: loads never release/acq_rel; stores never acquire/acq_rel;
`cmpxchg` failure never release/acq_rel and never exceeds success ordering
(downgraded per LLVM rules).

## Atomic library (full family)

`core::atomic` provides `AtomicU8/U16/U32/U64/Usize/Bool` with
`load(_relaxed/_acquire)`, `store(_relaxed/_release)`, `swap`,
`fetch_add/sub/and/or/xor(_relaxed)`, `compare_exchange(_weak)`.
`uinx_atomic_*` with constant ordering lowers directly to LLVM
`load atomic/store atomic/atomicrmw/cmpxchg`; hosted builds fall back to
C11 `stdatomic` in `runtime/sync.c`.

```uinx
var counter = new AtomicU64(value=0)
counter.fetch_add(1)
val value = counter.load()
```

## Synchronization

`core::sync` freestanding: `SpinLock` (with backoff), fair `TicketSpinLock`,
`RwSpinLock` (concurrent readers), `SeqLock` (lock-free readers via retry),
`Once`, `Barrier`. No interrupt/preemption/lockdep/NUMA policy is guessed.

```uinx
var lock = new SpinLock(state=new AtomicU64(value=0))
lock.lock()
# protected state
lock.unlock()
```

## Per-CPU state

```uinx
percpu var local_ticks: u64 = 0
```

Lowers to local-exec TLS, never atomic. Kernels must install TLS/TP per CPU
before access. Remote writes need an explicit protocol.

## MMIO and volatile access

Device registers are never atomics. Use `core::ptr` volatile ops in `unsafe`
plus explicit fences/`asm()` per device/arch spec.

```uinx
public unsafe func device_write(reg: mutptr u32, value: u32) -> unit:
    write_volatile_u32(reg, value)
    return
```

## What the compiler can and cannot infer

The compiler discovers concurrent call paths and checks explicit `shared`.
It cannot infer synchronization hidden behind FFI, inline assembly, DMA,
interrupt controllers, multi-word lock-free protocols, or external agents.
`unsafe` is where the kernel author takes responsibility.

## Verification status

Tests verify explicit-shared IR lowering, call-graph propagation, E0363
rejection of implicit access, strict seq_cst, TLS `percpu`, fences, `Send`
checks, structural `Send`/`Sync`, full-width atomic lowering with C++20
ordering validation, two-phase borrow acceptance (`v.push(v.len())`-shape),
and cross-target objects. Not a machine-checked proof for all
unsafe/FFI/kernel code.
