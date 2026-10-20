## Summary

<!-- What does this change and why? -->

## Checklist

- [ ] `cmake --preset ci && cmake --build --preset ci && ctest --preset ci` passes
- [ ] `clang-format` is clean (`cmake --build <dir> --target format`)
- [ ] New language behavior has a test under `tests/` (`conformance/`, `safety/`, `codegen/` or `runtime/`)
- [ ] User-visible changes are recorded in `CHANGELOG.md`
