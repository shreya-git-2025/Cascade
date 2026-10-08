# Cascade — Implementation Notes (Stages 2–5)

How each pipeline stage after the lexer works, what it produces, and
where to find it in the code. The lexer is described in
[token-classes-dfa.md](token-classes-dfa.md); the grammar in
[language-spec.md](language-spec.md).

```
source ─► Lexer ─► tokens ─► SLR parser ─► AST ─► Semantic ─► annotated AST
                                                                    │
            program state ◄─ Stack VM ◄─ bytecode ◄─ assembler ◄─ TAC (ICG)
```

Every stage can be inspected on its own with a `cascade` flag:

| Stage | Flag | Output |
|---|---|---|
| 1 Lexer | `--tokens` | token stream (type, lexeme, line/col) |
| 2 Parser | `--tables` | numbered grammar, FIRST/FOLLOW, LR(0) item sets, SLR table |
| 2 Parser | `--parse-trace` | every shift / reduce step with the state stack |
| 2/3 | `--ast` | AST, annotated with types and resolved names after Stage 3 |
| 3 Semantic | `--symbols` | symbol table (name, resolved name, type, scope depth) |
| 4 ICG | `--tac` | numbered three-address code |
| 5 VM | `--bytecode` / `--trace` | bytecode listing / step-by-step execution |

Sample outputs for the report are in [`generated/`](generated/)
(regenerate with `make docs`).

---

## Stage 2 — SLR parser

**Files:** `grammar.{h,cpp}`, `slr.{h,cpp}`, `parser.{h,cpp}`, `ast.{h,cpp}`

The grammar from the spec is stored as data (`grammar.cpp`): a list of
productions, each with the semantic action that builds its AST node. The
SLR table is then *computed* from that list rather than typed in by hand,
so the table can never drift out of sync with the grammar:

1. **FIRST / FOLLOW** — fixed-point iteration over the productions
   (`computeFirst`, `computeFollow`).
2. **LR(0) canonical collection** — `closure()` and `gotoSet()` exactly as
   in the Dragon Book; states are discovered breadth-first from
   `closure({S' -> . program})`. Result: **78 states**.
3. **ACTION / GOTO** — shift on terminal transitions, goto on nonterminal
   transitions, reduce `A -> α.` on every terminal in FOLLOW(A), accept
   on `S' -> program .` with lookahead `$`. Any cell written twice with
   different entries is recorded as a conflict. Result: **0 conflicts**,
   so the grammar is SLR(1).

The driver (`parser.cpp`) keeps a state stack and a parallel value stack.
On a reduce it pops |RHS| entries, runs the production's action on them
(syntax-directed translation), and pushes the result with
`GOTO[top, A]`. The AST is therefore built bottom-up during the parse, with
no second pass. Unit productions (`term -> factor` etc.) just pass their
child up, so the AST contains no chain nodes.

On an error cell the parser reports the offending token and the set of
terminals that *would* have been accepted in that state (taken straight
from the ACTION row), e.g.

```
line 5, col 11: syntax error: unexpected '<', expected '+', '-', '&&', '||', ')' or ';'
```

## Stage 3 — Semantic analysis

**Files:** `semantic.{h,cpp}`

A recursive walk over the AST with a **stack of scopes** (one hash map per
scope): a scope is pushed on entering a block and popped on leaving it.
Lookup searches from the innermost scope outwards.

