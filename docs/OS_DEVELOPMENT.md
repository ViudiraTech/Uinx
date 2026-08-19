# Writing a Bare-Metal OS in Uinx

Uinx 0.3 includes a freestanding kernel project path rather than treating `no_std` as only a compiler flag.

## Create a kernel

For x86-64:

```sh
uinx new mykernel --kernel=x86_64
cd mykernel
uinx build --release
```

Other implemented scaffolds are:

```sh
uinx new mykernel --kernel=aarch64
uinx new mykernel --kernel=riscv64
```

The package tool generates:

```text
mykernel/
├── uinx.toml
├── src/
│   └── main.ux
└── arch/<arch>/
    ├── start.S
    └── link.ld
```

A release build produces:

```text
target/release/mykernel.elf
```

The generated ELF is freestanding and statically linked without libc or the hosted Uinx runtime.

## Kernel source model

A kernel uses a dedicated exported entry instead of hosted `main()`:

```uinx
dontneed std
need core
smp auto

shared var online_cpus: u64 = 0

public unsafe concurrent func kernel_main() -> unit:
    online_cpus += 1
    return
```

`concurrent` marks an entry that may run simultaneously on multiple CPUs. In the default `smp auto` mode, compiler-visible mutable scalar state reached by concurrent code is strengthened to atomics where possible. Use `smp manual` to turn this inference off or `smp strict` for sequentially consistent generated atomics.

## Startup assembly

The generated architecture startup installs an early stack, calls `kernel_main`, and enters the architecture's idle/halt loop if the function returns. Uinx intentionally keeps firmware/bootloader policy out of the language frontend.

For a real operating system, startup code is where you extend the generated skeleton to parse your chosen boot protocol, establish page tables, initialize CPU-local state, enable the FPU/SIMD state you intend to use, and bring up secondary processors.

## Linker script

The generated linker script provides `ENTRY(_start)` and places `.text`, `.rodata`, `.data`, and `.bss` sections at the target's starter load address. Adjust it to match the boot protocol and virtual-memory layout of your kernel.

## Freestanding `core`

The OS-oriented core layer avoids libc dependencies for the implemented low-level paths.

### Memory

`core::mem` contains pure-Uinx implementations of:

```text
memcpy
memmove
memset
copy_bytes
move_bytes
fill_bytes
```

They are implemented with raw-pointer arithmetic and dereference and compile on x86-64, AArch64, and RISC-V64 without a hosted memory runtime.

### Volatile MMIO

`core::ptr` provides:

```text
read_volatile_u8/u32/u64
write_volatile_u8/u32/u64
```

The recognized volatile ABI calls lower directly to LLVM volatile loads/stores.

### Atomics

`core::atomic` provides `AtomicU64` with load/store/fetch-add/compare-exchange operations. Recognized atomic ABI calls lower directly to LLVM atomics.

### Locks

`core::sync` provides `SpinLock` as a freestanding primitive. A production kernel may wrap it with interrupt/preemption rules, lock debugging, lock ranking, scheduler integration, or architecture pause hints.

## Raw pointers

Uinx supports typed raw-pointer arithmetic for OS code:

```uinx
public unsafe func clear_page(base: mutptr u8, size: usize) -> unit:
    var i: usize = 0 as usize
    while i < size:
        deref (base + i) = 0
        i += 1 as usize
    return
```

Pointer arithmetic lowers to typed LLVM GEP rather than integer arithmetic on pointer values.

## Per-CPU state

```uinx
percpu var cpu_id: u64 = 0
```

`percpu` uses local-exec TLS. Before any `percpu` access, the OS must establish the target architecture's per-CPU TLS/thread-pointer base on every CPU. The starter assembly currently sets up the stack but does not guess a universal TLS layout.

## Inline assembly

Architecture operations remain available through real LLVM inline assembly:

```uinx
unsafe:
    asm("nop", volatile, clobber("memory"))
```

Use inline assembly for instructions that do not have a Uinx/core abstraction, and keep architecture-specific code behind narrow interfaces where possible.

## Direct compiler flow

The package tool is recommended, but the compiler can emit a freestanding object directly:

```sh
uinxc kernel.ux --target=x86_64-unknown-none --emit=obj -o kernel.o
```

Implemented release target triples include:

```text
x86_64-unknown-none
aarch64-unknown-none
riscv64-unknown-none-elf
```

The RISC-V kernel package path selects the `medany` code model required by its starter high physical address.

## What is not a boot protocol

Producing a valid freestanding ELF is not the same as implementing every firmware/bootloader contract. Uinx does not silently add Multiboot, Limine, UEFI, SBI, device-tree parsing, ACPI, AP startup, or platform drivers. Choose the boot environment for your OS and extend `start.S`/the linker script accordingly.

The compiler and package tests verify object generation and ELF link paths for all three starter architectures. Actual execution on every QEMU machine/firmware combination and physical board is a separate platform validation step.
