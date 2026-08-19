# Verification Artifacts

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

This directory contains verification records regenerated for the Uinx 0.3 source tree. Historical logs are not used as evidence for modified code.

- `environment.txt`: compiler/linker and host environment.
- `release-build.txt`: clean Release configure/build summary.
- `ctest-release.txt`: the 18-test final CTest run.
- `stdlib-check.txt`: aggregate semantic check of all 32 standard-library Uinx sources.
- `examples-check.txt`: individual semantic checks for all shipped Uinx examples.
- `os-freestanding.txt`: three-architecture kernel ELF and freestanding-core verification summary.
- `optimizer-check.txt`: O0/O2 MIR optimization probe.
- `compiler-benchmark.txt`: current-tree lexer/parser/check-pipeline microbenchmark output (not a historical speedup claim).

Package integration tests create, check, and release-build x86-64, AArch64, and RISC-V64 kernel projects. The direct bare-metal integration test separately cross-compiles architecture startup code and links freestanding ELFs.

Actual boot on every firmware/QEMU/hardware platform remains a separate platform validation because a generic ELF does not define one universal boot protocol.
