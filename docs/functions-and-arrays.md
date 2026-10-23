# Cascade — Functions and Arrays (final-review extension)

The proposal listed functions and arrays as out of scope. For the final
review they were added, which touched **every stage**. This document
explains what was added and how each stage handles it.

## 1. What the language gained

| Feature | Syntax | Notes |
|---|---|---|
| Functions | `int f(int a, float b) { ... return x; }` | `int`, `float` or `void` return type; recursion and mutual recursion work |
| Prototypes | `int isOdd(int n);` and `int main(void)` | optional: a call may precede the definition |
| Arrays | `int a[10];` `int a[3] = {1,2,3};` `int a[] = {1,2,3};` | 1-D; element type `int` or `float` |
| Indexing | `a[i]`, `a[i] = e;`, `a[i] += e;` | run-time bounds checked |
| Array parameters | `void sort(int a[], int n)` | passed by reference (callee sees the caller's array) |
| Output | `print(expr);` | one value per line |
| Loops | `for (init; cond; step) body`, `break;`, `continue;` | `for` is lowered to `while` by the parser |
| Conditionals | `else if`; bodies without braces | dangling `else` binds to the nearest `if`, as in C |
| Assignment forms | `+= -= *= /= %=`, `x++`, `x--` | lowered to plain assignment by the parser |
| C-style truthiness | `while (n)`, `if (f(x))`, `count += x > 0;` | non-zero is true; comparisons are 0/1 |

Execution model: top-level statements run in order, then `main()` (if
defined) is called.

## 2. Stage by stage

**Lexer.** New tokens `[ ] ++ -- += -= *= /= %=` and keywords `void return
for print break continue`; `/* ... */` comments. All use the same
longest-match approach as before (`+` then peek: `+`, `++` or `+=`).

**Grammar / SLR table.** 200 LR(0) states (was 78), **0 unresolved
conflicts**. Design points worth knowing for the review:

- `ID` at the start of a statement is followed by `=`, `[` or `(`, so
  assignment, indexed assignment and call statements never conflict.
- `type ID` followed by `(` is a function, by `;` / `=` / `[` a variable.
- Allowing braceless bodies (`if (x) y = 1;`) re-introduces the classic
  *dangling else* shift/reduce conflict. It is resolved **on purpose** as
  *shift* (else binds to the nearest if). The builder records it separately
  (`cascade --tables` prints "1 conflict(s) resolved on purpose"); any other
  conflict still stops the compiler.
- `for`, `x += e`, `x++` and `else if` need no new AST kinds: the parser
  rewrites them (`for` → a block holding the init and a `while` with a step;
  `x += e` → `x = x + e`).

**Semantic analysis.**
- A pre-pass collects every function signature, so calls work before the
  definition and recursion needs no special case.
- Each function has its own scope for parameters; variables remember which
  function owns them (needed by the later stages).
- Checks: undefined function, wrong number/type of arguments (an `int` is
  widened to `float` automatically), return-type mismatch, `return` outside a
  function, void function used as a value, `break`/`continue` outside a
  loop, array misuse (indexing a non-array, using an array as a scalar,
  non-int index, bad size, too many initializers), duplicate or conflicting
  declarations, `main` with parameters.
- Errors are reported in source order.

**Three-address code.** New instructions:

```
param x            push an argument
t = call f, n      call (result optional)
return [x]
t = a[i]           array read
a[i] = x           array write
a = alloc N        create an array
print x
```

Arguments are evaluated first (nested calls finish before the outer call's
`param`s are emitted), then pushed right before the `call`. `break` and
`continue` use backpatched jump lists, the same mechanism as `if`/`while`.
Function bodies follow the top-level code; each function falls through to an
implicit `return` at its end.

**Virtual machine.**
- *Frames:* a `CALL` creates a frame holding the callee's parameters,
  variables and temporaries (`LOADL`/`STOREL`), so every recursive call has its
  own copies. `RET` pops the frame and pushes the return value for the caller.
  Call depth is limited to 10,000 ("stack overflow" error instead of a crash).
- *Arrays:* all arrays live in one heap. An array is a length cell followed by
  its elements; a variable holds the cell's index. `ALOAD`/`ASTORE` check
  bounds and report `array index 5 is out of bounds (array has 3 elements)`
  with the source line. Passing an array to a function passes that index, which
  is why the callee modifies the caller's array.

New opcodes: `LOADL STOREL CALL RET POP ALLOC ALOAD ASTORE PRINT`.

## 3. Verification

- `make test` — 73 programs (valid, lexical, syntax, semantic, runtime).
- Differential testing: programs using recursion, sorting, a sieve, nested
  `break`/`continue`, integer division and modulo semantics were run through
  both Cascade and `gcc` and produced identical output.
- `cascade --tables | tail -4` shows 0 conflicts plus the one resolved
  dangling-else conflict.

## 4. Known limits (say so if asked)

No strings/`char`, `printf`/`scanf`, 2-D arrays, pointers, `switch`,
`do`/`while` or `?:`. Arrays are 1-D with a size known at declaration (an
integer literal). Arguments to a call are evaluated left to right.
