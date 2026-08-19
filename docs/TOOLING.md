# Uinx Tooling

**by JiTianYu391**

## `uinxc`

The standalone compiler accepts one or more Uinx source files and supports semantic checking, LLVM IR emission, object emission, executable linking, target triples and optimization selection.

## `uinx`

The package/build tool reads `uinx.toml`, recursively collects package sources and local path dependencies, writes a deterministic lock record with source hashes, and dispatches the shared compiler pipeline.

Commands implemented in this release:

- `uinx new [--lib] [--freestanding]`

`--no-std` remains accepted as a migration alias for `--freestanding`; new documentation and generated workflows use `--freestanding`.
- `uinx add NAME --path PATH`
- `uinx fetch`
- `uinx check`
- `uinx build`
- `uinx run`
- `uinx test`
- `uinx fmt [--check]`
- `uinx lint`
- `uinx doc`
- `uinx help`
- `uinx version`

The integration test executes the complete command sequence for local path packages and also validates observable `need std`, `dontneed std`, and `dontneed dependency` source-selection behavior.

A remote registry, cryptographic package signatures, network dependency fetching and content-addressed global caches are `UNVERIFIED` and are not part of the verified package-manager surface.

## Formatter

The formatter normalizes indentation and trailing whitespace deterministically and has a check-only mode suitable for CI. Full comment-aware canonical layout for every future syntax form is `UNVERIFIED`.

## Linter

The linter reuses parser, resolver, type checker and ownership/borrow checker diagnostics rather than implementing a disconnected parser. A broad style/performance lint catalog is `UNVERIFIED`.

## LSP

`uinx-lsp` speaks JSON-RPC over stdio and implements initialization, shutdown/exit, open/change document diagnostics and basic hover information. Completion, go-to-definition, references, rename, workspace symbols and semantic tokens are `UNVERIFIED`.

## Documentation generator

`uinx doc` parses project sources and writes Markdown API documentation from declarations. It participates in the package-tooling integration test.
