#include "../include/ast.h"

std::string typeName(Type t) {
    switch (t) {
        case Type::Unknown: return "?";
        case Type::Int:     return "int";
        case Type::Float:   return "float";
        case Type::Bool:    return "bool";
        case Type::Error:   return "<error>";
        case Type::Void:    return "void";
    }
    return "?";
}

std::string opSpelling(TokenType t) {
    switch (t) {
        case TokenType::PLUS:    return "+";
        case TokenType::MINUS:   return "-";
        case TokenType::STAR:    return "*";
        case TokenType::SLASH:   return "/";
        case TokenType::PERCENT: return "%";
        case TokenType::LT:      return "<";
        case TokenType::LE:      return "<=";
        case TokenType::GT:      return ">";
        case TokenType::GE:      return ">=";
        case TokenType::EQ:      return "==";
        case TokenType::NE:      return "!=";
        case TokenType::AND_AND: return "&&";
        case TokenType::OR_OR:   return "||";
        case TokenType::NOT:     return "!";
        default:                 return tokenTypeName(t);
    }
}

namespace {
std::string annotate(const Node& n) {
    std::string s;
    if (!n.resolved.empty() && n.resolved != n.text) s += " -> " + n.resolved;
    if (n.type != Type::Unknown) s += "  : " + typeName(n.type);
    return s;
}
} // namespace

void printAst(const NodePtr& node, std::ostream& out, int depth) {
    if (!node) return;
    std::string pad(depth * 2, ' ');
    const Node& n = *node;
    out << pad;
    switch (n.kind) {
        case NodeKind::Program:    out << "Program"; break;
        case NodeKind::Block:      out << "Block"; break;
        case NodeKind::StmtList:   out << "StmtList"; break;
       case NodeKind::Decl:       out << "Decl " << typeName(n.declType) << " " << n.text
                                       << (n.isArray ? "[" + std::to_string(n.arraySize) + "]" : ""); break;
        case NodeKind::Assign:     out << "Assign " << n.text; break;
        case NodeKind::If:         out << (n.kids.size() == 3 ? "IfElse" : "If"); break;
        case NodeKind::While:      out << "While"; break;
        case NodeKind::IntLit:     out << "Int " << n.text; break;
        case NodeKind::FloatLit:   out << "Float " << n.text; break;
        case NodeKind::BoolLit:    out << "Bool " << n.text; break;
        case NodeKind::Var:        out << "Var " << n.text; break;
        case NodeKind::Unary:      out << "Unary " << opSpelling(n.op); break;
        case NodeKind::Binary:     out << "Binary " << opSpelling(n.op); break;
        case NodeKind::IntToFloat: out << "IntToFloat"; break;
        case NodeKind::FuncDecl:    out << "Func " << typeName(n.declType) << " " << n.text; break;
        case NodeKind::Param:       out << "Param " << typeName(n.declType) << " " << n.text << (n.isArray ? "[]" : ""); break;
        case NodeKind::Call:        out << "Call " << n.text; break;
        case NodeKind::Index:       out << "Index " << n.text; break;
        case NodeKind::ArrayAssign: out << "ArrayAssign " << n.text; break;
        case NodeKind::Return:      out << "Return"; break;
        case NodeKind::Print:       out << "Print"; break;
    }
    out << annotate(n) << "   [" << n.line << ":" << n.column << "]\n";
    for (const NodePtr& kid : n.kids) printAst(kid, out, depth + 1);
}
