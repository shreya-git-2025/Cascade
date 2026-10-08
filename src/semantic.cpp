#include "../include/semantic.h"

#include <iomanip>
#include <stdexcept>

namespace {
bool isNumeric(Type t) { return t == Type::Int || t == Type::Float; }

bool isArithmetic(TokenType op) {
    return op == TokenType::PLUS || op == TokenType::MINUS || op == TokenType::STAR ||
           op == TokenType::SLASH || op == TokenType::PERCENT;
}
bool isOrdering(TokenType op) {
    return op == TokenType::LT || op == TokenType::LE || op == TokenType::GT || op == TokenType::GE;
}
bool isEquality(TokenType op) { return op == TokenType::EQ || op == TokenType::NE; }
} // namespace

bool SemanticAnalyzer::analyze(const NodePtr& program) {
    pushScope(); // global scope
    visitStmts(program->kids);
    popScope();
    return errors_.empty();
}

SymbolEntry* SemanticAnalyzer::lookup(const std::string& name) {
    // innermost scope first: that's what makes shadowing work
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &entries_[found->second];
    }
    return nullptr;
}

void SemanticAnalyzer::error(const Node& at, const std::string& msg) {
    errors_.push_back(SemanticDiag{msg, at.line, at.column});
}

void SemanticAnalyzer::warning(const Node& at, const std::string& msg) {
    warnings_.push_back(SemanticDiag{msg, at.line, at.column});
}

// ---------------------------------------------------------------------------
// statements
// ---------------------------------------------------------------------------

void SemanticAnalyzer::visitStmts(const std::vector<NodePtr>& stmts) {
    for (const NodePtr& s : stmts) visitStmt(s);
}

void SemanticAnalyzer::visitStmt(const NodePtr& node) {
    Node& n = *node;
    switch (n.kind) {
        case NodeKind::Decl: {
            // The initializer is checked before the new name enters scope,
            // so in `int x = x + 1;` the right-hand x refers to an outer x
            // (or is undeclared) — never to the variable being declared.
            if (!n.kids.empty()) visitExpr(n.kids[0]);

            auto& scope = scopes_.back();
            auto existing = scope.find(n.text);
            if (existing != scope.end()) {
                const SymbolEntry& prev = entries_[existing->second];
                error(n, "redeclaration of '" + n.text + "' (first declared at line " +
                             std::to_string(prev.line) + ")");
                return;
            }

            int count = declCount_[n.text]++;
            std::string unique = count == 0 ? n.text : n.text + "." + std::to_string(count);
            entries_.push_back(SymbolEntry{n.text, unique, n.declType,
                                           static_cast<int>(scopes_.size()) - 1, n.line, n.column,
                                           !n.kids.empty()});
            scope[n.text] = static_cast<int>(entries_.size()) - 1;
            n.resolved = unique;

            if (!n.kids.empty()) checkStore(n, n.declType, n.kids[0], "variable '" + n.text + "'");
            return;
        }

        case NodeKind::Assign: {
            visitExpr(n.kids[0]);
            SymbolEntry* sym = lookup(n.text);
            if (!sym) {
                error(n, "assignment to undeclared variable '" + n.text + "'");
                return;
            }
            n.resolved = sym->unique;
            checkStore(n, sym->type, n.kids[0], "variable '" + n.text + "'");
            sym->assigned = true;
            return;
        }

        case NodeKind::If:
            checkCondition(n.kids[0], "if");
            visitStmt(n.kids[1]);
            if (n.kids.size() == 3) visitStmt(n.kids[2]);
            return;

        case NodeKind::While:
            checkCondition(n.kids[0], "while");
            visitStmt(n.kids[1]);
            return;

        case NodeKind::Block:
            pushScope();
            visitStmts(n.kids);
            popScope();
            return;

        default:
            error(n, "internal error: unexpected node in statement position");
            return;
    }
}

void SemanticAnalyzer::checkCondition(NodePtr& cond, const std::string& construct) {
    Type t = visitExpr(cond);
    if (t == Type::Error || t == Type::Bool) return;
    error(*cond, "condition of '" + construct + "' must be a boolean expression, got " +
                     typeName(t) + " (write e.g. 'x != 0')");
}

// Type rule for storing a value: exact match, or int -> float widening.
// float -> int narrowing and anything involving bool are rejected.
bool SemanticAnalyzer::checkStore(const Node& at, Type target, NodePtr& value, const std::string& what) {
    Type vt = value->type;
    if (vt == Type::Error || vt == target) return true;
    if (target == Type::Float && vt == Type::Int) {
        coerceToFloat(value);
        return true;
    }
    if (vt == Type::Bool) {
        error(at, "cannot store a boolean value in " + typeName(target) + " " + what +
                      " (the language has no bool variables)");
    } else {
        error(at, "type mismatch: cannot assign " + typeName(vt) + " to " + typeName(target) + " " +
                      what + " (implicit float-to-int narrowing is not allowed)");
    }
    return false;
}

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

