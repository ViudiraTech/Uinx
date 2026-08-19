# Release Verification

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

Verification is reproducible from source and is intentionally **ephemeral**. Build directories, probe programs, and test-output transcripts are not required source artifacts and should not be committed merely to prove that a command ran once.

## Canonical verification

From a clean tree:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The Uinx 0.3 CTest configuration currently defines 18 top-level tests covering compiler/runtime units, inline assembly, executable I/O/inout paths, safe slice bounds behavior, C FFI, cross-target object generation, bare-metal linking, async suspend/resume, positive and negative conformance, borrow-safety rejection, and package tooling.

Negative suites require their expected diagnostic code rather than accepting any arbitrary compiler failure.

## Additional release checks

When preparing a release, also check the shipped standard-library sources and examples with `uinxc --emit=check`, and run the project formatting checks. Cross-target and bare-metal checks should use the CMake integration tests so target selection and linker behavior are exercised through the same path users receive.

Fuzzers and the compiler benchmark remain buildable project components. A short fuzz run or microbenchmark is useful during development but is not a proof of compiler correctness or a historical performance claim.

## Artifact policy

Temporary build trees and ad-hoc safety probes should live outside the repository (for example under `/tmp`) and be deleted after the run. CI may retain its own logs as CI artifacts, but this source tree does not need a growing collection of `ctest-*.txt` or build transcripts.

Release claims belong in `RELEASE_STATUS.md`, where implemented behavior is separated from properties that remain unverified.
