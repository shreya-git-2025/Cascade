#include "../include/grammar.h"
#include <string>

namespace {
using NT = NonTerminal;
using TT = TokenType;

Symbol N(NT n) { return nonterm(n); }
Symbol T(TT t) { return term(t); }

NodePtr leaf(NodeKind kind, const Token& tok) {
    auto n = std::make_shared<Node>(kind, tok.line, tok.column);
    n->text = tok.lexeme;
    return n;
}

SemVal wrap(NodePtr n) {
    SemVal v;
    v.node = std::move(n);
    return v;
}

// --- reusable semantic actions -------------------------------------------

// A -> B   (unit production: pass the child's value through unchanged)
SemVal passFirst(std::vector<SemVal>& rhs) { return rhs[0]; }

// A -> A op B   (binary operator; position the node at the operator)
SemVal binary(std::vector<SemVal>& rhs) {
    const Token& op = rhs[1].tok;
    auto n = std::make_shared<Node>(NodeKind::Binary, op.line, op.column);
    n->op = op.type;
    n->kids = {rhs[0].node, rhs[2].node};
    return wrap(n);
}

// A -> op A   (prefix unary operator)
SemVal unary(std::vector<SemVal>& rhs) {
    const Token& op = rhs[0].tok;
    auto n = std::make_shared<Node>(NodeKind::Unary, op.line, op.column);
    n->op = op.type;
    n->kids = {rhs[1].node};
    return wrap(n);
}

Action literal(NodeKind kind) {
    return [kind](std::vector<SemVal>& rhs) { return wrap(leaf(kind, rhs[0].tok)); };
}

Type typeFromToken(const Token& t) {
    if (t.type == TT::KW_FLOAT) return Type::Float;
    if (t.type == TT::KW_VOID) return Type::Void;
    return Type::Int;
}

NodePtr makeNode(NodeKind kind, const Token& at) {
    return std::make_shared<Node>(kind, at.line, at.column);
}

// Lists (statements, parameters, arguments) accumulate in a StmtList node.
// startList:  L -> X          appendLast:  L -> L [,] X
SemVal startList(std::vector<SemVal>& rhs) {
    auto n = std::make_shared<Node>(NodeKind::StmtList, rhs[0].node->line, rhs[0].node->column);
    n->kids.push_back(rhs[0].node);
    return wrap(n);
}
SemVal appendLast(std::vector<SemVal>& rhs) {
    rhs[0].node->kids.push_back(rhs.back().node);
    return rhs[0];
}

NodePtr declNode(const SemVal& type, const SemVal& id) {
    auto n = leaf(NodeKind::Decl, id.tok);
    n->line = type.tok.line;
    n->column = type.tok.column;
    n->declType = typeFromToken(type.tok);
    return n;
}

long long parseSize(const Token& t) {
    try { return std::stoll(t.lexeme); } catch (...) { return -1; }
}

// lvalue = value   becomes Assign (plain variable) or ArrAssign (a[i]).
NodePtr makeAssign(const NodePtr& lv, NodePtr value) {
    if (lv->kind == NodeKind::Var) {
        auto n = std::make_shared<Node>(NodeKind::Assign, lv->line, lv->column);
        n->text = lv->text;
        n->kids = {std::move(value)};
        return n;
    }
    auto n = std::make_shared<Node>(NodeKind::ArrAssign, lv->line, lv->column);
    n->text = lv->text;
    n->kids = {lv->kids[0], std::move(value)};
    return n;
}

NodePtr binaryNode(const Token& at, TT op, NodePtr l, NodePtr r) {
    auto n = makeNode(NodeKind::Binary, at);
    n->op = op;
    n->kids = {std::move(l), std::move(r)};
    return n;
}

// Lvalue op= Expr   is lowered to   Lvalue = Lvalue op Expr
Action compoundAssign(TT op) {
    return [op](std::vector<SemVal>& rhs) {
        NodePtr value = binaryNode(rhs[1].tok, op, cloneNode(rhs[0].node), rhs[2].node);
        return wrap(makeAssign(rhs[0].node, value));
    };
}

// Lvalue ++ / --   is lowered to   Lvalue = Lvalue +/- 1
Action incDec(TT op) {
    return [op](std::vector<SemVal>& rhs) {
        auto one = makeNode(NodeKind::IntLit, rhs[1].tok);
        one->text = "1";
        NodePtr value = binaryNode(rhs[1].tok, op, cloneNode(rhs[0].node), one);
        return wrap(makeAssign(rhs[0].node, value));
    };
}

NodePtr callNode(const Token& id, const NodePtr& args) {
    auto n = leaf(NodeKind::Call, id);
    if (args) n->kids = args->kids;
    return n;
}

// FuncDef: rhs = [ret-type, id, '(', [params,] ')', body]
Action funcDef(bool hasParams) {
    return [hasParams](std::vector<SemVal>& rhs) {
        auto n = leaf(NodeKind::FuncDef, rhs[1].tok);
        n->declType = typeFromToken(rhs[0].tok);
        if (hasParams) n->kids = rhs[3].node->kids;
        n->kids.push_back(rhs[hasParams ? 5 : 4].node);
        return wrap(n);
    };
}

// FuncDef with a C-style `(void)` parameter list: rhs = [ret-type, id, '(', 'void', ')', body]
SemVal funcDefVoidParams(std::vector<SemVal>& rhs) {
    auto n = leaf(NodeKind::FuncDef, rhs[1].tok);
    n->declType = typeFromToken(rhs[0].tok);
    n->kids.push_back(rhs[5].node);
    return wrap(n);
}

// FuncProto: rhs = [ret-type, id, '(', [params,] ')', ';']
Action funcProto(bool hasParams) {
    return [hasParams](std::vector<SemVal>& rhs) {
        auto n = leaf(NodeKind::FuncProto, rhs[1].tok);
        n->declType = typeFromToken(rhs[0].tok);
        if (hasParams) n->kids = rhs[3].node->kids;
        return wrap(n);
    };
}

Action paramAction(bool isArray) {
    return [isArray](std::vector<SemVal>& rhs) {
        auto n = leaf(NodeKind::Param, rhs[1].tok);
        n->line = rhs[0].tok.line;
        n->column = rhs[0].tok.column;
        n->declType = typeFromToken(rhs[0].tok);
        n->isArray = isArray;
        return wrap(n);
    };
}

std::vector<Production> buildGrammar() {
    std::vector<Production> g;
    auto P = [&g](NT lhs, std::vector<Symbol> rhs, Action act) {
        g.push_back(Production{N(lhs), std::move(rhs), std::move(act)});
    };

    // 0: S' -> program   (augmented start; reducing it means "accept")
    P(NT::ProgramPrime, {N(NT::Program)}, passFirst);

    // program -> item_list ;  item_list -> item_list item | item
    // item -> stmt | func_def | func_proto
    //   (statements may appear at top level and run in order; functions can
    //    be defined anywhere)
    P(NT::Program, {N(NT::ItemList)}, [](std::vector<SemVal>& rhs) {
        auto n = std::make_shared<Node>(NodeKind::Program, 1, 1);
        n->kids = rhs[0].node->kids;
        return wrap(n);
    });
    P(NT::ItemList, {N(NT::ItemList), N(NT::Item)}, appendLast);
    P(NT::ItemList, {N(NT::Item)}, startList);
    P(NT::Item, {N(NT::Stmt)}, passFirst);
    P(NT::Item, {N(NT::FuncDef)}, passFirst);
    P(NT::Item, {N(NT::FuncProto)}, passFirst);

    // func_def -> (type | void) ID ( [params] ) block
    P(NT::FuncDef, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LPAREN), N(NT::Params), T(TT::RPAREN), N(NT::Block)}, funcDef(true));
    P(NT::FuncDef, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::RPAREN), N(NT::Block)}, funcDef(false));
    P(NT::FuncDef, {T(TT::KW_VOID), T(TT::IDENTIFIER), T(TT::LPAREN), N(NT::Params), T(TT::RPAREN), N(NT::Block)}, funcDef(true));
    P(NT::FuncDef, {T(TT::KW_VOID), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::RPAREN), N(NT::Block)}, funcDef(false));

    // C-style empty parameter list:  int main(void) { ... }
    P(NT::FuncDef, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::KW_VOID), T(TT::RPAREN), N(NT::Block)}, funcDefVoidParams);
    P(NT::FuncDef, {T(TT::KW_VOID), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::KW_VOID), T(TT::RPAREN), N(NT::Block)}, funcDefVoidParams);
    P(NT::FuncProto, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::KW_VOID), T(TT::RPAREN), T(TT::SEMICOLON)}, funcProto(false));
    P(NT::FuncProto, {T(TT::KW_VOID), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::KW_VOID), T(TT::RPAREN), T(TT::SEMICOLON)}, funcProto(false));

    // func_proto -> (type | void) ID ( [params] ) ;
    P(NT::FuncProto, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LPAREN), N(NT::Params), T(TT::RPAREN), T(TT::SEMICOLON)}, funcProto(true));
    P(NT::FuncProto, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::RPAREN), T(TT::SEMICOLON)}, funcProto(false));
    P(NT::FuncProto, {T(TT::KW_VOID), T(TT::IDENTIFIER), T(TT::LPAREN), N(NT::Params), T(TT::RPAREN), T(TT::SEMICOLON)}, funcProto(true));
    P(NT::FuncProto, {T(TT::KW_VOID), T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::RPAREN), T(TT::SEMICOLON)}, funcProto(false));

    // params -> params , param | param ;  param -> type ID | type ID [ ]
    P(NT::Params, {N(NT::Params), T(TT::COMMA), N(NT::Param)}, appendLast);
    P(NT::Params, {N(NT::Param)}, startList);
    P(NT::Param, {N(NT::Type), T(TT::IDENTIFIER)}, paramAction(false));
    P(NT::Param, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LBRACKET), T(TT::RBRACKET)}, paramAction(true));

    // stmt_list -> stmt_list stmt | stmt
    P(NT::StmtList, {N(NT::StmtList), N(NT::Stmt)}, appendLast);
    P(NT::StmtList, {N(NT::Stmt)}, startList);

    // stmt -> decl | assign | call | return | print | break | continue | for | if | while | block
    P(NT::Stmt, {N(NT::DeclStmt)}, passFirst);
    P(NT::Stmt, {N(NT::AssignStmt)}, passFirst);
    P(NT::Stmt, {N(NT::CallStmt)}, passFirst);
    P(NT::Stmt, {N(NT::ReturnStmt)}, passFirst);
    P(NT::Stmt, {N(NT::PrintStmt)}, passFirst);
    P(NT::Stmt, {N(NT::BreakStmt)}, passFirst);
    P(NT::Stmt, {N(NT::ContinueStmt)}, passFirst);
    P(NT::Stmt, {N(NT::ForStmt)}, passFirst);
    P(NT::Stmt, {N(NT::IfStmt)}, passFirst);
    P(NT::Stmt, {N(NT::WhileStmt)}, passFirst);
    P(NT::Stmt, {N(NT::Block)}, passFirst);

    // decl_stmt -> type ID = expr ; | type ID ;
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::ASSIGN), N(NT::Expr), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = declNode(rhs[0], rhs[1]);
          n->kids = {rhs[3].node};
          return wrap(n);
      });
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return wrap(declNode(rhs[0], rhs[1])); });

    // decl_stmt -> type ID [ INT ] ;
    //            | type ID [ INT ] = { expr_list } ;
    //            | type ID [ ] = { expr_list } ;      (size = number of initializers)
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LBRACKET), T(TT::INT_LITERAL),
                     T(TT::RBRACKET), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = declNode(rhs[0], rhs[1]);
          n->isArray = true;
          n->arraySize = parseSize(rhs[3].tok);
          return wrap(n);
      });
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LBRACKET), T(TT::INT_LITERAL),
                     T(TT::RBRACKET), T(TT::ASSIGN), T(TT::LBRACE), N(NT::ExprList),
                     T(TT::RBRACE), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = declNode(rhs[0], rhs[1]);
          n->isArray = true;
          n->arraySize = parseSize(rhs[3].tok);
          n->kids = rhs[7].node->kids;
          return wrap(n);
      });
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::LBRACKET), T(TT::RBRACKET),
                     T(TT::ASSIGN), T(TT::LBRACE), N(NT::ExprList), T(TT::RBRACE),
                     T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = declNode(rhs[0], rhs[1]);
          n->isArray = true;
          n->kids = rhs[6].node->kids;
          n->arraySize = static_cast<long long>(n->kids.size());
          return wrap(n);
      });

    // type -> INT | FLOAT   (value = the keyword token itself)
    P(NT::Type, {T(TT::KW_INT)}, passFirst);
    P(NT::Type, {T(TT::KW_FLOAT)}, passFirst);

    // assign_stmt -> simple ;
    P(NT::AssignStmt, {N(NT::Simple), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return rhs[0]; });

    // simple -> lvalue = expr | lvalue op= expr | lvalue ++ | lvalue --
    P(NT::Simple, {N(NT::Lvalue), T(TT::ASSIGN), N(NT::Expr)},
      [](std::vector<SemVal>& rhs) { return wrap(makeAssign(rhs[0].node, rhs[2].node)); });
    P(NT::Simple, {N(NT::Lvalue), T(TT::PLUS_ASSIGN), N(NT::Expr)}, compoundAssign(TT::PLUS));
    P(NT::Simple, {N(NT::Lvalue), T(TT::MINUS_ASSIGN), N(NT::Expr)}, compoundAssign(TT::MINUS));
    P(NT::Simple, {N(NT::Lvalue), T(TT::STAR_ASSIGN), N(NT::Expr)}, compoundAssign(TT::STAR));
    P(NT::Simple, {N(NT::Lvalue), T(TT::SLASH_ASSIGN), N(NT::Expr)}, compoundAssign(TT::SLASH));
    P(NT::Simple, {N(NT::Lvalue), T(TT::PERCENT_ASSIGN), N(NT::Expr)}, compoundAssign(TT::PERCENT));
    P(NT::Simple, {N(NT::Lvalue), T(TT::PLUS_PLUS)}, incDec(TT::PLUS));
    P(NT::Simple, {N(NT::Lvalue), T(TT::MINUS_MINUS)}, incDec(TT::MINUS));

    // lvalue -> ID | ID [ expr ]
    P(NT::Lvalue, {T(TT::IDENTIFIER)}, literal(NodeKind::Var));
    P(NT::Lvalue, {T(TT::IDENTIFIER), T(TT::LBRACKET), N(NT::Expr), T(TT::RBRACKET)},
      [](std::vector<SemVal>& rhs) {
          auto n = leaf(NodeKind::Index, rhs[0].tok);
          n->kids = {rhs[2].node};
          return wrap(n);
      });

    // call_stmt -> ID ( [expr_list] ) ;
    P(NT::CallStmt, {T(TT::IDENTIFIER), T(TT::LPAREN), N(NT::ExprList), T(TT::RPAREN), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return wrap(callNode(rhs[0].tok, rhs[2].node)); });
    P(NT::CallStmt, {T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::RPAREN), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return wrap(callNode(rhs[0].tok, nullptr)); });

    // expr_list -> expr_list , expr | expr
    P(NT::ExprList, {N(NT::ExprList), T(TT::COMMA), N(NT::Expr)}, appendLast);
    P(NT::ExprList, {N(NT::Expr)}, startList);

    // return_stmt -> return expr ; | return ;
    P(NT::ReturnStmt, {T(TT::KW_RETURN), N(NT::Expr), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = makeNode(NodeKind::Return, rhs[0].tok);
          n->kids = {rhs[1].node};
          return wrap(n);
      });
    P(NT::ReturnStmt, {T(TT::KW_RETURN), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return wrap(makeNode(NodeKind::Return, rhs[0].tok)); });

    // print_stmt -> print ( expr ) ;
    P(NT::PrintStmt, {T(TT::KW_PRINT), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = makeNode(NodeKind::Print, rhs[0].tok);
          n->kids = {rhs[2].node};
          return wrap(n);
      });

    P(NT::BreakStmt, {T(TT::KW_BREAK), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return wrap(makeNode(NodeKind::Break, rhs[0].tok)); });
    P(NT::ContinueStmt, {T(TT::KW_CONTINUE), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) { return wrap(makeNode(NodeKind::Continue, rhs[0].tok)); });

    // for_stmt -> for ( for_init ; expr ; simple ) stmt
    // Lowered right here to   { init; while (cond) body step }   — the While
    // node's optional third child is the step, so `continue` can jump to it.
    P(NT::ForStmt, {T(TT::KW_FOR), T(TT::LPAREN), N(NT::ForInit), T(TT::SEMICOLON), N(NT::Expr),
                    T(TT::SEMICOLON), N(NT::Simple), T(TT::RPAREN), N(NT::Stmt)},
      [](std::vector<SemVal>& rhs) {
          auto loop = makeNode(NodeKind::While, rhs[0].tok);
          loop->kids = {rhs[4].node, rhs[8].node, rhs[6].node};
          auto outer = makeNode(NodeKind::Block, rhs[0].tok); // scopes the init variable
          outer->kids = {rhs[2].node, loop};
          return wrap(outer);
      });
    // for_init -> simple | type ID = expr
    P(NT::ForInit, {N(NT::Simple)}, passFirst);
    P(NT::ForInit, {N(NT::Type), T(TT::IDENTIFIER), T(TT::ASSIGN), N(NT::Expr)},
      [](std::vector<SemVal>& rhs) {
          auto n = declNode(rhs[0], rhs[1]);
          n->kids = {rhs[3].node};
          return wrap(n);
      });

    // if_stmt -> IF ( expr ) stmt | IF ( expr ) stmt ELSE stmt
    // The body is any statement (a block, or a single statement without braces).
    // `else if` needs no rule of its own: the else-branch is just a stmt that
    // happens to be an if_stmt. This makes the grammar ambiguous on `else`
    // (dangling else); the SLR builder resolves it as shift (see slr.cpp).
    P(NT::IfStmt, {T(TT::KW_IF), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), N(NT::Stmt)},
      [](std::vector<SemVal>& rhs) {
          auto n = std::make_shared<Node>(NodeKind::If, rhs[0].tok.line, rhs[0].tok.column);
          n->kids = {rhs[2].node, rhs[4].node};
          return wrap(n);
      });
    P(NT::IfStmt, {T(TT::KW_IF), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), N(NT::Stmt),
                   T(TT::KW_ELSE), N(NT::Stmt)},
      [](std::vector<SemVal>& rhs) {
          auto n = std::make_shared<Node>(NodeKind::If, rhs[0].tok.line, rhs[0].tok.column);
          n->kids = {rhs[2].node, rhs[4].node, rhs[6].node};
          return wrap(n);
      });
    // while_stmt -> WHILE ( expr ) stmt
    P(NT::WhileStmt, {T(TT::KW_WHILE), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), N(NT::Stmt)},
      [](std::vector<SemVal>& rhs) {
          auto n = std::make_shared<Node>(NodeKind::While, rhs[0].tok.line, rhs[0].tok.column);
          n->kids = {rhs[2].node, rhs[4].node};
          return wrap(n);
      });

    // block -> { stmt_list } | { }
    P(NT::Block, {T(TT::LBRACE), N(NT::StmtList), T(TT::RBRACE)}, [](std::vector<SemVal>& rhs) {
        auto n = std::make_shared<Node>(NodeKind::Block, rhs[0].tok.line, rhs[0].tok.column);
        n->kids = rhs[1].node->kids;
        return wrap(n);
    });
    P(NT::Block, {T(TT::LBRACE), T(TT::RBRACE)}, [](std::vector<SemVal>& rhs) {
        return wrap(std::make_shared<Node>(NodeKind::Block, rhs[0].tok.line, rhs[0].tok.column));
    });

    // expr -> expr || and_expr | and_expr
    P(NT::Expr, {N(NT::Expr), T(TT::OR_OR), N(NT::AndExpr)}, binary);
    P(NT::Expr, {N(NT::AndExpr)}, passFirst);

    // and_expr -> and_expr && not_expr | not_expr
    P(NT::AndExpr, {N(NT::AndExpr), T(TT::AND_AND), N(NT::NotExpr)}, binary);
    P(NT::AndExpr, {N(NT::NotExpr)}, passFirst);

    // not_expr -> ! not_expr | rel_expr
    P(NT::NotExpr, {T(TT::NOT), N(NT::NotExpr)}, unary);
    P(NT::NotExpr, {N(NT::RelExpr)}, passFirst);

    // rel_expr -> arith_expr relop arith_expr | arith_expr
    P(NT::RelExpr, {N(NT::ArithExpr), N(NT::RelOp), N(NT::ArithExpr)}, binary);
    P(NT::RelExpr, {N(NT::ArithExpr)}, passFirst);

    // relop -> < | <= | > | >= | == | !=   (value = the operator token)
    for (TT op : {TT::LT, TT::LE, TT::GT, TT::GE, TT::EQ, TT::NE}) {
        P(NT::RelOp, {T(op)}, passFirst);
    }

    // arith_expr -> arith_expr + term | arith_expr - term | term
    P(NT::ArithExpr, {N(NT::ArithExpr), T(TT::PLUS), N(NT::Term)}, binary);
    P(NT::ArithExpr, {N(NT::ArithExpr), T(TT::MINUS), N(NT::Term)}, binary);
    P(NT::ArithExpr, {N(NT::Term)}, passFirst);

    // term -> term * factor | term / factor | term % factor | factor
    P(NT::Term, {N(NT::Term), T(TT::STAR), N(NT::Factor)}, binary);
    P(NT::Term, {N(NT::Term), T(TT::SLASH), N(NT::Factor)}, binary);
    P(NT::Term, {N(NT::Term), T(TT::PERCENT), N(NT::Factor)}, binary);
    P(NT::Term, {N(NT::Factor)}, passFirst);

    // factor -> - factor | ( expr ) | ID | ID [ expr ] | ID ( [args] ) | literals
    P(NT::Factor, {T(TT::MINUS), N(NT::Factor)}, unary);
    P(NT::Factor, {T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN)},
      [](std::vector<SemVal>& rhs) { return rhs[1]; });
    P(NT::Factor, {T(TT::IDENTIFIER)}, literal(NodeKind::Var));
    P(NT::Factor, {T(TT::IDENTIFIER), T(TT::LBRACKET), N(NT::Expr), T(TT::RBRACKET)},
      [](std::vector<SemVal>& rhs) {
          auto n = leaf(NodeKind::Index, rhs[0].tok);
          n->kids = {rhs[2].node};
          return wrap(n);
      });
    P(NT::Factor, {T(TT::IDENTIFIER), T(TT::LPAREN), N(NT::ExprList), T(TT::RPAREN)},
      [](std::vector<SemVal>& rhs) { return wrap(callNode(rhs[0].tok, rhs[2].node)); });
    P(NT::Factor, {T(TT::IDENTIFIER), T(TT::LPAREN), T(TT::RPAREN)},
      [](std::vector<SemVal>& rhs) { return wrap(callNode(rhs[0].tok, nullptr)); });
    P(NT::Factor, {T(TT::INT_LITERAL)}, literal(NodeKind::IntLit));
    P(NT::Factor, {T(TT::FLOAT_LITERAL)}, literal(NodeKind::FloatLit));
    P(NT::Factor, {T(TT::KW_TRUE)}, literal(NodeKind::BoolLit));
    P(NT::Factor, {T(TT::KW_FALSE)}, literal(NodeKind::BoolLit));

    return g;
}
} // namespace

