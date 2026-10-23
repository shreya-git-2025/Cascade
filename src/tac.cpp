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
        for (const TacFunc& f : funcs) {
            if (f.entry == static_cast<int>(i)) {
                out << "      -- function " << f.name << " (" << f.nParams << " param"
                    << (f.nParams == 1 ? "" : "s") << ") --\n";
            }
        }
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
            case TacOp::Param:
                out << "param " << operandText(q.arg1);
                break;
            case TacOp::Call:
                if (q.result.kind != Operand::Kind::None) out << operandText(q.result) << " = ";
                out << "call " << q.func << ", " << q.argc;
                break;
            case TacOp::Return:
                out << "return";
                if (q.arg1.kind != Operand::Kind::None) out << " " << operandText(q.arg1);
                break;
            case TacOp::ArrLoad:
                out << operandText(q.result) << " = " << operandText(q.arg1) << "["
                    << operandText(q.arg2) << "]";
                break;
            case TacOp::ArrStore:
                out << operandText(q.arg1) << "[" << operandText(q.arg2) << "] = "
                    << operandText(q.result);
                break;
            case TacOp::Alloc:
                out << operandText(q.result) << " = alloc " << operandText(q.arg1)
                    << (q.isFloat ? " floats" : " ints");
                break;
            case TacOp::Print:
                out << "print " << operandText(q.arg1);
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

void TacGenerator::addSlotTo(int func, const std::string& name, Type t) {
    if (!slotType_.emplace(name, t).second) return;
    if (func < 0) prog_.slots.emplace_back(name, t);
    else prog_.funcs[func].slots.emplace_back(name, t);
}

