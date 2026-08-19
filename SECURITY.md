# Security Policy

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

Uinx treats compiler crashes, safe-language memory-safety escapes, borrow-checker soundness failures, malformed-input parser crashes, package path traversal and runtime memory corruption as security-relevant defects.

Do not classify `unsafe` code that violates its documented preconditions as a safe-language escape. A valid report should include the smallest reproducible Uinx source, target triple, compiler invocation and observed diagnostic or generated behavior.

The release includes sanitizing fuzz targets for lexer/parser input and negative ownership suites. Formal soundness is not claimed; the remaining verification boundaries are listed in `docs/RELEASE_STATUS.md`.
