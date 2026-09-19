# Uinx for VS Code

Systems programming without syntax noise — language support for Uinx (`.ux` / `.uxh`).

![Uinx logo](images/icon.png)

## Features

- Syntax highlighting (`source.uinx`) for canonical Uinx: indentation blocks, `func/struct/trait/extend`, ownership (`ref/mutref/borrow/move`), SMP (`concurrent/shared/percpu/smp/fence`), `async/await`, `unsafe`, `asm()`
- Language configuration: `#` comments, `:`-driven indentation, bracket pairs, folding
- Snippets: `func`, `concurrent`, `struct`, `shared`, `percpu`, `spinlock`, `atomic`, `fence`, `smp-sched`, `kernelmain`, …
- Language server (`uinx-lsp` over stdio): diagnostics on open/change, hover placeholder
- Fallback diagnostics via `uinxc --emit=check` when the server is unavailable
- Commands: Check / Build / Run / Test / Fmt / Lint / New project / Show LLVM IR / Restart server
- Task provider (`type: uinx`) + `$uinx` problem matcher for `file:line:col: error[EXXXX]` output
- Settings for toolchain paths, `--smp`, `--target`, headers, tracing, format-on-save

## Requirements

Build the toolchain first:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Ensure `uinx-lsp`, `uinxc` and `uinx` are on `PATH`, or set:

- `uinx.server.path`
- `uinx.uinxc.path`
- `uinx.tool.path`

## Usage

1. Open a `.ux` file — the extension activates.
2. `Ctrl+Shift+B` builds, `Ctrl+Shift+C` checks.
3. Command palette → `Uinx: …` for run/test/fmt/lint/new project/show IR.
4. Tasks: create a task with `"type": "uinx", "command": "build"`.

```json
{
  "version": "2.0.0",
  "tasks": [
    { "type": "uinx", "command": "check", "problemMatcher": ["$uinx"] },
    { "type": "uinx", "command": "build", "problemMatcher": ["$uinx"] }
  ]
}
```

Freestanding example (`smp-sched` snippet):

```uinx
dontneed std
need core
smp auto

shared var completed: u64 = 0
percpu var ticks: u64 = 0

public unsafe concurrent func cpu_entry() -> unit:
    loop:
        ticks += 1
        completed += 1
        compiler_fence release
```

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `uinx.server.path` | `uinx-lsp` | Language server binary |
| `uinx.uinxc.path` | `uinxc` | Compiler for fallback diagnostics / IR |
| `uinx.tool.path` | `uinx` | Project tool |
| `uinx.smp` | `auto` | `--smp` override |
| `uinx.target` | `` | `--target` triple override |
| `uinx.enableHeader` | `false` | `-enable-header` |
| `uinx.includeDirs` | `[]` | `-I` dirs |
| `uinx.trace.server` | `off` | LSP trace |
| `uinx.diagnostics.fallbackToUinxc` | `true` | uinxc on save when LSP down |
| `uinx.format.onSave` | `false` | `uinx fmt` on save |

## Known limits

`uinx-lsp` currently provides full diagnostics + hover placeholder; completion/navigation/refactoring remain `UNVERIFIED` in the language release status. The extension degrades gracefully to `uinxc` diagnostics.

## License

BSD-3-Clause — see repository `LICENSE`. Copyright © 2026 ViudiraTech.
