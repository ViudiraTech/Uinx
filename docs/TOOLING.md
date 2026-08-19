# Uinx Tooling

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

## `uinxc`

The direct compiler accepts source files and can check, emit LLVM IR, emit objects, or link hosted executables. Important system options include:

```text
--target=<triple>
--emit=check|llvm-ir|obj
--smp=auto|manual|strict
```

Example:

```sh
uinxc kernel.ux --target=x86_64-unknown-none --emit=obj -o kernel.o
```

## `uinx new`

Application/library projects:

```sh
uinx new app
uinx new lib --lib
uinx new freestanding --freestanding
```

`--no-std` remains a migration alias for `--freestanding`.

Kernel projects:

```sh
uinx new kernel --kernel=x86_64
uinx new kernel --kernel=aarch64
uinx new kernel --kernel=riscv64
```

Kernel generation writes a freestanding manifest, `src/main.ux`, architecture startup assembly, and a linker script. `uinx build --release` produces `target/release/<name>.elf` without libc/the hosted runtime.

## Package commands

Implemented primary commands include:

```text
uinx new
uinx build
uinx run
uinx check
uinx test
uinx fmt
uinx lint
uinx doc
uinx fetch
uinx add
```

SMP policy can be overridden for package checking/building:

```sh
uinx check --smp=manual
uinx build --smp=strict
```

`uinx run` rejects kernel packages because a bare-metal ELF is not a host process.

## Formatting

`uinx fmt` formats canonical indentation-based Uinx source. C/C++ formatting is defined by the project `.clang-format` and CMake `format` / `format-check` targets when clang-format is installed.
