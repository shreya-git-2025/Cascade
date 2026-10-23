#ifndef CASCADE_AST_H
#define CASCADE_AST_H

#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include "token.h"

// Static types. Bool exists only as the type of conditions/comparisons —
// the language has no bool variables, so a Bool value is never stored.
enum class Type { Unknown, Int, Float, Bool, Error, Void };
std::string typeName(Type t);

enum class NodeKind {
    // statements
    Program,    // kids = statements
    Block,      // kids = statements
    Decl,       // text = name, declType, kids[0] = initializer (optional)
    Assign,     // text = name, kids[0] = value
    If,         // kids = cond, then-block, [else-block]
    While,      // kids = cond, body
    // expressions
    IntLit,     // text = digits
    FloatLit,   // text = digits.digits
    BoolLit,    // text = "true" / "false"
    Var,        // text = name
    Unary,      // op = MINUS / NOT, kids[0]
    Binary,     // op = operator token, kids[0], kids[1]
    IntToFloat, // implicit widening, inserted by the semantic analyzer
    // parser-internal: an accumulating stmt_list, flattened before the
    // tree is handed to later stages
    FuncDecl,    // text=name, declType=return type, kids = Param..., Block (last)
    Param,       // text=name, declType, isArray
    Call,        // text=name, kids = args
    Index,       // text=array name, kids[0] = index
    ArrayAssign, // text=array name, kids = {index, value}
    Return,      // kids[0] optional
    Print,       // kids[0]

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

    Type declType = Type::Unknown; // Decl only: the declared type
    Type type = Type::Unknown;     // expressions: annotated by semantic analysis
    std::string resolved;          // Var/Decl/Assign: unique name after scope resolution
    bool isArray = false;
    long long arraySize = 0;
    Node(NodeKind k, int ln, int col) : kind(k), line(ln), column(col) {}
};

// Indented tree dump. Shows annotated types / resolved names once
// semantic analysis has run, so the same printer serves both stages.
void printAst(const NodePtr& node, std::ostream& out, int depth = 0);

// Short operator spelling ("+", "<=", "&&") for a token type.
std::string opSpelling(TokenType t);

#endif // CASCADE_AST_H