void SemanticAnalyzer::coerceToFloat(NodePtr& node) {
    if (node->type != Type::Int) return;
    auto conv = std::make_shared<Node>(NodeKind::IntToFloat, node->line, node->column);
    conv->type = Type::Float;
    conv->kids = {node};
    node = conv;
}

Type SemanticAnalyzer::visitExpr(NodePtr& node) {
    Node& n = *node;
    Type result = Type::Error;

    switch (n.kind) {
        case NodeKind::IntLit:
            try {
                (void)std::stoll(n.text);
                result = Type::Int;
            } catch (const std::out_of_range&) {
                error(n, "integer literal '" + n.text + "' is out of range");
            }
            break;

        case NodeKind::FloatLit:
            try {
                (void)std::stod(n.text);
                result = Type::Float;
            } catch (const std::out_of_range&) {
                error(n, "float literal '" + n.text + "' is out of range");
            }
            break;

        case NodeKind::BoolLit:
            result = Type::Bool;
            break;

        case NodeKind::Var: {
            SymbolEntry* sym = lookup(n.text);
            if (!sym) {
                error(n, "use of undeclared variable '" + n.text + "'");
                break;
            }
            n.resolved = sym->unique;
            result = sym->type;
            if (!sym->assigned && !sym->warned) {
                sym->warned = true;
                warning(n, "'" + n.text + "' is read before it is assigned a value (it defaults to 0)");
            }
            break;
        }

        case NodeKind::Unary: {
            Type t = visitExpr(n.kids[0]);
            if (t == Type::Error) break;
            if (n.op == TokenType::MINUS) {
                if (isNumeric(t)) result = t;
                else error(n, "unary '-' needs a numeric operand, got " + typeName(t));
            } else { // NOT
                if (t == Type::Bool) result = Type::Bool;
                else error(n, "'!' needs a boolean operand, got " + typeName(t));
            }
            break;
        }

        case NodeKind::Binary: {
            Type l = visitExpr(n.kids[0]);
            Type r = visitExpr(n.kids[1]);
            if (l == Type::Error || r == Type::Error) break;
            std::string op = "'" + opSpelling(n.op) + "'";

            if (n.op == TokenType::AND_AND || n.op == TokenType::OR_OR) {
                if (l == Type::Bool && r == Type::Bool) result = Type::Bool;
                else error(n, op + " needs boolean operands, got " + typeName(l) + " and " + typeName(r));
                break;
            }

            if (isEquality(n.op) && l == Type::Bool && r == Type::Bool) {
                result = Type::Bool;
                break;
            }

            if (!isNumeric(l) || !isNumeric(r)) {
                error(n, op + " needs numeric operands, got " + typeName(l) + " and " + typeName(r));
                break;
            }

            if (n.op == TokenType::PERCENT && (l != Type::Int || r != Type::Int)) {
                error(n, "'%' needs int operands, got " + typeName(l) + " and " + typeName(r));
                break;
            }

            // usual arithmetic conversion: int op float -> float op float
            Type common = (l == Type::Float || r == Type::Float) ? Type::Float : Type::Int;
            if (common == Type::Float) {
                coerceToFloat(n.kids[0]);
                coerceToFloat(n.kids[1]);
            }
            if (isArithmetic(n.op)) result = common;
            else if (isOrdering(n.op) || isEquality(n.op)) result = Type::Bool;
            break;
        }

        default:
            error(n, "internal error: unexpected node in expression position");
            break;
    }

    // `node` may have been replaced (IntToFloat) only on children, never on
    // itself here, so annotating through `n` is safe.
    n.type = result;
    return result;
}

void SemanticAnalyzer::printSymbolTable(std::ostream& out) const {
    out << std::left << std::setw(10) << "NAME" << std::setw(10) << "RESOLVED" << std::setw(8)
        << "TYPE" << std::setw(7) << "SCOPE" << "DECLARED\n";
    out << std::string(45, '-') << "\n";
    for (const SymbolEntry& e : entries_) {
        out << std::left << std::setw(10) << e.name << std::setw(10) << e.unique << std::setw(8)
            << typeName(e.type) << std::setw(7) << e.scopeDepth << "line " << e.line << ", col "
            << e.column << "\n";
    }
}
