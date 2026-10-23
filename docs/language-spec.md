# Cascade — Language Specification & Grammar

This is the formal spec behind the lexer built in Stage 1, and the grammar
Stage 2 (the SLR parser) will build FIRST/FOLLOW sets and the LR(0)
automaton from. Written to match the scope locked in the proposal: no
functions, no arrays, no user-defined types.

> **Update for the final review:** functions, 1-D arrays, `print`, `for`,
> `break`/`continue`, `else if` and compound assignment were added. The
> sections below describe the original core language, which is unchanged;
> the additions are described in [`functions-and-arrays.md`](functions-and-arrays.md)
> and the **complete current grammar** is the appendix at the end of this file.

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

## Appendix: complete current grammar (generated by `cascade --tables`)

```
GRAMMAR (augmented, numbered)
    0)  S' -> program
    1)  program -> item_list
    2)  item_list -> item_list item
    3)  item_list -> item
    4)  item -> stmt
    5)  item -> func_def
    6)  item -> func_proto
    7)  func_def -> type id ( params ) block
    8)  func_def -> type id ( ) block
    9)  func_def -> void id ( params ) block
   10)  func_def -> void id ( ) block
   11)  func_def -> type id ( void ) block
   12)  func_def -> void id ( void ) block
   13)  func_proto -> type id ( void ) ;
   14)  func_proto -> void id ( void ) ;
   15)  func_proto -> type id ( params ) ;
   16)  func_proto -> type id ( ) ;
   17)  func_proto -> void id ( params ) ;
   18)  func_proto -> void id ( ) ;
   19)  params -> params , param
   20)  params -> param
   21)  param -> type id
   22)  param -> type id [ ]
   23)  stmt_list -> stmt_list stmt
   24)  stmt_list -> stmt
   25)  stmt -> decl_stmt
   26)  stmt -> assign_stmt
   27)  stmt -> call_stmt
   28)  stmt -> return_stmt
   29)  stmt -> print_stmt
   30)  stmt -> break_stmt
   31)  stmt -> continue_stmt
   32)  stmt -> for_stmt
   33)  stmt -> if_stmt
   34)  stmt -> while_stmt
   35)  stmt -> block
   36)  decl_stmt -> type id = expr ;
   37)  decl_stmt -> type id ;
   38)  decl_stmt -> type id [ int_lit ] ;
   39)  decl_stmt -> type id [ int_lit ] = { expr_list } ;
   40)  decl_stmt -> type id [ ] = { expr_list } ;
   41)  type -> int
   42)  type -> float
   43)  assign_stmt -> simple ;
   44)  simple -> lvalue = expr
   45)  simple -> lvalue += expr
   46)  simple -> lvalue -= expr
   47)  simple -> lvalue *= expr
   48)  simple -> lvalue /= expr
   49)  simple -> lvalue %= expr
   50)  simple -> lvalue ++
   51)  simple -> lvalue --
   52)  lvalue -> id
   53)  lvalue -> id [ expr ]
   54)  call_stmt -> id ( expr_list ) ;
   55)  call_stmt -> id ( ) ;
   56)  expr_list -> expr_list , expr
   57)  expr_list -> expr
   58)  return_stmt -> return expr ;
   59)  return_stmt -> return ;
   60)  print_stmt -> print ( expr ) ;
   61)  break_stmt -> break ;
   62)  continue_stmt -> continue ;
   63)  for_stmt -> for ( for_init ; expr ; simple ) stmt
   64)  for_init -> simple
   65)  for_init -> type id = expr
   66)  if_stmt -> if ( expr ) stmt
   67)  if_stmt -> if ( expr ) stmt else stmt
   68)  while_stmt -> while ( expr ) stmt
   69)  block -> { stmt_list }
   70)  block -> { }
   71)  expr -> expr || and_expr
   72)  expr -> and_expr
   73)  and_expr -> and_expr && not_expr
   74)  and_expr -> not_expr
   75)  not_expr -> ! not_expr
   76)  not_expr -> rel_expr
   77)  rel_expr -> arith_expr relop arith_expr
   78)  rel_expr -> arith_expr
   79)  relop -> <
   80)  relop -> <=
   81)  relop -> >
   82)  relop -> >=
   83)  relop -> ==
   84)  relop -> !=
   85)  arith_expr -> arith_expr + term
   86)  arith_expr -> arith_expr - term
   87)  arith_expr -> term
   88)  term -> term * factor
   89)  term -> term / factor
   90)  term -> term % factor
   91)  term -> factor
   92)  factor -> - factor
   93)  factor -> ( expr )
   94)  factor -> id
   95)  factor -> id [ expr ]
   96)  factor -> id ( expr_list )
   97)  factor -> id ( )
   98)  factor -> int_lit
   99)  factor -> float_lit
  100)  factor -> true
  101)  factor -> false
```
