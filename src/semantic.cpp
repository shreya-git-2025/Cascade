#include "../include/semantic.h"
#include <algorithm>

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
    // Pre-pass: record every function's signature so calls can be checked
    // regardless of where the definition sits (recursion, mutual recursion).
    for (const NodePtr& item : program->kids) {
        if (item->kind == NodeKind::FuncDef || item->kind == NodeKind::FuncProto) declareFunction(*item);
    }
    for (const auto& [name, sig] : funcs_) {
        if (!sig.defined) {
            errors_.push_back(SemanticDiag{"function '" + name + "' is declared but never defined",
                                           sig.line, sig.column});
        }
    }

    pushScope(); // global scope
    for (const NodePtr& item : program->kids) {
        if (item->kind == NodeKind::FuncDef) visitFuncDef(item);
        else if (item->kind == NodeKind::FuncProto) continue; // nothing to check in a prototype
        else visitStmt(item);
    }
    popScope();

    auto entry = funcs_.find("main");
    if (entry != funcs_.end() && !entry->second.paramTypes.empty()) {
        errors_.push_back(SemanticDiag{"'main' must not take parameters", entry->second.line,
                                       entry->second.column});
    }
    // Report in source order (the function pre-pass and the main pass find
    // errors out of order, and the function table is an unordered map).
    std::stable_sort(errors_.begin(), errors_.end(), [](const SemanticDiag& a, const SemanticDiag& b) {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    return errors_.empty();
}

void SemanticAnalyzer::declareFunction(const Node& def) {
    const bool isDefinition = def.kind == NodeKind::FuncDef;
    FuncSig sig;
    sig.name = def.text;
    sig.ret = def.declType;
    sig.line = def.line;
    sig.column = def.column;
    sig.defined = isDefinition;
    size_t nParams = isDefinition ? def.kids.size() - 1 : def.kids.size(); // a definition's last kid is its body
    for (size_t i = 0; i < nParams; ++i) {
        sig.paramTypes.push_back(def.kids[i]->declType);
        sig.paramIsArray.push_back(def.kids[i]->isArray);
    }

    auto existing = funcs_.find(def.text);
    if (existing == funcs_.end()) {
        funcs_[def.text] = sig;
        return;
    }
    FuncSig& prev = existing->second;
    if (prev.ret != sig.ret || prev.paramTypes != sig.paramTypes || prev.paramIsArray != sig.paramIsArray) {
        error(def, "conflicting declaration of function '" + def.text + "' (first declared at line " +
                       std::to_string(prev.line) + " with a different signature)");
        return;
    }
    if (isDefinition && prev.defined) {
        error(def, "redefinition of function '" + def.text + "' (first defined at line " +
                       std::to_string(prev.line) + ")");
        return;
    }
    if (isDefinition) { prev.defined = true; prev.line = def.line; prev.column = def.column; }
}

void SemanticAnalyzer::visitFuncDef(const NodePtr& def) {
    Node& n = *def;
    curFunc_ = n.text;
    curRet_ = n.declType;
    inFunc_ = true;

    pushScope(); // parameters live in their own scope, below the body block's
    for (size_t i = 0; i + 1 < n.kids.size(); ++i) {
        Node& p = *n.kids[i];
        auto& scope = scopes_.back();
        auto existing = scope.find(p.text);
        if (existing != scope.end()) {
            error(p, "duplicate parameter name '" + p.text + "' in function '" + n.text + "'");
            continue;
        }
        int count = declCount_[p.text]++;
        std::string unique = count == 0 ? p.text : p.text + "." + std::to_string(count);
        entries_.push_back(SymbolEntry{p.text, unique, p.declType,
                                       static_cast<int>(scopes_.size()) - 1, p.line, p.column,
                                       /*assigned=*/true, /*warned=*/false, p.isArray,
                                       /*arraySize=*/0, curFunc_});
        scope[p.text] = static_cast<int>(entries_.size()) - 1;
        p.resolved = unique;
    }
    visitStmt(n.kids.back());
    popScope();

    inFunc_ = false;
    curFunc_.clear();
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
            for (NodePtr& init : n.kids) visitExpr(init);

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
                                           /*assigned=*/n.isArray || !n.kids.empty(),
                                           /*warned=*/false, n.isArray,
                                           n.isArray ? n.arraySize : 0, curFunc_});
            scope[n.text] = static_cast<int>(entries_.size()) - 1;
            n.resolved = unique;

            if (n.isArray) {
                if (n.arraySize <= 0 || n.arraySize > 1000000) {
                    error(n, "array '" + n.text + "' must have a size between 1 and 1000000");
                } else if (static_cast<long long>(n.kids.size()) > n.arraySize) {
                    error(n, "too many initializers for array '" + n.text + "' (size " +
                                 std::to_string(n.arraySize) + ", " +
                                 std::to_string(n.kids.size()) + " given)");
                }
                for (NodePtr& init : n.kids) {
                    checkStore(*init, n.declType, init, "element of array '" + n.text + "'");
                }
            } else if (!n.kids.empty()) {
                checkStore(n, n.declType, n.kids[0], "variable '" + n.text + "'");
            }
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
            if (sym->isArray) {
                error(n, "cannot assign to array '" + n.text +
                             "' as a whole (assign to an element, e.g. " + n.text + "[0] = ...)");
                return;
            }
            checkStore(n, sym->type, n.kids[0], "variable '" + n.text + "'");
            sym->assigned = true;
            return;
        }

        case NodeKind::ArrAssign: {
            checkIndex(n.kids[0]);
            visitExpr(n.kids[1]);
            SymbolEntry* sym = lookup(n.text);
            if (!sym) {
                error(n, "assignment to element of undeclared array '" + n.text + "'");
                return;
            }
            if (!sym->isArray) {
                error(n, "'" + n.text + "' is not an array");
                return;
            }
            n.resolved = sym->unique;
            checkStore(n, sym->type, n.kids[1], "element of array '" + n.text + "'");
            return;
        }

        case NodeKind::Call:
            visitCall(n);
            return;

        case NodeKind::Return: {
            if (!inFunc_) {
                error(n, "'return' outside of a function");
                for (NodePtr& k : n.kids) visitExpr(k);
                return;
            }
            if (n.kids.empty()) {
                if (curRet_ != Type::Void) {
                    error(n, "function '" + curFunc_ + "' must return a value of type " + typeName(curRet_));
                }
                return;
            }
            Type t = visitExpr(n.kids[0]);
            if (t == Type::Bool && isNumeric(curRet_)) { // `return a < b;` returns 0 / 1
                boolToInt(n.kids[0]);
                t = Type::Int;
            }
            if (curRet_ == Type::Void) {
                error(n, "void function '" + curFunc_ + "' cannot return a value");
            } else if (t != Type::Error && t != curRet_) {
                if (curRet_ == Type::Float && t == Type::Int) coerceToFloat(n.kids[0]);
                else error(n, "return type mismatch: function '" + curFunc_ + "' returns " +
                                  typeName(curRet_) + ", got " + typeName(t));
            }
            return;
        }

        case NodeKind::Print: {
            Type t = visitExpr(n.kids[0]);
            if (t == Type::Bool) { // print(a < b) prints 0 or 1
                boolToInt(n.kids[0]);
                t = Type::Int;
            }
            if (t != Type::Error && !isNumeric(t)) {
                error(n, "print needs an int or float value, got " + typeName(t));
            }
            return;
        }

        case NodeKind::Break:
            if (loopDepth_ == 0) error(n, "'break' outside of a loop");
            return;

        case NodeKind::Continue:
            if (loopDepth_ == 0) error(n, "'continue' outside of a loop");
            return;

        case NodeKind::If:
            checkCondition(n.kids[0], "if");
            visitStmt(n.kids[1]);
            if (n.kids.size() == 3) visitStmt(n.kids[2]);
            return;

        case NodeKind::While:
            checkCondition(n.kids[0], "while");
            ++loopDepth_;
            visitStmt(n.kids[1]);
            --loopDepth_;
            if (n.kids.size() == 3) visitStmt(n.kids[2]); // the lowered `for` step
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
    if (isNumeric(t)) { // C-style truthiness: a number is true when it is not 0
        toBool(cond);
        return;
    }
    error(*cond, "condition of '" + construct + "' must be a boolean or numeric expression, got " +
                     typeName(t));
}

