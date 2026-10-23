# Cascade

A compiler and stack-based virtual machine for a small C-like imperative
language (int/float variables, **functions with recursion**, **1-D arrays**,
arithmetic, relational and short-circuit boolean operators, `if`/`else`,
`while`/`for`, `print`). Every stage is hand-written in C++17 with no
external dependencies.

```
source ─► Lexer ─► SLR parser ─► Semantic analyzer ─► TAC generator ─► Stack VM ─► output
          (DFA)    (AST)         (symbol table,       (backpatching)    (bytecode)
                                  type checks)
```

## Status (Final review)

- [x] Grammar & language spec — [`docs/language-spec.md`](docs/language-spec.md)
- [x] Token classes & DFA scanner design — [`docs/token-classes-dfa.md`](docs/token-classes-dfa.md)
- [x] **Stage 1** Lexical analyzer
- [x] **Stage 2** FIRST/FOLLOW, LR(0) automaton and SLR table (200 states, 0 unresolved conflicts); shift-reduce parser building the AST
- [x] **Stage 3** Semantic analyzer — scoped symbol table, type checking, implicit int→float widening
- [x] **Stage 4** Three-address code with truelist/falselist/nextlist backpatching
- [x] **Stage 5** Stack VM — assembler, dispatch loop, execution trace, runtime errors
- [x] **Functions and arrays** (final-review requirement) — see [`docs/functions-and-arrays.md`](docs/functions-and-arrays.md)
- [x] Test suite — 73 programs across all five error stages + runner; plus differential testing against `gcc`
- [ ] Final phase: parser error recovery (report several syntax errors per run), more end-to-end tests, Flex/Bison cross-check, optional LLVM IR lowering

How each stage works: [`docs/implementation.md`](docs/implementation.md).

## Build & run

```
make                                   # builds ./cascade
./cascade tests/valid/03_gcd.casc      # compile + run, prints final variable values
make test                              # full regression suite
```

```
$ ./cascade tests/valid/03_gcd.casc

=== PROGRAM STATE (after 55 VM steps) ===
  a         int    = 21
  b         int    = 0
  t         int    = 21
```

### Inspecting each stage

```
./cascade --tokens      prog.casc   # stage 1: token stream
./cascade --tables                  # stage 2: grammar, FIRST/FOLLOW, LR(0) sets, SLR table
./cascade --parse-trace prog.casc   # stage 2: every shift/reduce step
./cascade --ast         prog.casc   # stage 2/3: (annotated) syntax tree
./cascade --symbols     prog.casc   # stage 3: symbol table
./cascade --tac         prog.casc   # stage 4: three-address code
./cascade --bytecode    prog.casc   # stage 5: VM bytecode
./cascade --trace       prog.casc   # stage 5: step-by-step execution with the stack
./cascade --all         prog.casc   # tokens + ast + symbols + tac + bytecode
./cascade --stop-after=parse prog.casc   # lex | parse | sema | tac
```

Exit status: `0` success · `1` compile error (lexical, syntax or semantic) · `2` usage / file error · `3` runtime error.

`make docs` regenerates the sample outputs in [`docs/generated/`](docs/generated/)
(SLR tables, a full pipeline run of `01_if_else.casc`, and the TAC/bytecode
for the short-circuit example).

## Example

```c
// count primes <= 50
int count = 0;
int k = 2;
while (k <= 50) {
    int d = 2;
    int prime = 1;
    while (d * d <= k && prime == 1) {
        if (k % d == 0) { prime = 0; }
        d = d + 1;
    }
    if (prime == 1) { count = count + 1; }
    k = k + 1;
}
```

→ `count int = 15`

## Layout

```
include/ & src/
  token, lexer        Stage 1  hand-written longest-match scanner
  grammar             Stage 2  the CFG as data + one AST action per production
  slr                 Stage 2  FIRST/FOLLOW, LR(0) collection, ACTION/GOTO tables
  parser, ast         Stage 2  shift-reduce driver, AST nodes + printer
  semantic            Stage 3  scope-stack symbol table, type checker
  tac                 Stage 4  quadruples + backpatching code generator
  vm                  Stage 5  TAC→bytecode assembler, stack VM
  main.cpp                     pipeline driver and CLI flags
tests/
  valid/ lexical/ syntax/ semantic/ runtime/   *.casc + *.expected
  run_tests.sh                                 regression runner (--update to re-bless)
docs/
  language-spec.md  token-classes-dfa.md  implementation.md  generated/
```

## Language notes

Full description of the final-review features (functions, arrays, `print`,
`for`, `break`/`continue`, `else if`, compound assignment): [`docs/functions-and-arrays.md`](docs/functions-and-arrays.md).

```c
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
void bubbleSort(int a[], int n) { /* ... */ }

int main() {
    int data[5] = {5, 3, 9, 1, 4};
    bubbleSort(data, 5);
    for (int i = 0; i < 5; i++) print(data[i]);
    print(fib(10));
    return 0;
}
```

- `print(expr);` writes one value per line. When the program ends, the final
  value of every **top-level** variable (arrays included) is also printed.
- Top-level statements run in order; if a `main()` function exists it runs
  after them. Functions may be called before they are defined.
- `//` and `/* ... */` comments are accepted.
- Bare declarations (`int x;`) are zero-initialized; reading one before
  assigning it gives a warning.
- C-style truthiness: a number is a valid condition (`while (n)`), and a
  comparison can be used as 0/1 (`count += x > 0;`).
- `a < b < c` is rejected by design (keeps the grammar SLR(1)).
- Not supported: strings/`char`, `printf`/`scanf`, 2-D arrays, pointers,
  `switch`, `do`/`while`, the `?:` operator.