void TacGenerator::addSlot(const std::string& name, Type t) { addSlotTo(curFunc_, name, t); }

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
    funcRet_.clear();
    loops_.clear();
    tempCount_ = 0;
    curFunc_ = -1;

    // Functions get their own slot tables; top-level code keeps the global one.
    std::vector<const Node*> defs;
    std::map<std::string, int> funcIndex;
    std::vector<NodePtr> topLevel;
    for (const NodePtr& item : program->kids) {
        if (item->kind == NodeKind::FuncProto) continue; // declarations generate no code
        if (item->kind != NodeKind::FuncDef) {
            topLevel.push_back(item);
            continue;
        }
        TacFunc f;
        f.name = item->text;
        f.ret = item->declType;
        f.nParams = static_cast<int>(item->kids.size()) - 1;
        funcIndex[f.name] = static_cast<int>(prog_.funcs.size());
        funcRet_[f.name] = f.ret;
        prog_.funcs.push_back(f);
        defs.push_back(item.get());
    }

    for (const SymbolEntry& s : symbols) {
        // An array variable's slot holds the array's address, an int.
        Type slotT = s.isArray ? Type::Int : s.type;
        if (s.isArray) prog_.arrayElem[s.unique] = s.type;
        if (s.owner.empty()) {
            addSlotTo(-1, s.unique, slotT);
            prog_.userVars.push_back(s.unique);
        } else {
            addSlotTo(funcIndex.at(s.owner), s.unique, slotT);
        }
    }

    // program -> item_list:  backpatch(nextlist, <next instruction>)
    List next = genStmts(topLevel);
    backpatch(next, nextInstr());
    auto mainFn = funcIndex.find("main");
    if (mainFn != funcIndex.end() && prog_.funcs[mainFn->second].nParams == 0) {
        Quad call{TacOp::Call, {}, {}, {}};
        call.func = "main";
        call.argc = 0;
        curLine_ = defs[mainFn->second]->line;
        emit(call);
    }
    emit(Quad{TacOp::Halt, {}, {}, {}});

    // Function bodies follow the top-level code, one after another.
    for (size_t i = 0; i < defs.size(); ++i) {
        curFunc_ = static_cast<int>(i);
        const Node& def = *defs[i];
        prog_.funcs[i].entry = nextInstr();
        curLine_ = def.line;
        List bodyNext = genStmt(def.kids.back());
        backpatch(bodyNext, nextInstr());
        // Falling off the end returns 0 (or nothing, for void functions).
        Quad ret{TacOp::Return, {}, {}, {}};
        if (def.declType == Type::Float) ret.arg1 = Operand::floatConst(0.0);
        else if (def.declType == Type::Int) ret.arg1 = Operand::intConst(0);
        emit(ret);
    }
    curFunc_ = -1;
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
            if (n.isArray) {
                // int a[N] = {...};   a = alloc N;  a[0] = ...;  a[1] = ...
                Quad alloc{TacOp::Alloc, Operand::var(n.resolved), Operand::intConst(n.arraySize), {}};
                alloc.isFloat = n.declType == Type::Float;
                emit(alloc);
                for (size_t k = 0; k < n.kids.size(); ++k) {
                    Operand value = genValue(n.kids[k]);
                    curLine_ = n.line;
                    emit(Quad{TacOp::ArrStore, value, Operand::var(n.resolved),
                              Operand::intConst(static_cast<long long>(k))});
                }
                return {};
            }
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
            // S -> while M1 ( B ) M2 S1 [ step ]
            int loopStart = nextInstr();                      // M1
            BoolLists c = genCond(n.kids[0]);
            backpatch(c.truelist, nextInstr());               // M2
            loops_.emplace_back();
            List bodyNext = genStmt(n.kids[1]);
            LoopCtx ctx = loops_.back();
            loops_.pop_back();
            if (n.kids.size() == 3) {
                // a lowered `for`: the body (and `continue`) fall into the step
                int stepStart = nextInstr();
                backpatch(bodyNext, stepStart);
                backpatch(ctx.continues, stepStart);
                List stepNext = genStmt(n.kids[2]);
                backpatch(stepNext, nextInstr());
            } else {
                backpatch(bodyNext, loopStart);
                backpatch(ctx.continues, loopStart);
            }
            curLine_ = n.line;
            Quad back{TacOp::Goto, {}, {}, {}};
            back.target = loopStart;
            emit(back);
            // leaving the loop: condition false, or a `break`
            return merge(c.falselist, ctx.breaks);
        }

        case NodeKind::ArrAssign: {
            Operand index = genValue(n.kids[0]);
            Operand value = genValue(n.kids[1]);
            curLine_ = n.line;
            emit(Quad{TacOp::ArrStore, value, Operand::var(n.resolved), index});
            return {};
        }

        case NodeKind::Call:
            genCall(n, false);
            return {};

        case NodeKind::Return: {
            Quad ret{TacOp::Return, {}, {}, {}};
            if (!n.kids.empty()) ret.arg1 = genValue(n.kids[0]);
            curLine_ = n.line;
            emit(ret);
            return {}; // control never falls through a return
        }

        case NodeKind::Print: {
            Operand value = genValue(n.kids[0]);
            curLine_ = n.line;
            emit(Quad{TacOp::Print, {}, value, {}});
            return {};
        }

        case NodeKind::Break: {
            int j = emit(Quad{TacOp::Goto, {}, {}, {}});
            loops_.back().breaks.push_back(j);
            return {};
        }

        case NodeKind::Continue: {
            int j = emit(Quad{TacOp::Goto, {}, {}, {}});
            loops_.back().continues.push_back(j);
            return {};
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

        case NodeKind::BoolToInt: // comparison used as a number: 1 if true, else 0
            return materializeBool(n.kids[0]);

        case NodeKind::Index: {
            Operand index = genValue(n.kids[0]);
            Operand t = newTemp(n.type);
            curLine_ = n.line;
            emit(Quad{TacOp::ArrLoad, t, Operand::var(n.resolved), index});
            return t;
        }

        case NodeKind::Call:
            return genCall(n, true);

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

// Arguments are evaluated first (left to right, nested calls complete before
// this call's own params are emitted), then pushed by a run of `param` quads
// immediately before the `call`.
Operand TacGenerator::genCall(const Node& n, bool wantValue) {
    std::vector<Operand> args;
    for (const NodePtr& arg : n.kids) args.push_back(genValue(arg));
    curLine_ = n.line;
    for (const Operand& a : args) emit(Quad{TacOp::Param, {}, a, {}});

    Type ret = funcRet_.at(n.text);
    Operand result;
    if (wantValue && ret != Type::Void) result = newTemp(ret);
    Quad call{TacOp::Call, result, {}, {}};
    call.func = n.text;
    call.argc = static_cast<int>(args.size());
    emit(call);
    return result;
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
