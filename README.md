# Cascade — Stage 1: Lexical Analyzer

This covers Objective 2 from the proposal: a lexical analyzer that
tokenizes Cascade source programs and detects lexical errors with line
numbers. It's a hand-written longest-match scanner (no Flex yet — see
"Next steps").

## Initial prototype checklist

- [x] Grammar & language spec drafted — [`docs/language-spec.md`](docs/language-spec.md)
- [x] Token classes & DFA scanner design complete — [`docs/token-classes-dfa.md`](docs/token-classes-dfa.md)
- [x] Lexical analyzer tokenizing sample programs — this stage, below
- [ ] FIRST/FOLLOW sets and SLR table construction
- [ ] Shift-reduce parser and AST build-out
- [ ] Semantic analyzer, ICG, and VM

## Build & run

```
make            # builds ./cascade_lexer
./cascade_lexer tests/valid1.casc
make test       # runs it over every sample program in tests/
```

Tested on macOS (Apple Silicon) with the system `g++`/`clang++` via
Xcode Command Line Tools — no external dependencies. `make test` output
above shows all three sample programs scanning correctly, including the
three planted lexical errors in `invalid_lexical.casc`.

## Layout

```
include/token.h     token type enum + Token struct
include/lexer.h     Lexer class interface
src/token.cpp       tokenTypeName() for pretty-printing / diagnostics
src/lexer.cpp       the scanner itself
src/main.cpp        CLI driver: file in, token table + error list out
tests/*.casc        sample programs (2 valid, 1 deliberately broken)
```

## What the lexer covers

- **Keywords:** `int float if else while true false`
- **Identifiers:** letters/underscore then letters/digits/underscore
- **Literals:** integers (`42`) and floats (`3.14`)
- **Operators:** `+ - * / % = == != < <= > >= && || !`
- **Punctuation:** `( ) { } ; ,`
- **Line comments:** `// ...` (not in the formal grammar, added purely so
  test programs are easier to annotate — drop it if you want the lexer to
  match the grammar exactly)

## Error recovery

Three classes of lexical error are detected, each reported with its line
and column and then skipped over so scanning continues (see
`invalid_lexical.casc` for one of each):

1. An unrecognized character (`@`, a lone `&`, etc.)
2. A malformed numeric literal — a second decimal point (`3.14.15`)
3. A numeric literal directly followed by letters (`12abc`), which can't
   be split into two tokens under maximal munch

Every token — including `LEX_ERROR` tokens — is still pushed into the
returned stream rather than dropped, so later stages (or a test script)
can see exactly where the bad tokens fell relative to the good ones.

## Next steps (Objective 3 onward)

1. Write out the CFG for the language and compute FIRST/FOLLOW sets by
   hand.
2. Build the LR(0) item sets → SLR parsing table.
3. Drive a shift-reduce parser off that table, building the AST as you
   reduce (this is the point where `Token` from this stage becomes the
   parser's input alphabet).

Happy to help build the grammar and the SLR table construction next —
that's naturally the next thing this feeds into for Review 2.
