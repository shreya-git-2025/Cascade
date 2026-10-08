#include "../include/tac.h"

#include <iomanip>
#include <sstream>

namespace {
std::string formatFloat(double v) {
    std::ostringstream ss;
    ss << std::setprecision(15) << v;
    std::string s = ss.str();
    if (s.find_first_of(".eEni") == std::string::npos) s += ".0"; // keep 2.0 visibly a float
    return s;
}

TacOp arithOp(TokenType t) {
    switch (t) {
        case TokenType::PLUS:  return TacOp::Add;
        case TokenType::MINUS: return TacOp::Sub;
        case TokenType::STAR:  return TacOp::Mul;
        case TokenType::SLASH: return TacOp::Div;
        default:               return TacOp::Mod;
    }
}

const char* arithSymbol(TacOp op) {
    switch (op) {
        case TacOp::Add: return "+";
        case TacOp::Sub: return "-";
        case TacOp::Mul: return "*";
        case TacOp::Div: return "/";
        case TacOp::Mod: return "%";
        default:         return "?";
    }
}
} // namespace

std::string operandText(const Operand& o) {
    switch (o.kind) {
        case Operand::Kind::Var:
        case Operand::Kind::Temp:       return o.name;
        case Operand::Kind::IntConst:   return std::to_string(o.ival);
        case Operand::Kind::FloatConst: return formatFloat(o.fval);
        case Operand::Kind::None:       return "";
    }
    return "";
}