void SemanticAnalyzer::toBool(NodePtr& node) {
    auto zero = std::make_shared<Node>(node->type == Type::Float ? NodeKind::FloatLit : NodeKind::IntLit,
                                       node->line, node->column);
    zero->text = node->type == Type::Float ? "0.0" : "0";
    zero->type = node->type;
    auto test = std::make_shared<Node>(NodeKind::Binary, node->line, node->column);
    test->op = TokenType::NE;
    test->type = Type::Bool;
    test->kids = {node, zero};
    node = test;
}

void SemanticAnalyzer::boolToInt(NodePtr& node) {
    auto conv = std::make_shared<Node>(NodeKind::BoolToInt, node->line, node->column);
    conv->type = Type::Int;
    conv->kids = {node};
    node = conv;
}

// Type rule for storing a value: exact match, or int -> float widening.
// float -> int narrowing and anything involving bool are rejected.
bool SemanticAnalyzer::checkStore(const Node& at, Type target, NodePtr& value, const std::string& what) {
    Type vt = value->type;
    if (vt == Type::Error || vt == target) return true;
    if (vt == Type::Void) {
        error(at, "cannot use the result of a void function as a value");
        return false;
    }
    if (target == Type::Float && vt == Type::Int) {
        coerceToFloat(value);
        return true;
    }
    if (vt == Type::Bool && isNumeric(target)) { // 0 / 1, as in C
        boolToInt(value);
        if (target == Type::Float) coerceToFloat(value);
        return true;
    }
    if (vt == Type::Bool) {
        error(at, "cannot store a boolean value in " + typeName(target) + " " + what);
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
            if (sym->isArray) {
                error(n, "array '" + n.text + "' cannot be used as a value (use " + n.text +
                             "[i], or pass it directly as a function argument)");
                break;
            }
            result = sym->type;
            // Globals read inside a function may well be assigned by the time
            // it runs, so only warn about variables owned by the current scope.
            if (!sym->assigned && !sym->warned && sym->owner == curFunc_) {
                sym->warned = true;
                warning(n, "'" + n.text + "' is read before it is assigned a value (it defaults to 0)");
            }
            break;
        }

        case NodeKind::Index: {
            checkIndex(n.kids[0]);
            SymbolEntry* sym = lookup(n.text);
            if (!sym) {
                error(n, "use of undeclared array '" + n.text + "'");
                break;
            }
            if (!sym->isArray) {
                error(n, "'" + n.text + "' is not an array");
                break;
            }
            n.resolved = sym->unique;
            result = sym->type;
            break;
        }

        case NodeKind::Call:
            result = visitCall(n);
            break;

        case NodeKind::Unary: {
            Type t = visitExpr(n.kids[0]);
            if (t == Type::Error) break;
            if (n.op == TokenType::MINUS) {
                if (isNumeric(t)) result = t;
                else error(n, "unary '-' needs a numeric operand, got " + typeName(t));
            } else { // NOT
                if (t == Type::Bool) result = Type::Bool;
                else if (isNumeric(t)) { toBool(n.kids[0]); result = Type::Bool; }
                else error(n, "'!' needs a boolean or numeric operand, got " + typeName(t));
            }
            break;
        }

        case NodeKind::Binary: {
            Type l = visitExpr(n.kids[0]);
            Type r = visitExpr(n.kids[1]);
            if (l == Type::Error || r == Type::Error) break;
            std::string op = "'" + opSpelling(n.op) + "'";

            if (n.op == TokenType::AND_AND || n.op == TokenType::OR_OR) {
                auto truthy = [](Type t) { return t == Type::Bool || isNumeric(t); };
                if (truthy(l) && truthy(r)) {
                    if (l != Type::Bool) toBool(n.kids[0]); // numbers count as true when non-zero
                    if (r != Type::Bool) toBool(n.kids[1]);
                    result = Type::Bool;
                } else {
                    error(n, op + " needs boolean or numeric operands, got " + typeName(l) + " and " + typeName(r));
                }
                break;
            }

            if (isEquality(n.op) && l == Type::Bool && r == Type::Bool) {
                result = Type::Bool;
                break;
            }

            // As in C, a comparison result used in arithmetic (or compared
            // with a number) is 0 or 1: `(a > b) - (a < b)`, `count += x > 0`.
            if (l == Type::Bool && (isNumeric(r) || r == Type::Bool)) { boolToInt(n.kids[0]); l = Type::Int; }
            if (r == Type::Bool && isNumeric(l)) { boolToInt(n.kids[1]); r = Type::Int; }

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

void SemanticAnalyzer::checkIndex(NodePtr& index) {
    Type t = visitExpr(index);
    if (t != Type::Error && t != Type::Int) {
        error(*index, "array index must be an int, got " + typeName(t));
    }
}

// Checks a call against the callee's signature and returns its result type.
Type SemanticAnalyzer::visitCall(Node& n) {
    auto it = funcs_.find(n.text);
    if (it == funcs_.end()) {
        error(n, "call to undefined function '" + n.text + "'");
        return Type::Error;
    }
    const FuncSig& sig = it->second;
    if (n.kids.size() != sig.paramTypes.size()) {
        error(n, "function '" + n.text + "' expects " + std::to_string(sig.paramTypes.size()) +
                     " argument(s), got " + std::to_string(n.kids.size()));
        return sig.ret;
    }

    for (size_t i = 0; i < n.kids.size(); ++i) {
        NodePtr& arg = n.kids[i];
        std::string where = "argument " + std::to_string(i + 1) + " of '" + n.text + "'";
        Type want = sig.paramTypes[i];

        if (sig.paramIsArray[i]) {
            // An array parameter takes a bare array name; the array itself is
            // shared with the callee (only its base address is passed).
            if (arg->kind != NodeKind::Var) {
                error(*arg, where + " must be an array variable");
                continue;
            }
            SymbolEntry* sym = lookup(arg->text);
            if (!sym) {
                error(*arg, "use of undeclared variable '" + arg->text + "'");
            } else if (!sym->isArray) {
                error(*arg, where + " must be an array, but '" + arg->text + "' is not one");
            } else if (sym->type != want) {
                error(*arg, where + " expects " + typeName(want) + "[], but '" + arg->text +
                                "' is " + typeName(sym->type) + "[]");
            } else {
                arg->resolved = sym->unique;
                arg->type = sym->type;
            }
            continue;
        }

        Type got = visitExpr(arg);
        if (got == Type::Bool && isNumeric(want)) {
            boolToInt(arg);
            got = Type::Int;
        }
        if (got == Type::Error || got == want) continue;
        if (want == Type::Float && got == Type::Int) coerceToFloat(arg);
        else error(*arg, where + " expects " + typeName(want) + ", got " + typeName(got));
    }
    return sig.ret;
}

void SemanticAnalyzer::printSymbolTable(std::ostream& out) const {
    out << std::left << std::setw(10) << "NAME" << std::setw(10) << "RESOLVED" << std::setw(10)
        << "TYPE" << std::setw(7) << "SCOPE" << "DECLARED\n";
    out << std::string(45, '-') << "\n";
    for (const SymbolEntry& e : entries_) {
        out << std::left << std::setw(10) << e.name << std::setw(10) << e.unique << std::setw(10)
            << (typeName(e.type) + (e.isArray ? (e.arraySize ? "[" + std::to_string(e.arraySize) + "]" : "[]") : ""))
            << std::setw(7) << e.scopeDepth << "line " << e.line << ", col " << e.column
            << (e.owner.empty() ? "" : "  (in " + e.owner + ")") << "\n";
    }
}