const std::vector<Production>& grammar() {
    static const std::vector<Production> g = buildGrammar();
    return g;
}

std::string symbolName(Symbol s) {
    if (!isTerminal(s)) {
        static const char* names[] = {
            "S'", "program", "item_list", "item", "func_def", "func_proto", "params", "param",
            "stmt_list", "stmt", "decl_stmt", "type", "assign_stmt", "simple", "lvalue",
            "call_stmt", "return_stmt", "print_stmt", "break_stmt", "continue_stmt", "for_stmt",
            "for_init", "if_stmt", "while_stmt", "block", "expr_list", "expr", "and_expr",
            "not_expr", "rel_expr", "relop", "arith_expr", "term", "factor"};
        return names[s - NUM_TERMINALS];
    }
    switch (static_cast<TokenType>(s)) {
        case TT::IDENTIFIER:    return "id";
        case TT::INT_LITERAL:   return "int_lit";
        case TT::FLOAT_LITERAL: return "float_lit";
        case TT::KW_INT:        return "int";
        case TT::KW_FLOAT:      return "float";
        case TT::KW_IF:         return "if";
        case TT::KW_ELSE:       return "else";
        case TT::KW_WHILE:      return "while";
        case TT::KW_TRUE:       return "true";
        case TT::KW_FALSE:      return "false";
        case TT::KW_VOID:       return "void";
        case TT::KW_RETURN:     return "return";
        case TT::KW_FOR:        return "for";
        case TT::KW_PRINT:      return "print";
        case TT::KW_BREAK:      return "break";
        case TT::KW_CONTINUE:   return "continue";
        case TT::PLUS_ASSIGN:   return "+=";
        case TT::MINUS_ASSIGN:  return "-=";
        case TT::STAR_ASSIGN:   return "*=";
        case TT::SLASH_ASSIGN:  return "/=";
        case TT::PERCENT_ASSIGN:return "%=";
        case TT::PLUS_PLUS:     return "++";
        case TT::MINUS_MINUS:   return "--";
        case TT::LBRACKET:      return "[";
        case TT::RBRACKET:      return "]";
        case TT::ASSIGN:        return "=";
        case TT::LPAREN:        return "(";
        case TT::RPAREN:        return ")";
        case TT::LBRACE:        return "{";
        case TT::RBRACE:        return "}";
        case TT::SEMICOLON:     return ";";
        case TT::COMMA:         return ",";
        case TT::END_OF_FILE:   return "$";
        default:                return opSpelling(static_cast<TokenType>(s));
    }
}

std::string productionText(int prodIndex) {
    const Production& p = grammar()[prodIndex];
    std::string s = symbolName(p.lhs) + " ->";
    for (Symbol sym : p.rhs) s += " " + symbolName(sym);
    return s;
}
