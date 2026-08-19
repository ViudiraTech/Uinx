# Uinx 0.3 Language Specification

This document describes the canonical source syntax implemented by the current compiler frontend. Migration aliases may still parse, but new Uinx code should use the syntax documented here.

## 1. Lexical structure

Uinx is indentation-sensitive. A suite starts after `:` and uses indentation; project style is four spaces. Tabs in indentation are rejected. Blank lines and comments do not alter block depth.

```uinx
# canonical comment
func answer() -> i32:
    return 42
```

`//` and nested `/* ... */` remain migration/interoperability comments, not the preferred style.

## 2. Requirements and SMP policy

```uinx
dontneed std
need core
smp auto
```

Implemented requirements:

- `need name`: request a component;
- `dontneed name`: suppress a component;
- `dontneed std`: canonical freestanding form;
- `dontneed runtime`: suppress hosted runtime linkage;
- `dontneed dependency`: exclude a direct manifest path dependency.

A component cannot be both needed and disabled in the same module. `no_std;` is a migration alias only.

The implemented SMP modes are `smp auto`, `smp manual`, and `smp strict`; see `MEMORY_MODEL.md`.

## 3. Functions and modifiers

```uinx
func add(a: i32, b: i32) -> i32:
    return a + b

public unsafe concurrent func cpu_entry() -> unit:
    return

async func task() -> i32:
    return 42

extern "C" func abs(value: i32) -> i32
```

Implemented declaration modifiers include `public`, `unsafe`, `async`, `concurrent`, and `extern "C"` where semantically valid. Extern declarations have no body.

## 4. Local bindings and globals

```uinx
val immutable = 42
var mutable: i64 = 0
mutable += 1

const PAGE_SHIFT: u64 = 12
static var flags: u64 = 0
shared var online_cpus: u64 = 0
percpu var local_ticks: u64 = 0
```

`val` is immutable and `var` is mutable. `const` is a module constant. `static var/val` declares module storage. `shared` marks atomic-compatible shared storage. `percpu` marks CPU-local TLS storage.

## 5. Structures and construction

```uinx
struct Pair[T]:
    left: T
    right: T

struct QueueStats:
    shared pushes: u64
    local_hint: u64

val pair = new Pair[i32](left=20, right=22)
```

Construction uses `new Type(field=value, ...)`.

## 6. Traits and extensions

```uinx
trait Readable:
    func read(self: ref Self) -> i32

extend Cell:
    public func clear(self: mutref Self) -> unit:
        self.value = 0
        return

extend Cell with Readable:
    func read(self: ref Self) -> i32:
        return self.value
```

Use `pass` for an intentionally empty suite.

## 7. Generics

```uinx
func identity[T: Copy](value: T) -> T:
    return value

val result = identity[i32](42)
```

Generic declarations/applications use square brackets. Multiple bounds use `+`.

## 8. References, pointers, ownership

Types:

```text
ref T       shared safe reference
mutref T    exclusive mutable reference
ptr T       raw const pointer
mutptr T    raw mutable pointer
```

Expressions:

```uinx
val shared = borrow value
val unique = borrow mut value
val copied = deref shared
```

Raw-pointer arithmetic and dereference require unsafe context. Typed pointer `+`/`-` integer arithmetic lowers as element-address computation, and raw-pointer dereference is a valid assignment place:

```uinx
unsafe:
    deref (buffer + index) = byte
```

Non-`Copy` values move on by-value use; mutable references are affine. The borrow checker performs last-use loan shortening and field-sensitive place tracking for the verified forms.

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

for i in 0 .. 10:
    consume(i)

for i in 0 ..= 10:
    consume(i)

loop:
    if done:
        break
    if skip:
        continue

scope:
    val temporary = 1
```

Implemented statements include `if/elif/else`, `while`, integer-range `for`, `loop`, `break`, `continue`, `scope`, `return`, and `pass`.

## 10. Expressions and operators

Implemented expression forms include literals, names, calls, method calls, member/index access, construction, borrow/deref, casts, `await`, and `asm`.

Canonical logical operators are:

```text
and  or  not
```

Arithmetic/bitwise operators include:

```text
+  -  *  /  %
&  |  ^  ~
<< >>
```

Comparisons include `==`, `!=`, `<`, `<=`, `>`, `>=`. Assignment forms include ordinary assignment and the implemented arithmetic/bitwise/shift compound operators such as `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, and `>>=` where type rules permit them.

Casts use `as`:

```uinx
val address = raw as usize
```

## 11. Concurrency declarations

```uinx
shared var counter: u64 = 0
percpu var ticks: u64 = 0

public unsafe concurrent func cpu_entry() -> unit:
    counter += 1
    ticks += 1
    return
```

`concurrent` identifies a function that may execute concurrently; the property propagates through calls. `shared` is explicit shared atomic-compatible storage. `percpu` is local-exec TLS storage. Automatic promotion and ordering rules are specified in `MEMORY_MODEL.md`.

## 12. Fences

```uinx
fence acquire
fence release
fence acq_rel
fence seq_cst

compiler_fence acquire
compiler_fence release
compiler_fence acq_rel
compiler_fence seq_cst
```

These lower through MIR/LLVM rather than architecture-specific source rewriting.

## 13. Safe indexing

Safe `Slice[T]`/`SliceMut[T]` indexing emits bounds checks before pointer arithmetic. Out-of-range execution enters an `llvm.trap` path in the verified implementation.

## 14. RAII and Drop

Non-`Copy` values with a `Drop` implementation receive MIR destruction at normal scope exit for the tested forms.

```uinx
extend Resource with Drop:
    func drop(value: mutref Resource) -> unit:
        return
```

Complete exception/unwind destruction and every nested generic specialization are not claimed as verified.

## 15. Unsafe

```uinx
unsafe:
    asm("nop", volatile, clobber("memory"))
```

An `unsafe func` body is also an unsafe context. Raw-pointer dereference/arithmetic and inline assembly require unsafe context. Safe code must cross this boundary explicitly.

## 16. C FFI

```uinx
extern "C" func abs(value: i32) -> i32
```

Scalar/reference/raw-pointer C-linkage paths are implemented. Full aggregate ABI equivalence across every ABI/architecture is not claimed.

## 17. Inline assembly

```uinx
unsafe:
    asm("mov $1, $0", out("=r", output), in("r", input), volatile, clobber("cc"))
```

Implemented operands include `in`, `out`, `inout`, `clobber`, and `volatile`. The tested validation/codegen paths cover x86-64, AArch64, and RISC-V64.

## 18. Async/await

```uinx
async func answer() -> i32:
    val value = await child()
    return value
```

Hosted async lowering has future-frame suspend/resume machinery. Allocator-free kernel async/executor integration is not part of the verified 0.3 bare-metal surface.

## 19. Primitive types

Implemented primitive spellings include:

```text
unit never bool
char str
i8 i16 i32 i64 i128
u8 u16 u32 u64 u128
isize usize
f32 f64
```

## 20. Compatibility syntax

Older Rust/C-shaped aliases such as `fn`, `pub`, `let`, brace/semicolon suites, `&T`, `&mut T`, `*const T`, `*mut T`, angle-bracket generic applications, and `impl Trait for Type` may remain accepted for migration. They are not canonical Uinx 0.3 and should not appear in new examples or standard-library code.

Tokens reserved by the lexer are not automatically part of the language. A construct is canonical only when parser, semantic analysis, lowering/codegen, tests, and this specification agree that it is implemented.
