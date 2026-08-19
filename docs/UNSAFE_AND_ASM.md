# Unsafe, raw memory and inline assembly

Canonical Uinx makes unsafe boundaries visually explicit without requiring brace syntax.

## Unsafe context

```uinx
unsafe:
    dangerous_operation()
```

The body of an `unsafe func` is also an unsafe context. Calls to unsafe functions, raw-pointer dereference/arithmetic and `asm()` are rejected outside unsafe context where required by semantic checking.

## Raw pointer types

`ptr T` is a non-owning const/raw address and `mutptr T` is its mutable form. `deref value` performs dereference; raw-pointer dereference requires unsafe context. `ref T`/`mutref T` remain checked references and are created with `borrow` / `borrow mut`.

The migration aliases `*const T`, `*mut T`, `&T` and `&mut T` are accepted but are not canonical Uinx syntax.

## C FFI

```uinx
extern "C" func abs(value: i32) -> i32
```

C declarations keep C symbol names through object/link generation. The integration suite executes a mixed Uinx/C FFI path.

## Inline assembly

```uinx
func main() -> i32:
    var output = 0
    val input = 7
    unsafe:
        asm("mov $1, $0", out("=r", output), in("r", input), volatile, clobber("cc"))
    return output - 7
```

The compiler lowers assembly directly to LLVM inline asm; it is not a comment, intrinsic stub, or C transpilation escape hatch. Supported operand classes are `in`, `out`, `inout`, and `clobber`, plus `volatile`. Target-specific constraint validation covers the tested x86_64, AArch64 and RISC-V64 paths.
