# Uinx Language Specification

This document describes the canonical source syntax implemented by the current compiler frontend.

## 1. Design goals

Uinx is a statically typed AOT systems language with no tracing GC. The language keeps explicit control over allocation, raw memory, FFI and inline assembly while making ordinary source code visually closer to Python than to Rust/C++.

Canonical Uinx uses indentation-sensitive suites. Four spaces are the project style; tabs in indentation are rejected by the lexer. Blank lines and comments do not change block depth.

Comments use `#`. `//` and nested `/* ... */` remain accepted for migration/source interoperability.

## 2. Modules and requirements

A module may begin with requirement directives:

```uinx
dontneed std
need core
```

`need name` records a component requirement. `dontneed name` records that the compiler/package layer must not automatically connect that component. A component cannot be both needed and disabled in the same module.

For the bundled package tool:

- `need std` selects the full hosted standard library;
- `need minimal`, `need alloc`, or `need core` selects that highest bundled layer;
- `dontneed std` selects no bundled hosted standard-library layer and suppresses the normal hosted-runtime link;
- `dontneed runtime` suppresses the runtime archive link;
- `dontneed dependency_name` excludes that direct manifest path dependency from source collection.

`no_std;` is accepted only as a migration alias for `dontneed std`.

## 3. Declarations

Functions use `func` and a colon-terminated suite:

```uinx
func add(a: i32, b: i32) -> i32:
    return a + b
```

Modifiers precede the declaration and can be combined:

```uinx
public func visible() -> i32:
    return 0

unsafe func privileged(address: mutptr u8) -> unit:
    return

async func compute() -> i32:
    return 42

extern "C" func abs(value: i32) -> i32
```

An `extern` declaration has no body. `public` controls language-level visibility. Calls to an `unsafe func` require an unsafe context.

## 4. Bindings and mutability

`val` creates an immutable local; `var` creates a mutable local:

```uinx
val answer = 42
val typed: i64 = 42
var counter = 0
counter += 1
```

Assignment to a `val` is rejected. A mutable borrow also requires a mutable place.

## 5. Structs and construction

```uinx
struct Pair[T]:
    left: T
    right: T

val pair = new Pair[i32](left=20, right=22)
```

Fields are listed one per logical line. Struct construction is explicit with `new`, which avoids the old brace-literal ambiguity with control-flow blocks.

## 6. Generics

Generic declarations and applications use square brackets:

```uinx
func identity[T: Copy](value: T) -> T:
    return value

val value = identity[i32](42)
```

Multiple parameters are comma-separated. Trait bounds use `:` and `+`.

## 7. Traits and extensions

Traits declare contracts:

```uinx
trait Readable:
    func read(self: ref Self) -> i32
```

Inherent methods use `extend Type:`. Trait implementations use `extend Type with Trait:`:

```uinx
extend Cell with Readable:
    func read(self: ref Self) -> i32:
        return self.value
```

An intentionally empty suite is written with `pass`.

## 8. References, pointers and ownership

Canonical type spellings are:

- `ref T`: shared reference;
- `mutref T`: exclusive mutable reference;
- `ptr T`: const/raw pointer;
- `mutptr T`: mutable raw pointer.

Canonical expression spellings are:

```uinx
val shared = borrow value
val unique = borrow mut value
val copied = deref shared
```

Shared references to copyable data are copyable. Mutable references are affine. Non-`Copy` values are moved when consumed by value, and use-after-move is rejected.

Loans are shortened to relevant last use. Field-sensitive place tracking permits non-overlapping field loans; indexed places are conservatively treated as potentially overlapping.

## 9. Control flow

```uinx
if ready:
    run()
elif retryable:
    retry()
else:
    fail()

while active:
    tick()
```

A lexical lifetime-only block can be written explicitly:

```uinx
scope:
    val temporary = 1
```

`return` ends the current function. Non-`unit` functions must return a compatible value along accepted MIR paths.

## 10. Expressions and operators

Uinx implements literals, names, calls, method calls, member/index access, unary and binary operations, casts, borrowing, dereference, struct construction, await and inline assembly.

`and`, `or`, and `not` are canonical logical spellings; `&&`, `||`, and `!` remain accepted. Arithmetic/comparison operators use conventional precedence. Casts use `as`:

```uinx
val raw = borrow value as ptr i32
```

## 11. Safe indexing

`Slice[T]` and `SliceMut[T]` safe indexing emits an unsigned bounds comparison before pointer arithmetic. An out-of-range execution enters an `llvm.trap` path.

## 12. RAII and Drop

A non-`Copy` value with a `Drop` implementation receives MIR destruction at normal scope exit:

```uinx
extend Resource with Drop:
    func drop(value: mutref Resource) -> unit:
        return
```

Complete unwinding semantics and every nested generic destructor case remain outside the verified surface.

## 13. Unsafe code

Unsafe context is introduced by an `unsafe func` body or an explicit suite:

```uinx
unsafe:
    asm("nop", volatile, clobber("memory"))
```

Unsafe context permits operations that can invalidate memory-safety invariants, including raw-pointer dereference/arithmetic and inline assembly. Safe callers must cross an explicit unsafe boundary.

## 14. C FFI

`extern "C" func` uses C linkage. Scalar/reference/raw-pointer FFI is supported by the implemented ABI path. Full aggregate ABI equivalence across every target/compiler remains unverified.

## 15. Inline assembly

`asm()` lowers directly to LLVM inline assembly. Supported operands are `in`, `out`, `inout`, `clobber`, and `volatile`.

```uinx
unsafe:
    asm("mov $1, $0", out("=r", output), in("r", input), volatile, clobber("cc"))
```

Architecture register validation is implemented for x86_64, AArch64 and RISC-V64 paths covered by the test suite.

## 16. Async/await

```uinx
async func answer() -> i32:
    val value = await child()
    return value
```

Async lowering produces a future frame and poll/drop machinery with saved resume state. Hosted construction currently uses the runtime allocator; allocator-free bare-metal async remains outside the verified surface.

## 17. Compatibility syntax

For migration, the lexer/parser still accepts the earlier aliases including `fn`, `pub`, `let`, `let mut`, brace suites, semicolons, `&T`, `&mut T`, `*const T`, `*mut T`, angle-bracket generics and `impl Trait for Type`. They are not the canonical syntax and are not used by the shipped examples/stdlib/tests.

## 18. Diagnostics

Diagnostics carry source ranges and stable codes. Negative conformance tests declare `EXPECT-DIAGNOSTIC` and must fail for that specific reason; arbitrary compiler failure is not accepted as a passing negative test.
