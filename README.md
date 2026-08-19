<div align="center">

# Uinx

### Readable syntax. Native code. Built for systems.

Uinx is an ahead-of-time systems programming language with **Python-style indentation**, **ownership and borrowing**, **LLVM native code generation**, **no tracing GC**, and a first-class **freestanding kernel path**.

![Version](https://img.shields.io/badge/version-0.3.0-4C8BF5?style=flat-square)
![C++](https://img.shields.io/badge/compiler-C%2B%2B20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![LLVM](https://img.shields.io/badge/backend-LLVM-262D3A?style=flat-square&logo=llvm)
![License](https://img.shields.io/badge/license-BSD--3--Clause-44CC11?style=flat-square)
![GC](https://img.shields.io/badge/GC-none-success?style=flat-square)

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

</div>

---

## Why Uinx?

Uinx keeps the machinery needed for kernels, drivers, embedded software, runtimes, and high-performance native programs without forcing ordinary code to look like punctuation-heavy C++ or Rust.

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

The compiler pipeline is native:

```text
Source → Lexer → Parser → AST → Name/Type/Trait Analysis
       → Ownership/Borrow Check → MIR → Optimization → LLVM → Object/ELF
```

Uinx does not transpile programs to C or C++.

## Language at a glance

| Area | Uinx 0.3 |
|---|---|
| Blocks | indentation + `:` |
| Immutable / mutable | `val` / `var` |
| Functions | `func name(...) -> Type:` |
| Structures | `struct`, `new`, `extend` |
| Generics | `Name[T]`, `func f[T](...)` |
| Traits | `trait`, `extend Type with Trait` |
| References | `ref T`, `mutref T` |
| Raw pointers | `ptr T`, `mutptr T` |
| Ownership | move + borrow checking + RAII |
| Unsafe boundary | `unsafe func`, `unsafe:` |
| C ABI | `extern "C" func` |
| Assembly | native LLVM inline `asm()` |
| Async | `async func`, `await` |
| Freestanding | `dontneed std`, `need core` |
| SMP | `concurrent`, `shared`, `percpu`, `smp` |

### Control flow

```uinx
func sum_without_two() -> i32:
    var total: i32 = 0

    for i in 0 .. 6:
        if i == 2:
            continue
        total += i

    loop:
        if total >= 13:
            break
        total += 1

    return total
```

`while`, `if / elif / else`, `scope`, `return`, `break`, and `continue` are also implemented.

### Globals and bit operations

```uinx
const PAGE_SHIFT: u64 = 12
static var flags: u64 = 0

func enable_page_flag() -> unit:
    flags |= 1 << PAGE_SHIFT
    flags &= ~((1 as u64) << 2)
    return
```

## `need` and `dontneed`

Source code states what environment it needs:

```uinx
need std
```

A kernel instead starts with:

```uinx
dontneed std
need core
```

`dontneed runtime` prevents the hosted runtime archive from being linked. `dontneed dependency_name` excludes a direct manifest path dependency. The old `no_std;` spelling is only a migration alias.

# OS-first SMP model

Uinx 0.3 adds a compiler-visible concurrency model aimed at kernels rather than a blanket "make everything seq_cst" switch.

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

`concurrent` propagates through the call graph, so helpers called by a concurrent entry are analyzed as concurrent too. In `smp auto`, mutable atomic-compatible globals/fields reached by those paths can be promoted to atomic access automatically.

### SMP policies

```uinx
smp auto       # default: infer + acquire/release/acq_rel
smp manual     # no implicit atomic promotion
smp strict     # infer + seq_cst generated atomics
```

The CLI can override source policy:

```sh
uinx check --smp=manual
uinx build --smp=strict
```

Explicit barriers are available when a protocol requires them:

```uinx
fence acquire
fence release
fence acq_rel
fence seq_cst

compiler_fence acquire
```

For multi-field invariants the compiler does not pretend that independent atomics are enough. Use explicit shared fields, `SpinLock`, per-CPU data, or a protocol appropriate to the kernel subsystem.

### Per-CPU data

```uinx
percpu var local_ticks: u64 = 0
```

`percpu` lowers to local-exec TLS and is intentionally excluded from automatic atomic strengthening. Bare-metal kernels must initialize the per-CPU TLS/thread-pointer base before using it on each CPU.

# Write a bare-metal kernel

Create a complete starter project:

```sh
uinx new mykernel --kernel=x86_64
cd mykernel
uinx build --release
```

Also implemented:

```sh
uinx new mykernel --kernel=aarch64
uinx new mykernel --kernel=riscv64
```

The generator creates architecture startup assembly, a linker script, a freestanding `uinx.toml`, and a Uinx kernel entry. The result is:

```text
target/release/mykernel.elf
```

Kernel source uses a dedicated entry rather than hosted `main()`:

```uinx
dontneed std
need core
smp auto

shared var online_cpus: u64 = 0

public unsafe concurrent func kernel_main() -> unit:
    online_cpus += 1
    return
```

The starter `_start` establishes an early stack, calls `kernel_main`, and enters the target idle loop if it returns. Boot-protocol integration (UEFI, Limine, Multiboot, SBI/device tree, and so on) remains a platform choice rather than hidden compiler behavior.

## Freestanding `core`

Uinx 0.3 includes OS-oriented primitives that compile without libc/host runtime on the verified bare-metal paths:

- `core::mem`: pure-Uinx `memcpy`, `memmove`, `memset`, byte-copy/move/fill;
- `core::ptr`: volatile `u8/u32/u64` MMIO loads and stores;
- `core::atomic`: compiler-lowered `AtomicU64` operations;
- `core::sync`: a freestanding `SpinLock`;
- typed raw-pointer arithmetic and `deref (...) = value` for allocators, page tables, DMA buffers, and memory code.

```uinx
public unsafe func clear(base: mutptr u8, size: usize) -> unit:
    var i: usize = 0 as usize
    while i < size:
        deref (base + i) = 0
        i += 1 as usize
    return
```

# Performance

The MIR optimizer now performs more than unreachable-block cleanup. At optimization levels that enable it, Uinx runs local-load forwarding, integer/boolean constant folding, and dead pure-SSA value elimination before LLVM optimization.

Concurrency strengthening also avoids unnecessary contention: `percpu` stays CPU-local, `smp auto` uses acquire/release/acq_rel rather than forcing seq_cst everywhere, and `smp manual` can disable all implicit promotion when a kernel wants to own the memory model explicitly.

# Build Uinx

Requirements include CMake, a C++20 compiler, LLVM development files, Clang for the tested package link flow, and LLD for bare-metal kernel linking.

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

Main commands include `new`, `build`, `run`, `check`, `test`, `fmt`, `lint`, `doc`, `fetch`, and `add`.

# Formatting

The repository ships a canonical `.clang-format`:

```sh
cmake --build build --target format
cmake --build build --target format-check
```

Uinx source can be formatted with:

```sh
uinx fmt
```

# Documentation

| Document | Contents |
|---|---|
| `docs/LANGUAGE_SPEC.md` | canonical implemented syntax |
| `docs/MEMORY_MODEL.md` | ownership, atomics, SMP modes, fences, per-CPU rules |
| `docs/OS_DEVELOPMENT.md` | bare-metal kernel workflow |
| `docs/UNSAFE_AND_ASM.md` | unsafe and inline assembly |
| `docs/STANDARD_LIBRARY.md` | core/alloc/minimal/std layers |
| `docs/VERIFICATION.md` | release verification scope |
| `docs/RELEASE_STATUS.md` | implemented vs unverified boundaries |

# License

Uinx is released under the **BSD 3-Clause License**.

```text
Copyright (c) 2026 ViudiraTech
Code by JiTianYu391
```

See [`LICENSE`](LICENSE) for the complete license text.

<div align="center">

**Uinx — readable enough for application code, explicit enough for a kernel.**

</div>