| Check | Rule |
|---|---|
| Undeclared use / assignment | error |
| Redeclaration in the **same** scope | error (shows the first declaration's line) |
| Redeclaration in an **inner** scope | allowed (shadowing); the inner one is renamed `x.1`, `x.2`, … |
| `int x;` read before assignment | warning; value defaults to 0 |
| Integer literal out of 64-bit range | error |

**Type rules** (`bool` is the type of comparisons only — there are no bool variables):

| Expression | Operands | Result |
|---|---|---|
| `+ - * /` | int, float (mixed → both widened to float) | int or float |
| `%` | int, int only | int |
| unary `-` | int or float | same |
| `< <= > >=` | int, float (mixed → widened) | bool |
| `== !=` | two numerics, or two bools | bool |
| `&& \|\|` | bool, bool | bool |
| `!` | bool | bool |
| `if` / `while` condition | must be bool (`if (x)` is rejected — write `x != 0`) | |
| `int x = e` / `x = e` | `e` must be int | |
| `float x = e` / `x = e` | `e` int (widened) or float | |

Wherever an int is widened, the analyzer inserts an explicit `IntToFloat`
node into the AST, so later stages never have to reason about implicit
conversions. Errors don't stop the walk: an ill-typed node gets the type
`<error>`, and anything built on `<error>` is accepted silently — one
mistake gives one message, not a cascade (see
`tests/semantic/08_multiple_errors.casc`).

## Stage 4 — Three-address code with backpatching

**Files:** `tac.{h,cpp}`

TAC is a vector of quadruples; the instruction number is the vector
index. Instruction forms:

```
x = y            x = y op z  (op: + - * / %)     x = - y      x = (float) y
if y relop z goto L          goto L              halt
```

Control flow uses the textbook **backpatching** scheme. Jumps are emitted
with an empty target (`_`) and collected in lists; a list is filled in
(`backpatch`) only once the instruction it should point to has been
emitted. `M.instr` below means "the next instruction number at this point".

| Production | Translation |
|---|---|
| `B -> E1 relop E2` | truelist = {`if E1 relop E2 goto _`}, falselist = {`goto _`} |
| `B -> true` / `false` | truelist / falselist = {`goto _`} |
| `B -> ! B1` | swap B1's truelist and falselist |
| `B -> B1 \|\| M B2` | backpatch(B1.false, M); true = B1.true ∪ B2.true; false = B2.false |
| `B -> B1 && M B2` | backpatch(B1.true, M); true = B2.true; false = B1.false ∪ B2.false |
| `S -> if (B) M S1` | backpatch(B.true, M); next = B.false ∪ S1.next |
| `S -> if (B) M1 S1 N else M2 S2` | backpatch(B.true, M1); backpatch(B.false, M2); next = S1.next ∪ N ∪ S2.next |
| `S -> while M1 (B) M2 S1` | backpatch(S1.next, M1); backpatch(B.true, M2); emit `goto M1`; next = B.false |
| `L -> L1 M S` | backpatch(L1.next, M); next = S.next |
| `program -> L` | backpatch(L.next, M); emit `halt` |

Because `&&` and `||` are translated as jumps, they **short-circuit** for
free: in `z != 0 && 10 / z > 1` the division is never reached when
`z == 0` (`tests/valid/06_short_circuit.casc`). A comparison between two
*bool* operands (`(a < 5) == (b < 5)`) first materializes each side into
a 0/1 temporary.

Example (`tests/valid/01_if_else.casc`):

```
   0:  x = 5
   1:  y = 10
   2:  if x < y goto 4        ← B.truelist, patched to M1 = 4
   3:  goto 7                 ← B.falselist, patched to M2 = 7
   4:  t1 = x + 1
   5:  x = t1
   6:  goto 9                 ← N, patched with the if's nextlist
   7:  t2 = y - 1
   8:  y = t2
   9:  halt
```

## Stage 5 — Stack-based virtual machine

**Files:** `vm.{h,cpp}`

**Assembler.** Each quad becomes a short stack sequence; jump targets are
then remapped from quad numbers to bytecode addresses:

| TAC | Bytecode |
|---|---|
| `x = a` | `push a; STORE x` |
| `t = a op b` | `push a; push b; OP; STORE t` |
| `if a < b goto L` | `push a; push b; LT_x; JNZ L'` |
| `goto L` | `JMP L'` |

(`push` is `PUSH_I` / `PUSH_F` for a constant, `LOAD` for a variable or temporary.)

**Instruction set.** Every arithmetic and comparison op is typed (`_I` /
`_F`) because types are fully known after Stage 3 — the VM never has to
inspect a value to decide what to do.

```
PUSH_I k   PUSH_F k   LOAD s   STORE s
ADD_I SUB_I MUL_I DIV_I MOD_I NEG_I     ADD_F SUB_F MUL_F DIV_F NEG_F     I2F
LT_I LE_I GT_I GE_I EQ_I NE_I           LT_F LE_F GT_F GE_F EQ_F NE_F    (push 0/1)
JMP addr   JNZ addr (pop; jump if non-zero)   HALT
```

**Execution.** One operand stack, one slot array (user variables and
temporaries, typed from the TAC), a program counter and a `switch`
dispatch loop. Integer arithmetic wraps on overflow (two's complement);
integer `/` and `%` truncate toward zero, as in C.

**Runtime errors** (exit status 3), each reported with its source line:
integer division / modulo by zero, floating-point division by zero, and a
step limit (default 10 million, `--max-steps=N`) that stops runaway loops.

When the program halts, the final value of every user variable is printed
in declaration order — this is the program's observable output, since the
language has no I/O statements.

---

## Testing

`make test` runs `tests/run_tests.sh`. Each `tests/<category>/<name>.casc`
has a `.expected` file containing the exact output plus exit status. A
test passes only if the output matches **and** the program stopped at the
stage its folder names (lexical / syntax / semantic / runtime error, or a
clean run for `valid/`). The runner also checks the grammar is still
SLR(1).

| Category | Count | Covers |
|---|---|---|
| `valid/` | 12 | if/else, while, gcd, factorial, primes (nested loops), short-circuit, shadowing, precedence, int/float mixing, uninitialized vars, bool equality |
| `lexical/` | 4 | bad characters, lone `&`/`\|`, malformed numbers |
| `syntax/` | 9 | missing `;`/`(`/`}`, chained `<`, keyword as name, stray `else`, empty program, non-block body |
| `semantic/` | 8 | undeclared, redeclared, narrowing, non-bool condition, `%` on float, bool storage, out-of-scope use, multiple errors |
| `runtime/` | 4 | div/mod by zero, float div by zero, infinite loop |