void TacProgram::print(std::ostream& out) const {
    auto tgt = [](int t) { return t < 0 ? std::string("_") : std::to_string(t); };
    for (size_t i = 0; i < code.size(); ++i) {
        const Quad& q = code[i];
        out << std::right << std::setw(4) << i << ":  ";
        switch (q.op) {
            case TacOp::Copy:
                out << operandText(q.result) << " = " << operandText(q.arg1);
                break;
            case TacOp::Add: case TacOp::Sub: case TacOp::Mul: case TacOp::Div: case TacOp::Mod:
                out << operandText(q.result) << " = " << operandText(q.arg1) << " "
                    << arithSymbol(q.op) << " " << operandText(q.arg2);
                break;
            case TacOp::Neg:
                out << operandText(q.result) << " = - " << operandText(q.arg1);
                break;
            case TacOp::IntToFloat:
                out << operandText(q.result) << " = (float) " << operandText(q.arg1);
                break;
            case TacOp::IfGoto:
                out << "if " << operandText(q.arg1) << " " << opSpelling(q.relop) << " "
                    << operandText(q.arg2) << " goto " << tgt(q.target);
                break;
            case TacOp::Goto:
                out << "goto " << tgt(q.target);
                break;
            case TacOp::Halt:
                out << "halt";
                break;
        }
        out << "\n";
    }
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

int TacGenerator::emit(Quad q) {
    q.line = curLine_;
    prog_.code.push_back(q);
    return nextInstr() - 1;
}

void TacGenerator::addSlot(const std::string& name, Type t) {
    if (slotType_.emplace(name, t).second) prog_.slots.emplace_back(name, t);
}

Operand TacGenerator::newTemp(Type t) {
    std::string name = "t" + std::to_string(++tempCount_);
    addSlot(name, t);
    return Operand::temp(name);
}

TacGenerator::List TacGenerator::merge(const List& a, const List& b) {
    List out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

void TacGenerator::backpatch(const List& list, int target) {
    for (int i : list) prog_.code[i].target = target;
}

// ---------------------------------------------------------------------------
// program / statements
// ---------------------------------------------------------------------------

TacProgram TacGenerator::generate(const NodePtr& program, const std::vector<SymbolEntry>& symbols) {
    prog_ = TacProgram{};
    slotType_.clear();
    tempCount_ = 0;
    for (const SymbolEntry& s : symbols) {
        addSlot(s.unique, s.type);
        prog_.userVars.push_back(s.unique);
    }

    // program -> stmt_list:  backpatch(stmt_list.nextlist, <halt>)
    List next = genStmts(program->kids);
    backpatch(next, nextInstr());
    emit(Quad{TacOp::Halt, {}, {}, {}});
    return prog_;
}

// L -> L1 M S:  backpatch(L1.nextlist, M.instr); L.nextlist = S.nextlist
TacGenerator::List TacGenerator::genStmts(const std::vector<NodePtr>& stmts) {
    List next;
    for (const NodePtr& s : stmts) {
        backpatch(next, nextInstr());
        next = genStmt(s);
    }
    return next;
}

TacGenerator::List TacGenerator::genStmt(const NodePtr& node) {
    const Node& n = *node;
    curLine_ = n.line;
    switch (n.kind) {
        case NodeKind::Decl: {
            // `int x;` is zero-initialized (see semantic.h for the policy)
            Operand value = n.kids.empty()
                ? (n.declType == Type::Float ? Operand::floatConst(0.0) : Operand::intConst(0))
                : genValue(n.kids[0]);
            curLine_ = n.line;
            emit(Quad{TacOp::Copy, Operand::var(n.resolved), value, {}});
            return {};
        }

        case NodeKind::Assign: {
            Operand value = genValue(n.kids[0]);
            curLine_ = n.line;
            emit(Quad{TacOp::Copy, Operand::var(n.resolved), value, {}});
            return {};
        }

        case NodeKind::If: {
            BoolLists c = genCond(n.kids[0]);
            // S -> if ( B ) M1 S1 [ N else M2 S2 ]
            backpatch(c.truelist, nextInstr());               // M1
            List thenNext = genStmt(n.kids[1]);
            if (n.kids.size() == 2) {
                return merge(c.falselist, thenNext);
            }
            int skipElse = emit(Quad{TacOp::Goto, {}, {}, {}}); // N: jump over else
            backpatch(c.falselist, nextInstr());              // M2
            List elseNext = genStmt(n.kids[2]);
            return merge(merge(thenNext, makeList(skipElse)), elseNext);
        }

        case NodeKind::While: {
            // S -> while M1 ( B ) M2 S1
            int loopStart = nextInstr();                      // M1
            BoolLists c = genCond(n.kids[0]);
            backpatch(c.truelist, nextInstr());               // M2
            List bodyNext = genStmt(n.kids[1]);
            backpatch(bodyNext, loopStart);
            curLine_ = n.line;
            Quad back{TacOp::Goto, {}, {}, {}};
            back.target = loopStart;
            emit(back);
            return c.falselist;
        }

        case NodeKind::Block:
            return genStmts(n.kids);

        default:
            return {};
    }
}

// ---------------------------------------------------------------------------
// expressions
// ---------------------------------------------------------------------------

Operand TacGenerator::genValue(const NodePtr& node) {
    const Node& n = *node;
    switch (n.kind) {
        case NodeKind::IntLit:   return Operand::intConst(std::stoll(n.text));
        case NodeKind::FloatLit: return Operand::floatConst(std::stod(n.text));
        case NodeKind::Var:      return Operand::var(n.resolved);

        case NodeKind::IntToFloat: {
            Operand a = genValue(n.kids[0]);
            // fold conversions of constants instead of emitting t = (float) 3
            if (a.kind == Operand::Kind::IntConst) return Operand::floatConst(static_cast<double>(a.ival));
            Operand t = newTemp(Type::Float);
            curLine_ = n.line;
            emit(Quad{TacOp::IntToFloat, t, a, {}});
            return t;
        }

        case NodeKind::Unary: { // only unary minus has a numeric value
            Operand a = genValue(n.kids[0]);
            Operand t = newTemp(n.type);
            curLine_ = n.line;
            Quad q{TacOp::Neg, t, a, {}};
            q.isFloat = n.type == Type::Float;
            emit(q);
            return t;
        }

        case NodeKind::Binary: {
            Operand a = genValue(n.kids[0]);
            Operand b = genValue(n.kids[1]);
            Operand t = newTemp(n.type);
            curLine_ = n.line;
            Quad q{arithOp(n.op), t, a, b};
            q.isFloat = n.type == Type::Float;
            emit(q);
            return t;
        }

        default:
            return {};
    }
}

TacGenerator::BoolLists TacGenerator::genCond(const NodePtr& node) {
    const Node& n = *node;
    curLine_ = n.line;

    if (n.kind == NodeKind::BoolLit) {
        // B -> true:  truelist = {goto _}     B -> false:  falselist = {goto _}
        int j = emit(Quad{TacOp::Goto, {}, {}, {}});
        if (n.text == "true") return {makeList(j), {}};
        return {{}, makeList(j)};
    }

    if (n.kind == NodeKind::Unary) { // B -> ! B1: swap the lists
        BoolLists b = genCond(n.kids[0]);
        return {b.falselist, b.truelist};
    }

    // n.kind == Binary from here on
    if (n.op == TokenType::OR_OR) {
        // B -> B1 || M B2
        BoolLists b1 = genCond(n.kids[0]);
        backpatch(b1.falselist, nextInstr());
        BoolLists b2 = genCond(n.kids[1]);
        return {merge(b1.truelist, b2.truelist), b2.falselist};
    }
    if (n.op == TokenType::AND_AND) {
        // B -> B1 && M B2
        BoolLists b1 = genCond(n.kids[0]);
        backpatch(b1.truelist, nextInstr());
        BoolLists b2 = genCond(n.kids[1]);
        return {b2.truelist, merge(b1.falselist, b2.falselist)};
    }

    // B -> E1 relop E2:  truelist = {if E1 relop E2 goto _}, falselist = {goto _}
    Operand a, b;
    bool isFloat = false;
    if (n.kids[0]->type == Type::Bool) {
        // (x < 1) == (y > 2): compare the two truth values as 0/1 ints
        a = materializeBool(n.kids[0]);
        b = materializeBool(n.kids[1]);
    } else {
        a = genValue(n.kids[0]);
        b = genValue(n.kids[1]);
        isFloat = n.kids[0]->type == Type::Float;
    }
    curLine_ = n.line;
    Quad test{TacOp::IfGoto, {}, a, b};
    test.relop = n.op;
    test.isFloat = isFloat;
    int t = emit(test);
    int f = emit(Quad{TacOp::Goto, {}, {}, {}});
    return {makeList(t), makeList(f)};
}

Operand TacGenerator::materializeBool(const NodePtr& node) {
    //      <B's jumps>
    //  T:  t = 1
    //      goto E
    //  F:  t = 0
    //  E:
    BoolLists b = genCond(node);
    Operand t = newTemp(Type::Int);
    backpatch(b.truelist, nextInstr());
    emit(Quad{TacOp::Copy, t, Operand::intConst(1), {}});
    int skip = emit(Quad{TacOp::Goto, {}, {}, {}});
    backpatch(b.falselist, nextInstr());
    emit(Quad{TacOp::Copy, t, Operand::intConst(0), {}});
    backpatch(makeList(skip), nextInstr());
    return t;
}
