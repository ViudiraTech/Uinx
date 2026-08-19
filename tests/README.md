# Uinx Test Suite

The test tree is organized by verification purpose rather than by ad-hoc examples.

- `unit/`: host-language unit tests for compiler/runtime components.
- `conformance/pass/`: language programs that must compile successfully.
- `conformance/fail/`: language programs that must fail with a specific stable diagnostic.
- `safety/borrow-fail/`: ownership, move, lifetime, and aliasing rejection tests.
- `codegen/asm/`: target-specific inline-assembly code generation tests.
- `runtime/async/`: executable async suspend/resume tests.
- `support/`: C harnesses used by executable tests.

Every `.ux` test carries a stable `TEST` identifier and an `EXPECT` contract. Negative tests also carry an
`EXPECT-DIAGNOSTIC` code. The CMake harness rejects a negative test that fails for the wrong reason.
