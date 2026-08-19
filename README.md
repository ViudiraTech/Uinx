<div align="center">

# Uinx

### Systems programming without syntax noise

**Readable indentation · ownership & borrowing · native LLVM code · no tracing GC · freestanding-first**

![Version](https://img.shields.io/badge/version-0.3.0-4C8BF5?style=flat-square)
![Compiler](https://img.shields.io/badge/compiler-C%2B%2B20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![Backend](https://img.shields.io/badge/backend-LLVM-262D3A?style=flat-square&logo=llvm)
![License](https://img.shields.io/badge/license-BSD--3--Clause-44CC11?style=flat-square)
![GC](https://img.shields.io/badge/tracing_GC-none-success?style=flat-square)

Uinx is an ahead-of-time systems language for kernels, drivers, embedded software,
runtimes, and native applications. Its surface is intentionally Python-like; its
execution model stays explicit enough for low-level work.

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

</div>

---

## A small surface, a real compiler pipeline

```uinx
struct Counter:
    value: i32

extend Counter:
    public func add(self: mutref Self, amount: i32) -> unit:
        self.value += amount
        return

func main() -> i32:
    var counter = new Counter(value=40)
    counter.add(2)
    return counter.value - 42
```

Uinx compiles directly through its own front end and MIR into LLVM IR; it is not a
C/C++ transpiler.

```text
Source
  ↓
Lexer → Parser → AST → Name Resolution / HIR
  ↓
Type + Trait Analysis
  ↓
Ownership / Borrow / Lifetime-Flow Analysis
  ↓
MIR → Optimization → LLVM IR → Object → Executable / ELF
```

## What the language looks like

| Area | Canonical Uinx |
|---|---|
| Blocks | indentation + `:` |
| Bindings | `val` immutable, `var` mutable |
| Functions | `func name(...) -> Type:` |
| Data | `struct`, `new`, `extend` |
| Generics | `Name[T]`, `func f[T](...)` |
| Bounds | `[T: Copy]` or `where T: Copy + Send` |
| Ownership | implicit move + explicit `move value` |
| References | `ref T`, `mutref T`, `borrow`, `borrow mut` |
| Raw pointers | `ptr T`, `mutptr T`, unsafe dereference/arithmetic |
| Traits | `trait`, `extend Type with Trait` |
| RAII | `Drop` on normal scope exit for implemented forms |
| Unsafe | `unsafe func`, `unsafe:` |
| C ABI | `extern "C" func` |
| Assembly | LLVM-backed `asm()` |
| Async | `async func`, `await` |
| Freestanding | `dontneed std`, `need core` |
| SMP | `concurrent`, `shared`, `percpu`, `smp` |

### Generics without punctuation overload

Bounds can stay next to the parameter or move into a `where` clause when the
signature gets busy:

```uinx
func keep[T](value: T) -> T where T: Copy + Send:
    return value

struct Slot[T] where T: Copy:
    value: T
```

`Copy` is compiler-validated. An explicit `Copy` implementation is rejected when
a field is not actually copyable, when it contains an exclusive mutable reference,
or when the same concrete type has `Drop` semantics.

### Ownership is visible when you want it to be

```uinx
struct Packet:
    id: u64

func consume(packet: Packet) -> u64:
    return packet.id

func main() -> i32:
    val packet = new Packet(id=42)
    val owned = move packet
    consume(owned)
    return 0
```

Ordinary by-value use already moves non-`Copy` values. `move` is the explicit
spelling for APIs and code where making transfer intent obvious is useful.

## Safety model

Safe references are `ref T` and `mutref T`; raw pointers are separated behind an
`unsafe` boundary. The borrow checker now tracks **resolved HIR binding identity**
rather than variable spelling, so shadowed locals cannot accidentally erase or
reuse another binding's ownership state.

The current checker includes:

- non-`Copy` move tracking and partial move/reinitialization;
- shared-vs-exclusive alias checks down to struct fields, with conservative index aliasing;
- reference provenance through bindings, aggregates, assignments, calls, method receivers, and returns;
- branch joins plus loop/back-edge fixed-point analysis;
- backward CFG-style liveness for non-lexical loan expiry;
- stack-reference escape rejection through direct and aggregate returns;
- conservative safe-reference checks across `await` suspension;
- compiler-validated `Copy` eligibility;
- fail-closed diagnostics if borrow dataflow cannot converge within its safety limit.

```uinx
func main() -> i32:
    var value = 10
    val view = borrow mut value

    # Rejected while `view` is live:
    # value = 20

    deref view = 20
    return value - 20
```

`unsafe` is an explicit trust boundary, not a switch that disables ownership rules
for safe references. Raw pointer operations, inline assembly, FFI contracts, MMIO,
and other externally enforced invariants belong on the unsafe side of that boundary.

> **Scope of the claim:** this tree has substantially stronger control-flow and
> provenance checking than the earlier 0.3 checker, but it does not claim formal
> equivalence to `rustc` or a mathematical proof that every possible safe Uinx
> program is memory-safe. See `docs/RELEASE_STATUS.md` for the exact verified and
> unverified boundary.

## Control flow and computational model

```uinx
func gcd(a0: u64, b0: u64) -> u64:
    var a = a0
    var b = b0
    while b != 0:
        val next = a % b
        a = b
        b = next
    return a
```

Uinx has mutable state, conditionals, unbounded-language-model loops, recursion,
integer arithmetic, functions, and dynamically managed memory layers. That is a
general-purpose/Turing-complete computational model in the usual abstract-machine
sense; real executions are of course bounded by finite machine resources.

Also implemented: `if / elif / else`, `while`, integer-range `for`, `loop`,
`break`, `continue`, `scope`, `return`, and `pass`.

## `need` / `dontneed`: environment as source-level intent

Hosted program:

```uinx
need std
```

Kernel or embedded code:

```uinx
dontneed std
need core
```

`dontneed runtime` suppresses the hosted runtime archive. Direct manifest path
dependencies can be excluded the same way. `no_std;` remains migration syntax;
`dontneed std` is canonical.

## OS-first concurrency

```uinx
dontneed std
need core
smp auto

shared var online_cpus: u64 = 0

func account_cpu() -> unit:
    online_cpus += 1
    return

public unsafe concurrent func secondary_cpu_entry() -> unit:
    account_cpu()
    return
```

`concurrent` propagates through the call graph. In `smp auto`, compatible mutable
shared state reached by concurrent paths can be strengthened to atomic accesses.
For protocols that need explicit ordering:

```uinx
fence acquire
fence release
fence acq_rel
fence seq_cst
compiler_fence acquire
```

Policies:

```text
smp auto    infer shared scalar access; acquire/release/acq_rel defaults
smp manual  only explicitly shared/atomic state is strengthened
smp strict  inferred accesses use seq_cst
```

Multi-field invariants are not magically made correct by independent atomics. Use
a lock, per-CPU state, or an explicit protocol when the invariant spans multiple
locations.

## Bare-metal path

Create a starter kernel:

```sh
uinx new mykernel --kernel=x86_64
cd mykernel
uinx build --release
```

Also supported by the project generator:

```sh
uinx new mykernel --kernel=aarch64
uinx new mykernel --kernel=riscv64
```

A freestanding entry can stay compact:

```uinx
dontneed std
need core
smp auto

public unsafe concurrent func kernel_main() -> unit:
    return
```

The generated project includes startup assembly, a linker script, a freestanding
manifest, and target-specific ELF linking. Firmware/boot protocol integration is a
platform decision rather than hidden compiler behavior.

## Freestanding building blocks

The shipped low-level layers include implemented paths for:

- `core::mem` byte copy/move/fill primitives;
- `core::ptr` volatile MMIO helpers;
- `core::atomic` compiler-lowered atomics;
- `core::sync` spin locking;
- typed raw-pointer arithmetic and dereference assignment;
- `alloc`, minimal hosted facilities, and the fuller `std` layer where selected.

```uinx
public unsafe func clear(base: mutptr u8, size: usize) -> unit:
    var i: usize = 0 as usize
    while i < size:
        deref (base + i) = 0
        i += 1 as usize
    return
```

## Build the toolchain

Requirements: CMake, a C++20 compiler, LLVM development files, Clang for the tested
host package link flow, and LLD for the bare-metal link flow.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Direct compiler use:

```sh
build/uinxc source.ux --emit=check
build/uinxc source.ux --emit=obj -o source.o
```

Package workflow:

```sh
build/uinx new app
cd app
../build/uinx check
../build/uinx build
../build/uinx run
```

Main commands include `new`, `build`, `run`, `check`, `test`, `fmt`, `lint`, `doc`,
`fetch`, and `add`.

### Formatting

```sh
cmake --build build --target format
cmake --build build --target format-check
uinx fmt
```

## Bootstrap status

The canonical stage-0 compiler in this repository is still **C++20**. The language
has the control-flow and computational expressiveness needed for compiler work, but
a complete Uinx implementation of the Uinx compiler is not shipped in this tree yet.
Therefore this release must **not** be described as self-hosted.

`docs/BOOTSTRAP.md` defines the stage0 → stage1 → stage2 reproducibility criteria a
real self-hosting release must pass. This is intentionally stated as an engineering
boundary rather than hidden behind a wrapper that simply invokes the C++ compiler.

## Documentation

| Document | Purpose |
|---|---|
| `docs/LANGUAGE_SPEC.md` | canonical implemented grammar and semantics |
| `docs/MEMORY_MODEL.md` | ownership, aliasing, atomics, SMP, fences |
| `docs/BOOTSTRAP.md` | honest self-hosting/bootstrap acceptance criteria |
| `docs/OS_DEVELOPMENT.md` | freestanding kernel workflow |
| `docs/UNSAFE_AND_ASM.md` | unsafe boundary and inline assembly |
| `docs/STANDARD_LIBRARY.md` | core / alloc / minimal / std layers |
| `docs/VERIFICATION.md` | reproducible verification procedure |
| `docs/RELEASE_STATUS.md` | implemented vs unverified boundaries |

## License

Uinx is released under the **BSD 3-Clause License**.

```text
Copyright (c) 2026 ViudiraTech
Code by JiTianYu391
```

See [`LICENSE`](LICENSE).

<div align="center">

**Uinx — readable at the surface, explicit at the machine boundary.**

</div>
