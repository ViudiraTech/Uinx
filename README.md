<div align="center">

# Uinx

### Readable syntax. Systems-level control.

A modern, ahead-of-time compiled systems programming language with  
**Python-style readability**, **native performance**, and **memory safety without a garbage collector**.

<br>

![Version](https://img.shields.io/badge/version-0.2.0-4C8BF5?style=flat-square)
![C++](https://img.shields.io/badge/compiler-C%2B%2B20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![LLVM](https://img.shields.io/badge/backend-LLVM-262D3A?style=flat-square&logo=llvm)
![License](https://img.shields.io/badge/license-BSD--3--Clause-44CC11?style=flat-square)
![GC](https://img.shields.io/badge/GC-none-success?style=flat-square)

**Developed by ViudiraTech · Code by JiTianYu391**

</div>

---

## Overview

**Uinx** is a native systems programming language designed for operating systems, kernels, drivers, embedded software, high-performance applications, and general native development.

Uinx combines low-level control with a deliberately simple, indentation-based syntax.

```uinx
func add(a: i32, b: i32) -> i32:
    return a + b

func main() -> i32:
    val answer = add(40, 2)
    return answer - 42
```

No mandatory braces.

No mandatory semicolons.

No garbage collector.

No transpilation to C or C++.

Uinx compiles through its own native compiler pipeline into LLVM IR and native machine code.

---

## Design goals

| | Uinx |
|---|---|
| Compilation | Ahead-of-time |
| Compiler | Modern C++20 |
| Backend | LLVM |
| Type system | Strong static typing |
| Memory management | Ownership + borrowing + RAII |
| Garbage collector | None |
| Abstraction model | Zero-cost |
| Syntax | Indentation-based |
| C interoperability | Native C FFI |
| Inline assembly | Supported |
| Async / Await | Supported |
| Freestanding development | Supported |
| License | BSD 3-Clause |

Uinx is designed around one principle:

> **Low-level programming should be powerful without being painful to read.**

---

# Language

## Functions

```uinx
func square(value: i32) -> i32:
    return value * value
```

## Variables

Immutable values use `val`:

```uinx
val answer = 42
```

Mutable values use `var`:

```uinx
var counter = 0
counter = counter + 1
```

## Conditions

```uinx
if value > 100:
    return 2
elif value > 50:
    return 1
else:
    return 0
```

Logical expressions can use readable operators:

```uinx
if ready and not stopped:
    start()
```

## Structures

```uinx
struct Point:
    x: i32
    y: i32
```

Create values with `new`:

```uinx
val point = new Point(x=10, y=20)
```

## Extensions

Methods can be implemented with `extend`:

```uinx
struct Counter:
    value: i32

extend Counter:
    func get(self: ref Self) -> i32:
        return self.value

    func set(self: mutref Self, value: i32) -> unit:
        self.value = value
```

Usage:

```uinx
func main() -> i32:
    var counter = new Counter(value=40)

    counter.set(42)

    return counter.get() - 42
```

---

# `need` and `dontneed`

Uinx lets source code explicitly describe the environment it needs.

```uinx
need std
```

Or:

```uinx
need core
```

For freestanding software:

```uinx
dontneed std
need core
```

Components can also be explicitly disabled:

```uinx
dontneed runtime
```

The intent is simple:

```text
need X
```

means:

> This program needs X.

while:

```text
dontneed X
```

means:

> Do not include or link X.

This replaces special-case syntax such as `no_std`.

---

# References and pointers

Safe references are written explicitly:

```uinx
ref T
mutref T
```

Raw pointers use:

```uinx
ptr T
mutptr T
```

Borrowing is readable:

```uinx
val reference = borrow value
```

Mutable borrowing:

```uinx
val reference = borrow mut value
```

Dereferencing:

```uinx
val value = deref pointer
```

The distinction between safe references and raw pointers remains visible in source code.

---

# Generics

Generic types use brackets:

```uinx
Vec[i32]
```

Generic functions:

```uinx
func identity[T](value: T) -> T:
    return value
```

Explicit specialization:

```uinx
val result = identity[i32](42)
```

---

# C FFI

Uinx can directly call C ABI functions.

```uinx
extern "C" func abs(value: i32) -> i32

func main() -> i32:
    return abs(-1) - 1
```

C interoperability is a first-class part of the language rather than an external transpilation layer.

---

# Unsafe code

Operations that cannot be verified by the safe language can be placed behind `unsafe`.

This includes functionality such as:

- raw pointer access
- low-level hardware operations
- foreign interfaces
- architecture-specific operations
- inline assembly

Safe Uinx code remains separate from explicitly unsafe operations.

---

# Inline assembly

Uinx supports real native inline assembly through the compiler backend.

It is intended for:

- kernels
- boot code
- interrupt handling
- device drivers
- architecture support
- highly specialized low-level routines

Assembly is lowered through LLVM rather than interpreted or emulated by the language.

---

# Compiler architecture

Uinx uses a native compiler pipeline:

```text
Source
  │
  ▼
Lexer
  │
  ▼
Indentation-aware Parser
  │
  ▼
AST
  │
  ▼
Name Resolution
  │
  ▼
Type Checking
  │
  ▼
Trait Checking
  │
  ▼
Ownership / Borrow Checking
  │
  ▼
MIR
  │
  ▼
Optimization
  │
  ▼
LLVM IR
  │
  ▼
Object Code
  │
  ▼
Linker
  │
  ▼
Native Executable
```

Uinx does **not** translate programs into C or C++ before compilation.

---

# Standard library

The standard library is divided into layers:

```text
core
 │
 ▼
alloc
 │
 ▼
minimal
 │
 ▼
std
```

### `core`

Fundamental language and freestanding facilities.

```uinx
need core
```

### `alloc`

Allocation-related facilities.

```uinx
need alloc
```

### `minimal`

A small runtime and I/O layer for constrained environments.

```uinx
need minimal
```

### `std`

The hosted standard-library layer.

```uinx
need std
```

For kernels, drivers, bare metal, and other freestanding targets:

```uinx
dontneed std
need core
```

---

# Building Uinx

## Requirements

You need:

- CMake
- a C++20 compiler
- LLVM development files
- a supported system linker

Configure:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Build:

```sh
cmake --build build -j
```

Run the test suite:

```sh
ctest --test-dir build --output-on-failure
```

---

# Compiling a Uinx program

Compile directly with the compiler:

```sh
build/uinxc examples/hello.ux -o hello
```

Run:

```sh
./hello
```

---

# Package tool

Create a project:

```sh
build/uinx new app
```

Enter it:

```sh
cd app
```

Check the project:

```sh
../build/uinx check
```

The package tool provides commands including:

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

---

# Formatting

The repository contains a canonical:

```text
.clang-format
```

If `clang-format` is installed:

```sh
cmake --build build --target format
```

Check formatting without modifying files:

```sh
cmake --build build --target format-check
```

Uinx source itself can be formatted with:

```sh
uinx fmt
```

---

# Repository layout

```text
Uinx-Language/
├── benchmarks/
├── cmake/
├── docs/
├── examples/
│   └── baremetal/
├── fuzz/
├── include/
│   └── uinx/
├── runtime/
├── src/
├── stdlib/
│   ├── core/
│   ├── alloc/
│   ├── minimal/
│   └── std/
├── tests/
│   ├── unit/
│   ├── conformance/
│   ├── safety/
│   ├── codegen/
│   └── runtime/
├── tools/
├── verification/
├── ARCHITECTURE.md
├── CMakeLists.txt
├── LICENSE
└── README.md
```

---

# Tests

The test suite is organized by purpose rather than as a collection of demonstration files.

```text
tests/
├── unit/                 Compiler and runtime unit tests
├── conformance/
│   ├── pass/             Programs that must compile
│   └── fail/             Programs that must be rejected
├── safety/
│   └── borrow-fail/      Ownership and borrowing rejection tests
├── codegen/
│   └── asm/              Inline assembly / backend tests
├── runtime/              Executable runtime tests
└── support/              Native test support code
```

Negative tests can require specific compiler diagnostics so that a test does not accidentally pass simply because compilation failed for an unrelated reason.

---

# Documentation

More detailed documentation is available in:

| Document | Purpose |
|---|---|
| `ARCHITECTURE.md` | Compiler architecture |
| `docs/LANGUAGE_SPEC.md` | Language specification |
| `docs/UNSAFE_AND_ASM.md` | Unsafe and inline assembly rules |
| `docs/VERIFICATION.md` | Verification and testing |
| `tests/README.md` | Test-suite organization |

---

# Philosophy

Uinx does not try to make systems programming look complicated just because the underlying machine is complicated.

The language aims for code that reads naturally:

```uinx
dontneed std
need core

func max(a: i32, b: i32) -> i32:
    if a > b:
        return a

    return b
```

while still retaining the facilities required to build:

- operating systems
- kernels
- device drivers
- boot software
- embedded systems
- runtimes
- native libraries
- high-performance applications

Readable syntax should not require giving up control over the machine.

---

# License

Uinx is distributed under the **BSD 3-Clause License**.

See [`LICENSE`](LICENSE) for the complete license text.

```text
Copyright (c) 2026 ViudiraTech
```

Source code attribution:

```text
By JiTianYu391
```

---

<div align="center">

### Uinx

**Simple to read. Close to the machine.**

Copyright © 2026 **ViudiraTech**  
Code by **JiTianYu391**

</div>
