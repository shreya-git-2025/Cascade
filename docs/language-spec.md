# Cascade — Language Specification & Grammar

This is the formal spec behind the lexer built in Stage 1, and the grammar
Stage 2 (the SLR parser) will build FIRST/FOLLOW sets and the LR(0)
automaton from. Written to match the scope locked in the proposal: no
functions, no arrays, no user-defined types.

## 1. Lexical categories (token classes)

| Class            | Examples                          | Notes |
|-------------------|------------------------------------|-------|
| Keywords          | `int float if else while true false` | reserved, cannot be used as identifiers |
| Identifiers       | `x`, `total`, `_tmp1`               | letter/underscore, then letters/digits/underscores |
| Integer literals  | `0`, `42`, `1000`                   | one or more digits |
| Float literals    | `3.14`, `0.0`, `100.5`              | digits, `.`, digits — both sides required |
| Arithmetic ops    | `+ - * / %`                         | |
| Relational ops    | `< <= > >= == !=`                   | |
| Logical ops       | `&& \|\| !`                         | `&&`/`\|\|` short-circuit |
| Assignment        | `=`                                 | |
| Punctuation       | `( ) { } ; ,`                       | `,` reserved for future use; unused by the current grammar |

The full regex-level definition of each class, plus the scanner's DFA
design, is in [`token-classes-dfa.md`](./token-classes-dfa.md).

## 2. Data types

Two primitive types only:

- `int` — integer values
- `float` — floating-point values

Type checking (e.g. rejecting `int x = 3.5;`) is a semantic-analysis
concern, not a grammar concern — the grammar accepts any expression on
the right-hand side of a declaration; Stage 4 rejects the ill-typed ones.

## 3. Context-free grammar

Written to be unambiguous by construction — precedence and associativity
are baked into the grammar's stratification (expression → boolean-or →
boolean-and → not → relational → additive → multiplicative → unary →
factor) rather than left to operator-precedence declarations, since the
parser is a hand-built SLR table, not a generator with precedence
directives.

```
program      -> stmt_list

stmt_list    -> stmt_list stmt
              | stmt

stmt         -> decl_stmt
              | assign_stmt
              | if_stmt
              | while_stmt
              | block

decl_stmt    -> type ID = expr ;
              | type ID ;

type         -> INT
              | FLOAT

assign_stmt  -> ID = expr ;

if_stmt      -> IF ( expr ) block
              | IF ( expr ) block ELSE block

while_stmt   -> WHILE ( expr ) block

block        -> { stmt_list }
              | { }

expr         -> expr OROR and_expr
              | and_expr

and_expr     -> and_expr ANDAND not_expr
              | not_expr

not_expr     -> NOT not_expr
              | rel_expr

rel_expr     -> arith_expr relop arith_expr
              | arith_expr

relop        -> LT | LE | GT | GE | EQ | NE

arith_expr   -> arith_expr PLUS term
              | arith_expr MINUS term
              | term

term         -> term STAR factor
              | term SLASH factor
              | term PERCENT factor
              | factor

factor       -> MINUS factor
              | ( expr )
              | ID
              | INT_LITERAL
              | FLOAT_LITERAL
              | TRUE
              | FALSE
```

### Precedence, low to high

1. `||`
2. `&&`
3. `!` (unary)
4. `< <= > >= == !=` (non-associative — chaining like `a < b < c` is
   rejected by this grammar, since `rel_expr` only ever compares two
   `arith_expr`s, never another `rel_expr`)
5. `+ -` (left-associative)
6. `* / %` (left-associative)
7. unary `-` (right-associative by construction — `factor -> MINUS factor`
   recurses on itself)

### Design decisions (resolved at Review 2)

- **Uninitialized variables** — a bare declaration (`int x;`) is
  zero-initialized (`0` / `0.0`). The semantic analyzer emits a *warning*
  (not an error) the first time such a variable is read before any
  assignment appears in program text. See `tests/valid/10_uninitialized.casc`.
- **`,`** is still tokenized but unused by the grammar. Comma-separated
  declarations remain a candidate for the final phase; if they aren't
  added, `COMMA` is dropped from the token table before Review 3.
- **Non-chaining relational operators** — `a < b < c` is a syntax error by
  design (`rel_expr` compares exactly two `arith_expr`s), which keeps the
  SLR table conflict-free. See `tests/syntax/03_chained_relational.casc`.
- **No dangling-else ambiguity** — `if`/`while` bodies must be blocks, so
  `ELSE` is never in FOLLOW(`if_stmt`) and the SLR table has no
  shift/reduce conflict there. `./cascade --tables` confirms the grammar
  is SLR(1): 78 LR(0) states, 0 conflicts.
- **Typing rules** (enforced in Stage 3) are listed in
  [implementation.md](implementation.md#stage-3--semantic-analysis).
