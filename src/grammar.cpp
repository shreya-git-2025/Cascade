#include "../include/grammar.h"

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
    return t.type == TT::KW_FLOAT ? Type::Float : Type::Int;
}

std::vector<Production> buildGrammar() {
    std::vector<Production> g;
    auto P = [&g](NT lhs, std::vector<Symbol> rhs, Action act) {
        g.push_back(Production{N(lhs), std::move(rhs), std::move(act)});
    };

    // 0: S' -> program   (augmented start; reducing it means "accept")
    P(NT::ProgramPrime, {N(NT::Program)}, passFirst);

    // program -> stmt_list
    P(NT::Program, {N(NT::StmtList)}, [](std::vector<SemVal>& rhs) {
        auto n = std::make_shared<Node>(NodeKind::Program, 1, 1);
        n->kids = rhs[0].node->kids;
        return wrap(n);
    });

    // stmt_list -> stmt_list stmt | stmt
    P(NT::StmtList, {N(NT::StmtList), N(NT::Stmt)}, [](std::vector<SemVal>& rhs) {
        rhs[0].node->kids.push_back(rhs[1].node);
        return rhs[0];
    });
    P(NT::StmtList, {N(NT::Stmt)}, [](std::vector<SemVal>& rhs) {
        auto n = std::make_shared<Node>(NodeKind::StmtList, rhs[0].node->line, rhs[0].node->column);
        n->kids.push_back(rhs[0].node);
        return wrap(n);
    });

    // stmt -> decl_stmt | assign_stmt | if_stmt | while_stmt | block
    P(NT::Stmt, {N(NT::DeclStmt)}, passFirst);
    P(NT::Stmt, {N(NT::AssignStmt)}, passFirst);
    P(NT::Stmt, {N(NT::IfStmt)}, passFirst);
    P(NT::Stmt, {N(NT::WhileStmt)}, passFirst);
    P(NT::Stmt, {N(NT::Block)}, passFirst);

    // decl_stmt -> type ID = expr ; | type ID ;
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::ASSIGN), N(NT::Expr), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = leaf(NodeKind::Decl, rhs[1].tok);
          n->line = rhs[0].tok.line;
          n->column = rhs[0].tok.column;
          n->declType = typeFromToken(rhs[0].tok);
          n->kids = {rhs[3].node};
          return wrap(n);
      });
    P(NT::DeclStmt, {N(NT::Type), T(TT::IDENTIFIER), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = leaf(NodeKind::Decl, rhs[1].tok);
          n->line = rhs[0].tok.line;
          n->column = rhs[0].tok.column;
          n->declType = typeFromToken(rhs[0].tok);
          return wrap(n);
      });

    // type -> INT | FLOAT   (value = the keyword token itself)
    P(NT::Type, {T(TT::KW_INT)}, passFirst);
    P(NT::Type, {T(TT::KW_FLOAT)}, passFirst);

    // assign_stmt -> ID = expr ;
    P(NT::AssignStmt, {T(TT::IDENTIFIER), T(TT::ASSIGN), N(NT::Expr), T(TT::SEMICOLON)},
      [](std::vector<SemVal>& rhs) {
          auto n = leaf(NodeKind::Assign, rhs[0].tok);
          n->kids = {rhs[2].node};
          return wrap(n);
      });

    // if_stmt -> IF ( expr ) block | IF ( expr ) block ELSE block
    P(NT::IfStmt, {T(TT::KW_IF), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), N(NT::Block)},
      [](std::vector<SemVal>& rhs) {
          auto n = std::make_shared<Node>(NodeKind::If, rhs[0].tok.line, rhs[0].tok.column);
          n->kids = {rhs[2].node, rhs[4].node};
          return wrap(n);
      });
    P(NT::IfStmt, {T(TT::KW_IF), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), N(NT::Block),
                   T(TT::KW_ELSE), N(NT::Block)},
      [](std::vector<SemVal>& rhs) {
          auto n = std::make_shared<Node>(NodeKind::If, rhs[0].tok.line, rhs[0].tok.column);
          n->kids = {rhs[2].node, rhs[4].node, rhs[6].node};
          return wrap(n);
      });

    // while_stmt -> WHILE ( expr ) block
    P(NT::WhileStmt, {T(TT::KW_WHILE), T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN), N(NT::Block)},
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

    // factor -> - factor | ( expr ) | ID | INT_LITERAL | FLOAT_LITERAL | TRUE | FALSE
    P(NT::Factor, {T(TT::MINUS), N(NT::Factor)}, unary);
    P(NT::Factor, {T(TT::LPAREN), N(NT::Expr), T(TT::RPAREN)},
      [](std::vector<SemVal>& rhs) { return rhs[1]; });
    P(NT::Factor, {T(TT::IDENTIFIER)}, literal(NodeKind::Var));
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
            "S'", "program", "stmt_list", "stmt", "decl_stmt", "type", "assign_stmt",
            "if_stmt", "while_stmt", "block", "expr", "and_expr", "not_expr",
            "rel_expr", "relop", "arith_expr", "term", "factor"};
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
