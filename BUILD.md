# Building Uinx

**by JiTianYu391**

## Requirements

Verified release environment:

- CMake 3.31.6
- Ninja 1.12.1
- GCC 14 or Clang 17 for the compiler implementation
- Clang 17 for LLVM IR to object lowering
- LLD 17 for freestanding cross-target linking
- POSIX threads and sockets for the host runtime tests

The project intentionally does not require LLVM C++ development headers. `uinxc` emits LLVM IR directly and invokes a configured Clang driver for LLVM IR to object conversion. Object linking is a separate phase.

## Release build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## Clang + fuzz build

```sh
cmake -S . -B build-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DUINX_BUILD_FUZZ=ON
cmake --build build-clang -j2
ctest --test-dir build-clang --output-on-failure
./build-clang/uinx-lexer-fuzz -runs=2000
./build-clang/uinx-parser-fuzz -runs=2000
```

The fuzz targets use libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer.

## Install

```sh
cmake --install build --prefix "$HOME/.local"
```

This installs:

- `bin/uinxc`
- `bin/uinx`
- `bin/uinx-lsp`
- `lib/libuinx_runtime.a`
- `lib/uinx/stdlib/...`
- `include/uinx/...`
- `include/uinx/runtime/runtime.h`

## Direct compiler examples

Check without object generation:

```sh
build/uinxc examples/generics.ux --emit=check
```

Emit LLVM IR:

```sh
build/uinxc examples/hello.ux --emit=llvm-ir -o hello.ll
```

Emit an object:

```sh
build/uinxc examples/freestanding.ux --target=riscv64-unknown-linux-gnu --emit=obj -o freestanding.o
```

Build and link an executable:

```sh
build/uinxc examples/hello.ux -o hello
./hello
```

## Freestanding examples

CTest target `baremetal-cross-link` performs the complete freestanding build for x86_64, AArch64 and RISC-V64. It compiles `examples/baremetal/kernel.ux`, assembles the architecture-specific `_start`, and links with the matching linker script through LLD without libc.

The produced ELFs are link-level artifacts. Booting them on physical hardware or emulators was not performed in this release and is therefore `UNVERIFIED`.
