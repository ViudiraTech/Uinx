# Uinx Standard Library Layers

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

The source tree is split so a kernel can use low-level language facilities without pulling in libc or the hosted runtime.

## `core`

`stdlib/core` is the freestanding foundation. It contains `Option`, `Result`, iterator and foundational traits, slice layouts, raw-pointer/volatile access, atomics, byte-memory routines, and synchronization primitives.

### `core::mem`

`memcpy`, `memmove`, and `memset` are implemented in Uinx itself with typed raw-pointer arithmetic and dereference. The verified bare-metal paths do not require libc or hosted `uinx_mem*` hooks for these operations.

### `core::ptr`

Volatile `u8/u32/u64` read/write wrappers call compiler-recognized intrinsics that lower directly to LLVM volatile loads/stores. These are intended for MMIO and other explicitly volatile memory.

### `core::atomic`

`AtomicU64` exposes relaxed/acquire/release/acq_rel/compare-exchange operations. Compiler-recognized atomic ABI calls lower directly to LLVM atomic instructions in the verified freestanding paths.

### `core::sync`

`SpinLock` is a small freestanding lock built on `AtomicU64`. Kernels may wrap it with their own interrupt/preemption/lockdep/NUMA policy.

## `alloc`

`stdlib/alloc` defines Box, Vec/RawVec, String, Rc, Arc, HashMap and collection layouts. Hosted allocation is backed by runtime hooks. A bare-metal OS is expected to provide an allocator policy before using facilities that require dynamic allocation.

## `minimal`

`stdlib/minimal` is a cuttable hosted layer for file I/O, monotonic time, mutex synchronization, yielding/threads, and socket operations. Its current runtime backing is POSIX-oriented.

## `std`

`stdlib/std` layers process spawning/waiting, pipes/IPC, async executor ABI, UTF-8 utilities, file/socket/thread handles, and collection facade types on top of lower layers.

Complete Windows behavior, full Unicode normalization/collation, TLS/DNS service stacks, a production async reactor, and every collection algorithm remain outside the verified release surface.
