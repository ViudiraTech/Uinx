# Contributing to Uinx

Thanks for helping out. Security-relevant reports (compiler crashes,
borrow-checker soundness failures, safe-language memory-safety escapes) should
follow [SECURITY.md](SECURITY.md) instead of the public issue tracker.

## Build and test

```sh
cmake --preset ci
cmake --build --preset ci
ctest --preset ci
```

Other presets: `debug`, `release`, `asan` (ASan + UBSan), `fuzz` (Clang +
libFuzzer). Toolchain requirements are listed in [docs/BUILD.md](docs/BUILD.md).

## Repository layout

| Path | Contents |
|---|---|
| `include/uinx/` | public compiler headers |
| `src/` | compiler front end, borrow checker, MIR, LLVM IR backend |
| `tools/` | `uinx`, `uinxc`, `uinx-lsp` executables |
| `runtime/` | C runtime linked into hosted programs |
| `stdlib/` | standard library in Uinx |
| `tests/` | unit, conformance, safety, codegen and runtime tests |
| `cmake/` | CTest driver scripts (`Run*.cmake`) |
| `examples/` | sample programs, also exercised by tests |
| `fuzz/`, `benchmarks/` | libFuzzer targets and compiler benchmark |
| `editors/` | editor integrations (VS Code) |
| `docs/` | specification, architecture, build and design documents |

## Tests

Every language-level change needs a test in `tests/`; see
[tests/README.md](tests/README.md). Negative tests must carry the stable
`EXPECT-DIAGNOSTIC` code they are meant to hit.

## Style

C and C++ are formatted with `clang-format` using the checked-in `.clang-format`:

```sh
cmake --build build/ci --target format
```

CI runs `clang-format --dry-run --Werror` and builds with warnings as errors.

## Commits and pull requests

Use short imperative subjects, preferably `area: change` (for example
`borrowck: reject dangling field reference`). Record user-visible changes in
[CHANGELOG.md](CHANGELOG.md).

Build outputs, logs and benchmark transcripts are not committed.
