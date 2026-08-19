# Uinx Standard Library Layers

**by JiTianYu391**

The source tree is split so freestanding projects do not need to import hosted facilities.

## `core`

`stdlib/core` contains `Option`, `Result`, iterator traits, immutable/mutable slice layouts, atomics, pointer/volatile functions, byte memory primitives and foundational traits including Copy, Drop, Send and Sync.

`core` source is marked `dontneed std` and has no OS allocation policy. Some low-level functions are declarations against the runtime ABI when used in hosted tests; a freestanding environment may provide its own ABI definitions.

## `alloc`

`stdlib/alloc` defines Box, Vec/RawVec, String, Rc, Arc, HashMap and collection layouts. The C runtime provides allocator hooks, dynamically growing raw-vector storage, atomic Arc control blocks and an open-addressing hash map.

The raw runtime storage implementations are exercised by `runtime-unit`. Rich typed convenience methods for every collection operation and generic Drop specialization in all combinations remain `UNVERIFIED`.

## `minimal`

`stdlib/minimal` exposes a cuttable hosted layer for file I/O, monotonic time, mutex synchronization, yielding/threads and socket operations. The backing POSIX implementations are present in `runtime/`.

## `std`

`stdlib/std` layers process spawning/waiting, pipes/IPC, async executor ABI, UTF-8 utilities, file/socket/thread handles and collection facade types on top of lower layers.

The hosted runtime tests cover process spawn/wait, pipe I/O, mutexes, threads, file-adjacent primitives, sockets at the ABI implementation level, UTF-8, atomics, volatile memory, Vec, Arc and HashMap.

Complete Windows behavior is `UNVERIFIED`; unsupported Windows runtime branches return platform errors rather than pretending success. Full Unicode normalization/collation, TLS, DNS, advanced filesystem metadata, a production async network reactor and every collection algorithm are `UNVERIFIED`.
