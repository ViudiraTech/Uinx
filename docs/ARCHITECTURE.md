# Uinx Compiler Architecture

**Copyright © 2026 ViudiraTech · Code by JiTianYu391**

## Pipeline

```text
Source files
  -> Lexer
  -> Parser / AST
  -> Name Resolution
  -> Semantic Model / HIR-facing symbols
  -> Type Checker + Trait Solver
  -> Ownership / Borrow Checker
  -> MIR
  -> MIR Optimization
  -> LLVM IR
  -> Clang LLVM Backend
  -> Object
  -> Linker
  -> Executable / freestanding ELF
```

Every diagnostic phase shares source locations and a single diagnostic engine. A failed semantic or ownership phase prevents backend emission.

## Front end

`src/lexer.cpp` tokenizes identifiers, literals, comments, punctuation, operators and language keywords. `src/parser.cpp` uses recursive descent and precedence parsing to build the owning AST from `include/uinx/ast.hpp`.

The parser recognizes functions, structs, generic parameters and bounds, traits, implementations, `extern "C"`, `unsafe`, async/await, blocks, local bindings, conditionals, loops, returns, calls, methods, member access, safe indexing, references, raw pointers, casts, struct construction and inline assembly.

## Resolution and semantic model

`src/sema.cpp` collects declarations and builds `SemanticModel`; name resolution and the trait solver live in `src/sema_resolve.cpp`, statement and expression checking in `src/sema_stmt.cpp` and `src/sema_expr.cpp`, and concurrency propagation plus the `E0363` shared-state check in `src/sema_smp.cpp`. Function signatures retain generic parameters, trait bounds, receiver types, safety and async properties. Trait implementations are checked for duplicate implementations, duplicate methods, missing methods and signature mismatch.

Type inference is local and constraint-oriented: declared parameter/result types are fixed while local bindings infer from initializers. Generic function calls infer type arguments from actual argument types when explicit arguments are not supplied.

## Ownership and borrowing

`src/borrow.cpp` drives statement-level checking and tracks places, moves and loans (place helpers in `borrow_places.cpp`, loan creation/access/reborrows in `borrow_loans.cpp`, liveness in `borrow_liveness.cpp`, branch/loop state merging and expression inspection in `borrow_flow.cpp`). A place may be a local, a field path or a conservatively normalized indexed path. Shared borrows may coexist; active mutable loans are exclusive. Mutable references are affine and cannot be implicitly copied.

Last-use information shortens loans before lexical scope end, providing NLL-style behavior. Field paths permit disjoint field borrowing. Indexed paths intentionally alias conservatively unless a stronger proof is available.

## MIR

`src/mir.cpp`, `mir_stmt.cpp` and `mir_expr.cpp` lower typed AST into control-flow blocks and typed instructions. MIR contains storage slots, loads/stores, arithmetic, comparisons, branch/jump, calls, casts, field and index addresses, struct construction, inline assembly, await operations and explicit scope drops.

The MIR optimizer removes instructions unreachable after terminators and canonicalizes the emitted block stream. LLVM optimization levels are then delegated to Clang during object generation.

## LLVM backend

`src/codegen.cpp` serializes valid textual LLVM IR (functions in `codegen_function.cpp`, async state machines in `codegen_async.cpp`, inline assembly and syscalls in `codegen_asm.cpp`). It emits named struct layouts, specialized generic layouts, function definitions/declarations, control flow, GEPs, slice bounds checks, inline assembly and async state machines.

The backend does not generate C or C++. The driver writes `.ll`, asks Clang to compile LLVM IR into an object, and performs linking as a separate process. This preserves the required `IR -> Object -> Link` boundary.

## Async ABI

An async function constructor returns a `%uinx.future` pair containing frame state and a vtable. The vtable exposes poll and drop functions. Await points become numbered resume states. Pending child futures remain stored in the parent frame; successful completion drops the child and advances state. The release test forces a real suspension before readiness.

## Runtime

The C runtime is deliberately small and ABI-oriented. It supplies allocator hooks, raw vector storage, atomic Arc control blocks, hash table storage, UTF-8 primitives, file/socket/thread/process helpers, synchronization, atomics, memory operations, volatile access, timing and a minimal future executor.

Language safety must not depend on the runtime accepting invalid safe-language operations: safe slice indexing is checked in generated code, and raw memory operations are gated behind unsafe source constructs.

## Tooling

`tools/uinx.cpp` implements manifests, recursive source collection, local path dependencies, a lock file, build/check/run/test, formatting, linting and documentation generation. `tools/uinx_lsp.cpp` provides JSON-RPC/LSP initialization plus document diagnostics driven by the same compiler front end.
