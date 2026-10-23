#ifndef CASCADE_AST_H
#define CASCADE_AST_H

#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include "token.h"

// Static types. Bool exists only as the type of conditions/comparisons —
// the language has no bool variables, so a Bool value is never stored.
// Void only ever appears as a function's return type.
enum class Type { Unknown, Int, Float, Bool, Error, Void };
std::string typeName(Type t);

enum class NodeKind {
    // statements
    Program,    // kids = statements and function definitions, in source order
    Block,      // kids = statements
    Decl,       // scalar: text = name, declType, kids[0] = initializer (optional)
                // array (isArray): declType = element type, arraySize, kids = initializer elements
    Assign,     // text = name, kids[0] = value
    ArrAssign,  // text = array name, kids[0] = index, kids[1] = value
    If,         // kids = cond, then-block (or statement), [else-block / else-if]
    While,      // kids = cond, body, [step]  (step: runs after body and on 'continue';
                //   this is how the parser lowers `for`)
    FuncDef,    // text = name, declType = return type, kids = Param..., body Block (last)
    FuncProto,  // forward declaration: like FuncDef but kids = Param... only
    Param,      // text = name, declType, isArray
    Return,     // kids[0] = value (optional)
    Print,      // kids[0] = value
    Break,
    Continue,
    Call,       // text = function name, kids = arguments (statement or expression)
    // expressions
    IntLit,     // text = digits
    FloatLit,   // text = digits.digits
    BoolLit,    // text = "true" / "false"
    Var,        // text = name
    Index,      // text = array name, kids[0] = index expression
    Unary,      // op = MINUS / NOT, kids[0]
    Binary,     // op = operator token, kids[0], kids[1]
    IntToFloat, // implicit widening, inserted by the semantic analyzer
    BoolToInt,  // a comparison / logical result used as an int (0 or 1), inserted by the semantic analyzer
    // parser-internal: an accumulating list (statements, parameters,
    // arguments), flattened before the tree is handed to later stages
    StmtList
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
    NodeKind kind;
    std::string text;
    TokenType op = TokenType::END_OF_FILE;
    int line = 0;
    int column = 0;
    std::vector<NodePtr> kids;

    Type declType = Type::Unknown; // Decl / Param / FuncDef: declared (element / return) type
    bool isArray = false;          // Decl / Param: declared with []
    long long arraySize = 0;       // Decl arrays: element count (-1 = invalid literal)
    Type type = Type::Unknown;     // expressions: annotated by semantic analysis
    std::string resolved;          // Var/Decl/Assign: unique name after scope resolution

    Node(NodeKind k, int ln, int col) : kind(k), line(ln), column(col) {}
};

// Deep copy (used when the parser lowers `a[i] += e` to `a[i] = a[i] + e`,
// so the two uses of `a[i]` are separate nodes the semantic analyzer can
// annotate independently).
NodePtr cloneNode(const NodePtr& node);

// Indented tree dump. Shows annotated types / resolved names once
// semantic analysis has run, so the same printer serves both stages.
void printAst(const NodePtr& node, std::ostream& out, int depth = 0);

// Short operator spelling ("+", "<=", "&&") for a token type.
std::string opSpelling(TokenType t);

#endif // CASCADE_AST_H
