# Uinx Memory and SMP Model

Uinx combines ownership/borrowing with an explicit weak-memory model for systems software. The compiler can strengthen accesses that it can prove are shared by concurrent execution, while keeping MMIO, per-CPU storage, and protocol-level synchronization explicit.

## Ownership and aliasing

Safe references are `ref T` and `mutref T`. Raw pointers are `ptr T` and `mutptr T` and require an `unsafe` boundary for dereference or arithmetic. Non-`Copy` values move by value, mutable references are affine, and the borrow checker tracks places down to structure fields.

```uinx
val shared = borrow value
val unique = borrow mut value
unsafe:
    deref raw = 42
```

## Declaring concurrency

A function that can execute concurrently on multiple CPUs/threads is declared with `concurrent`:

```uinx
public unsafe concurrent func secondary_cpu_entry() -> unit:
    scheduler_tick()
    return
```

Concurrency is propagated through the call graph. If a `concurrent` entry calls `scheduler_tick()`, helpers reachable from that entry are analyzed as concurrent too. It is not necessary to annotate every helper manually.

## Shared state

Explicit shared state uses `shared`:

```uinx
shared var online_cpus: u64 = 0

struct RunQueue:
    shared wakeups: u64
    local_hint: u64
```

Atomic-compatible scalar `shared` globals and fields are lowered to LLVM atomic operations. Uinx currently treats integer, boolean, and raw-pointer scalar storage as atomically compatible.

In `smp auto` and `smp strict`, mutable scalar globals reached from a concurrent call path are also promoted to shared storage by semantic analysis. Atomic-compatible fields of mutable global structures can be promoted field-by-field. Once promoted, every compiler-visible access to that global/field is atomic, including accesses from a non-`concurrent` observer.

The compiler does **not** pretend that making individual fields atomic makes an arbitrary multi-field invariant safe. If a concurrent path touches aggregate state that cannot be safely strengthened as one atomic object, the compiler emits `W0360`; use explicit shared fields, a lock, per-CPU storage, or another protocol.

## SMP policy

The module policy is selected with one directive:

```uinx
smp auto
```

Three modes are implemented:

| Mode | Meaning |
|---|---|
| `smp auto` | Default. Infer shared scalar state reachable from concurrent paths and use acquire/release-style ordering. |
| `smp manual` | Disable implicit strengthening. Only explicitly `shared` data and explicit atomic APIs are atomic. |
| `smp strict` | Keep automatic shared-state discovery but use sequentially consistent ordering for generated atomics. |

The package tools also accept `--smp=auto`, `--smp=manual`, and `--smp=strict` as an override.

### Ordering in automatic mode

For compiler-generated operations in `smp auto`:

- atomic loads use **acquire**;
- atomic stores use **release**;
- supported read-modify-write operations use **acq_rel**.

`+=`, `-=`, `&=`, `|=`, and `^=` on atomic places lower to LLVM `atomicrmw`. Operations that require a protocol Uinx cannot synthesize safely are rejected instead of being silently weakened; use `AtomicU64.compare_exchange()` or a lock.

`smp strict` emits `seq_cst` for automatically generated atomic accesses. This is a debugging/maximum-ordering option, not the recommended default for performance-sensitive kernels.

## Explicit fences

Hardware-visible fences are written directly:

```uinx
fence acquire
fence release
fence acq_rel
fence seq_cst
```

Compiler-local ordering barriers are:

```uinx
compiler_fence acquire
compiler_fence release
compiler_fence acq_rel
compiler_fence seq_cst
```

These are lowered at MIR/LLVM level. The backend is responsible for selecting the appropriate target instruction sequence; Uinx does not hard-code x86 barriers into architecture-independent source.

## Atomic library

`core::atomic` provides freestanding compiler-lowered atomics. The current concrete primitive is `AtomicU64`:

```uinx
var counter = new AtomicU64(value=0)
counter.fetch_add(1)
val value = counter.load()
```

Its operations include relaxed load/store, acquire load, release store, acq_rel fetch-add, and compare-exchange. Compiler-recognized `uinx_atomic_*` calls lower directly to LLVM atomic instructions, so a bare-metal target does not require the hosted runtime for these paths.

## Spin locks

`core::sync` provides a small freestanding `SpinLock` built on `AtomicU64`:

```uinx
var lock = new SpinLock(state=new AtomicU64(value=0))

lock.lock()
# protected state
lock.unlock()
```

The lock is suitable as a primitive synchronization building block. It intentionally does not guess higher-level scheduling policy, interrupt state, lock ranking, preemption rules, or NUMA placement; kernels should layer those policies explicitly.

## Per-CPU state

State that belongs to one CPU is declared with `percpu`:

```uinx
percpu var local_ticks: u64 = 0
```

`percpu` lowers to local-exec LLVM TLS storage and is deliberately excluded from automatic atomic strengthening. This avoids turning CPU-private counters into contended cache-line atomics.

A bare-metal kernel **must initialize the architecture TLS/thread-pointer base for each CPU before accessing `percpu` variables**. The generated starter kernel does not invent a boot protocol or per-CPU allocator, so it does not access `percpu` storage before the kernel installs that state.

Remote writes into another CPU's per-CPU area are not treated as ordinary safe local accesses and need an explicit kernel protocol.

## MMIO and volatile access

Device registers are not ordinary shared RAM. Uinx does not automatically convert MMIO to atomics. Use `core::ptr` volatile operations inside `unsafe` code:

```uinx
public unsafe func device_write(reg: mutptr u32, value: u32) -> unit:
    write_volatile_u32(reg, value)
    return
```

Use explicit fences or architecture-specific `asm()` where the device/architecture specification requires ordering beyond volatile access.

## What the compiler can and cannot infer

Automatic SMP strengthening is intentionally bounded by what the compiler can prove from Uinx-visible code. It can discover concurrent call paths and compiler-visible scalar globals/fields. It cannot infer synchronization hidden behind arbitrary FFI, inline assembly, DMA engines, interrupt controllers, lock-free multi-word protocols, or external agents.

`unsafe` remains the boundary where the kernel author takes responsibility for those facts.

## Verification status

The release tests verify IR-level atomic lowering, automatic call-graph propagation, automatic shared promotion, manual/strict policy behavior, TLS lowering for `percpu`, explicit fences, and cross-target object generation. The memory model is not a machine-checked proof of race freedom for all possible unsafe/FFI/kernel code.
