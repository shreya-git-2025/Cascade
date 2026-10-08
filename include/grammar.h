#ifndef CASCADE_GRAMMAR_H
#define CASCADE_GRAMMAR_H

#include <functional>
#include <string>
#include <vector>
#include "ast.h"
#include "token.h"

// The Cascade CFG from docs/language-spec.md, encoded as data so the SLR
// construction (slr.cpp) can compute FIRST/FOLLOW, the LR(0) automaton and
// the parsing table directly from it instead of from a hand-typed table.
//
// Symbols are plain ints:
//   0 .. NUM_TERMINALS-1   terminals, numbered exactly like TokenType
//                          (END_OF_FILE doubles as the end marker '$')
//   NUM_TERMINALS ..       nonterminals, in NonTerminal order
enum class NonTerminal {
    ProgramPrime, // augmented start symbol S'
    Program,
    StmtList,
    Stmt,
    DeclStmt,
    Type,
    AssignStmt,
    IfStmt,
    WhileStmt,
    Block,
    Expr,
    AndExpr,
    NotExpr,
    RelExpr,
    RelOp,
    ArithExpr,
    Term,
    Factor,
    Count
};

using Symbol = int;

// LEX_ERROR is the last TokenType and is never handed to the parser, so
// every enum value before it (including END_OF_FILE) is a grammar terminal.
constexpr int NUM_TERMINALS = static_cast<int>(TokenType::LEX_ERROR);
constexpr int NUM_NONTERMINALS = static_cast<int>(NonTerminal::Count);
constexpr int NUM_SYMBOLS = NUM_TERMINALS + NUM_NONTERMINALS;
constexpr Symbol END_MARKER = static_cast<int>(TokenType::END_OF_FILE);

inline Symbol term(TokenType t) { return static_cast<int>(t); }
inline Symbol nonterm(NonTerminal n) { return NUM_TERMINALS + static_cast<int>(n); }
inline bool isTerminal(Symbol s) { return s < NUM_TERMINALS; }

// One entry on the parser's value stack: terminals carry their token,
// nonterminals carry the AST fragment their reduction built.
struct SemVal {
    Token tok{TokenType::END_OF_FILE, "", 0, 0};
    NodePtr node;
};

// Semantic action run on reduction; receives the values of the RHS
// symbols (left to right) and returns the value for the LHS.
using Action = std::function<SemVal(std::vector<SemVal>&)>;

struct Production {
    Symbol lhs;
    std::vector<Symbol> rhs;
    Action action;
};

const std::vector<Production>& grammar();

// Display names: grammar-style for nonterminals ("arith_expr"), source
// spelling for terminals ("';'", "identifier") — used in table dumps and
// in "expected one of ..." syntax error messages.
std::string symbolName(Symbol s);
std::string productionText(int prodIndex);

#endif // CASCADE_GRAMMAR_H
