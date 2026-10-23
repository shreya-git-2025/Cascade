#include "../include/vm.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>

namespace {
const char* opName(OpCode op) {
    switch (op) {
        case OpCode::PUSH_I: return "PUSH_I"; case OpCode::PUSH_F: return "PUSH_F";
        case OpCode::LOAD:   return "LOAD";   case OpCode::STORE:  return "STORE";
        case OpCode::ADD_I:  return "ADD_I";  case OpCode::SUB_I:  return "SUB_I";
        case OpCode::MUL_I:  return "MUL_I";  case OpCode::DIV_I:  return "DIV_I";
        case OpCode::MOD_I:  return "MOD_I";  case OpCode::NEG_I:  return "NEG_I";
        case OpCode::ADD_F:  return "ADD_F";  case OpCode::SUB_F:  return "SUB_F";
        case OpCode::MUL_F:  return "MUL_F";  case OpCode::DIV_F:  return "DIV_F";
        case OpCode::NEG_F:  return "NEG_F";  case OpCode::I2F:    return "I2F";
        case OpCode::LT_I:   return "LT_I";   case OpCode::LE_I:   return "LE_I";
        case OpCode::GT_I:   return "GT_I";   case OpCode::GE_I:   return "GE_I";
        case OpCode::EQ_I:   return "EQ_I";   case OpCode::NE_I:   return "NE_I";
        case OpCode::LT_F:   return "LT_F";   case OpCode::LE_F:   return "LE_F";
        case OpCode::GT_F:   return "GT_F";   case OpCode::GE_F:   return "GE_F";
        case OpCode::EQ_F:   return "EQ_F";   case OpCode::NE_F:   return "NE_F";
        case OpCode::JMP:    return "JMP";    case OpCode::JNZ:    return "JNZ";
        case OpCode::LOADL:  return "LOADL";  case OpCode::STOREL: return "STOREL";
        case OpCode::CALL:   return "CALL";   case OpCode::RET:    return "RET";
        case OpCode::POP:    return "POP";    case OpCode::ALLOC:  return "ALLOC";
        case OpCode::ALOAD:  return "ALOAD";  case OpCode::ASTORE: return "ASTORE";
        case OpCode::PRINT:  return "PRINT";
        case OpCode::HALT:   return "HALT";
    }
    return "?";
}

OpCode compareOp(TokenType relop, bool isFloat) {
    int base;
    switch (relop) {
        case TokenType::LT: base = 0; break;
        case TokenType::LE: base = 1; break;
        case TokenType::GT: base = 2; break;
        case TokenType::GE: base = 3; break;
        case TokenType::EQ: base = 4; break;
        default:            base = 5; break; // NE
    }
    int first = static_cast<int>(isFloat ? OpCode::LT_F : OpCode::LT_I);
    return static_cast<OpCode>(first + base);
}

OpCode arithOpCode(TacOp op, bool isFloat) {
    switch (op) {
        case TacOp::Add: return isFloat ? OpCode::ADD_F : OpCode::ADD_I;
        case TacOp::Sub: return isFloat ? OpCode::SUB_F : OpCode::SUB_I;
        case TacOp::Mul: return isFloat ? OpCode::MUL_F : OpCode::MUL_I;
        case TacOp::Div: return isFloat ? OpCode::DIV_F : OpCode::DIV_I;
        default:         return OpCode::MOD_I;
    }
}

std::string instrText(const Instr& in, const Bytecode& bc) {
    std::ostringstream ss;
    ss << std::left << std::setw(7) << opName(in.op);
    switch (in.op) {
        case OpCode::PUSH_I: ss << in.ival; break;
        case OpCode::PUSH_F: ss << valueText(Value{true, 0, in.fval}); break;
        case OpCode::LOAD:
        case OpCode::STORE:  ss << bc.slotNames[in.arg]; break;
        case OpCode::LOADL:
        case OpCode::STOREL: ss << bc.funcs[in.func].slotNames[in.arg]; break;
        case OpCode::CALL:   ss << bc.funcs[in.arg].name; break;
        case OpCode::RET:    ss << (in.arg ? "(value)" : "(void)"); break;
        case OpCode::ALLOC:  ss << in.arg << (in.ival ? " floats" : " ints"); break;
        case OpCode::JMP:
        case OpCode::JNZ:    ss << "-> " << in.arg; break;
        default: break;
    }
    return ss.str();
}

// Two's-complement wrap-around for int arithmetic, without signed-overflow UB.
long long wrap(unsigned long long v) { return static_cast<long long>(v); }
} // namespace

std::string valueText(const Value& v) {
    if (!v.isFloat) return std::to_string(v.i);
    std::ostringstream ss;
    ss << std::setprecision(15) << v.f;
    std::string s = ss.str();
    if (s.find_first_of(".eEni") == std::string::npos) s += ".0";
    return s;
}

// ---------------------------------------------------------------------------
// TAC -> bytecode
// ---------------------------------------------------------------------------

Bytecode assemble(const TacProgram& tac) {
    Bytecode bc;
    std::map<std::string, int> globalIndex;
    for (const auto& [name, type] : tac.slots) {
        globalIndex[name] = static_cast<int>(bc.slotNames.size());
        bc.slotNames.push_back(name);
        bc.slotTypes.push_back(type);
    }
    for (const std::string& v : tac.userVars) {
        bc.userSlots.push_back(globalIndex.at(v));
        auto arr = tac.arrayElem.find(v);
        bc.userElem.push_back(arr == tac.arrayElem.end() ? Type::Unknown : arr->second);
    }

    // Function frames: each function numbers its own slots from 0 (params first).
    std::vector<std::map<std::string, int>> localIndex(tac.funcs.size());
    std::map<std::string, int> funcIndex;
    for (size_t i = 0; i < tac.funcs.size(); ++i) {
        const TacFunc& f = tac.funcs[i];
        FuncMeta m;
        m.name = f.name;
        m.ret = f.ret;
        m.nParams = f.nParams;
        for (const auto& [name, type] : f.slots) {
            localIndex[i][name] = static_cast<int>(m.slotNames.size());
            m.slotNames.push_back(name);
            m.slotTypes.push_back(type);
        }
        funcIndex[f.name] = static_cast<int>(i);
        bc.funcs.push_back(m);
    }

    // Function bodies are contiguous, in order, after the top-level code.
    std::vector<int> quadFunc(tac.code.size(), -1);
    for (size_t i = 0; i < tac.funcs.size(); ++i) {
        size_t end = i + 1 < tac.funcs.size() ? static_cast<size_t>(tac.funcs[i + 1].entry)
                                              : tac.code.size();
        for (size_t q = tac.funcs[i].entry; q < end; ++q) quadFunc[q] = static_cast<int>(i);
    }

    std::vector<int> quadStart(tac.code.size() + 1, 0);
    std::vector<size_t> jumpsToFix; // bytecode indices whose arg is still a quad number

    for (size_t qi = 0; qi < tac.code.size(); ++qi) {
        const Quad& q = tac.code[qi];
        const int fi = quadFunc[qi];
        quadStart[qi] = static_cast<int>(bc.code.size());
        auto put = [&](Instr in) { in.line = q.line; in.func = fi; bc.code.push_back(in); };
        // inside a function, a name is a frame-local if the function owns it,
        // otherwise it is a top-level (global) variable
        auto isLocal = [&](const std::string& name) {
            return fi >= 0 && localIndex[fi].count(name) > 0;
        };
        auto push = [&](const Operand& o) {
            switch (o.kind) {
                case Operand::Kind::IntConst:   { Instr in{OpCode::PUSH_I}; in.ival = o.ival; put(in); break; }
                case Operand::Kind::FloatConst: { Instr in{OpCode::PUSH_F}; in.fval = o.fval; put(in); break; }
                default:
                    if (isLocal(o.name)) put(Instr{OpCode::LOADL, localIndex[fi].at(o.name)});
                    else put(Instr{OpCode::LOAD, globalIndex.at(o.name)});
                    break;
            }
        };
        auto store = [&](const Operand& o) {
            if (isLocal(o.name)) put(Instr{OpCode::STOREL, localIndex[fi].at(o.name)});
            else put(Instr{OpCode::STORE, globalIndex.at(o.name)});
        };

        switch (q.op) {
            case TacOp::Copy:                       // x = a      ->  push a; STORE x
                push(q.arg1); store(q.result); break;
            case TacOp::Add: case TacOp::Sub: case TacOp::Mul: case TacOp::Div: case TacOp::Mod:
                push(q.arg1); push(q.arg2);         // t = a op b ->  push a; push b; OP; STORE t
                put(Instr{arithOpCode(q.op, q.isFloat)});
                store(q.result); break;
            case TacOp::Neg:
                push(q.arg1); put(Instr{q.isFloat ? OpCode::NEG_F : OpCode::NEG_I}); store(q.result); break;
            case TacOp::IntToFloat:
                push(q.arg1); put(Instr{OpCode::I2F}); store(q.result); break;
            case TacOp::IfGoto:                     // if a < b goto L -> push a; push b; LT; JNZ L
                push(q.arg1); push(q.arg2);
                put(Instr{compareOp(q.relop, q.isFloat)});
                jumpsToFix.push_back(bc.code.size());
                put(Instr{OpCode::JNZ, q.target}); break;
            case TacOp::Goto:
                jumpsToFix.push_back(bc.code.size());
                put(Instr{OpCode::JMP, q.target}); break;
            case TacOp::Param:                      // param a   ->  push a
                push(q.arg1); break;
            case TacOp::Call: {                     // [t =] call f, n  ->  CALL f; STORE t | POP
                int f = funcIndex.at(q.func);
                put(Instr{OpCode::CALL, f});
                if (q.result.kind != Operand::Kind::None) store(q.result);
                else if (tac.funcs[f].ret != Type::Void) put(Instr{OpCode::POP});
                break;
            }
            case TacOp::Return:
                if (q.arg1.kind != Operand::Kind::None) { push(q.arg1); put(Instr{OpCode::RET, 1}); }
                else put(Instr{OpCode::RET, 0});
                break;
            case TacOp::ArrLoad:                    // t = a[i]  ->  push a; push i; ALOAD; STORE t
                push(q.arg1); push(q.arg2); put(Instr{OpCode::ALOAD}); store(q.result); break;
            case TacOp::ArrStore:                   // a[i] = v  ->  push a; push i; push v; ASTORE
                push(q.arg1); push(q.arg2); push(q.result); put(Instr{OpCode::ASTORE}); break;
            case TacOp::Alloc: {                    // a = alloc n  ->  ALLOC n; STORE a
                Instr in{OpCode::ALLOC, static_cast<int>(q.arg1.ival)};
                in.ival = q.isFloat ? 1 : 0;
                put(in);
                store(q.result);
                break;
            }
            case TacOp::Print:
                push(q.arg1); put(Instr{OpCode::PRINT}); break;
            case TacOp::Halt:
                put(Instr{OpCode::HALT}); break;
        }
    }
    quadStart[tac.code.size()] = static_cast<int>(bc.code.size());
    for (size_t j : jumpsToFix) bc.code[j].arg = quadStart[bc.code[j].arg];
    for (size_t i = 0; i < tac.funcs.size(); ++i) bc.funcs[i].entry = quadStart[tac.funcs[i].entry];
    return bc;
}

void Bytecode::print(std::ostream& out) const {
    out << "slots:";
    for (size_t i = 0; i < slotNames.size(); ++i) {
        out << " [" << i << "]" << slotNames[i] << ":" << typeName(slotTypes[i]);
    }
    out << "\n";
    for (const FuncMeta& f : funcs) {
        out << "function " << f.name << ": entry " << f.entry << ", " << f.nParams
            << " param(s), frame:";
        for (size_t i = 0; i < f.slotNames.size(); ++i) {
            out << " [" << i << "]" << f.slotNames[i] << ":" << typeName(f.slotTypes[i]);
        }
        out << "\n";
    }
    for (size_t i = 0; i < code.size(); ++i) {
        out << std::right << std::setw(4) << i << ":  " << instrText(code[i], *this) << "\n";
    }
}

// ---------------------------------------------------------------------------
// execution
// ---------------------------------------------------------------------------

VirtualMachine::VirtualMachine(const Bytecode& bc, long long maxSteps, std::ostream* out)
    : bc_(bc), maxSteps_(maxSteps), out_(out ? out : &std::cout) {}

Value VirtualMachine::pop() {
    Value v = stack_.back();
    stack_.pop_back();
    return v;
}

bool VirtualMachine::run(std::ostream* trace) {
    slots_.assign(bc_.slotNames.size(), Value{});
    for (size_t i = 0; i < slots_.size(); ++i) slots_[i].isFloat = bc_.slotTypes[i] == Type::Float;
    stack_.clear();
    frames_.clear();
    heap_.assign(1, Value{}); // cell 0: the "no array" dummy
    steps_ = 0;

    auto fail = [&](const std::string& msg, int line) {
        error_ = RuntimeError{msg, line};
        return false;
    };

    if (trace) *trace << std::left << std::setw(7) << "STEP" << std::setw(6) << "PC"
                      << std::setw(18) << "INSTR" << "STACK (after)\n";

    int pc = 0;
    for (;;) {
        if (pc < 0 || pc >= static_cast<int>(bc_.code.size())) return fail("pc out of range", 0);
        if (++steps_ > maxSteps_) {
            return fail("step limit of " + std::to_string(maxSteps_) +
                        " exceeded (possible infinite loop)", bc_.code[pc].line);
        }
        const Instr& in = bc_.code[pc];
        int here = pc;
        ++pc;

        switch (in.op) {
            case OpCode::PUSH_I: stack_.push_back(Value{false, in.ival, 0}); break;
            case OpCode::PUSH_F: stack_.push_back(Value{true, 0, in.fval}); break;
            case OpCode::LOAD:   stack_.push_back(slots_[in.arg]); break;
            case OpCode::STORE:  slots_[in.arg] = pop(); break;

            case OpCode::ADD_I: { Value b = pop(), a = pop();
                stack_.push_back(Value{false, wrap(static_cast<unsigned long long>(a.i) + b.i), 0}); break; }
            case OpCode::SUB_I: { Value b = pop(), a = pop();
                stack_.push_back(Value{false, wrap(static_cast<unsigned long long>(a.i) - b.i), 0}); break; }
            case OpCode::MUL_I: { Value b = pop(), a = pop();
                stack_.push_back(Value{false, wrap(static_cast<unsigned long long>(a.i) * b.i), 0}); break; }
            case OpCode::DIV_I:
            case OpCode::MOD_I: { Value b = pop(), a = pop();
                if (b.i == 0) return fail(in.op == OpCode::DIV_I ? "integer division by zero"
                                                                 : "integer modulo by zero", in.line);
                long long r;
                if (b.i == -1) r = in.op == OpCode::DIV_I ? wrap(0ULL - static_cast<unsigned long long>(a.i)) : 0;
                else r = in.op == OpCode::DIV_I ? a.i / b.i : a.i % b.i;
                stack_.push_back(Value{false, r, 0}); break; }
            case OpCode::NEG_I: { Value a = pop();
                stack_.push_back(Value{false, wrap(0ULL - static_cast<unsigned long long>(a.i)), 0}); break; }

            case OpCode::ADD_F: { Value b = pop(), a = pop(); stack_.push_back(Value{true, 0, a.f + b.f}); break; }
            case OpCode::SUB_F: { Value b = pop(), a = pop(); stack_.push_back(Value{true, 0, a.f - b.f}); break; }
            case OpCode::MUL_F: { Value b = pop(), a = pop(); stack_.push_back(Value{true, 0, a.f * b.f}); break; }
            case OpCode::DIV_F: { Value b = pop(), a = pop();
                if (b.f == 0.0) return fail("floating-point division by zero", in.line);
                stack_.push_back(Value{true, 0, a.f / b.f}); break; }
            case OpCode::NEG_F: { Value a = pop(); stack_.push_back(Value{true, 0, -a.f}); break; }
            case OpCode::I2F:   { Value a = pop(); stack_.push_back(Value{true, 0, static_cast<double>(a.i)}); break; }

            case OpCode::LT_I: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.i <  b.i, 0}); break; }
            case OpCode::LE_I: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.i <= b.i, 0}); break; }
            case OpCode::GT_I: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.i >  b.i, 0}); break; }
            case OpCode::GE_I: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.i >= b.i, 0}); break; }
            case OpCode::EQ_I: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.i == b.i, 0}); break; }
            case OpCode::NE_I: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.i != b.i, 0}); break; }
            case OpCode::LT_F: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.f <  b.f, 0}); break; }
            case OpCode::LE_F: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.f <= b.f, 0}); break; }
            case OpCode::GT_F: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.f >  b.f, 0}); break; }
            case OpCode::GE_F: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.f >= b.f, 0}); break; }
            case OpCode::EQ_F: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.f == b.f, 0}); break; }
            case OpCode::NE_F: { Value b = pop(), a = pop(); stack_.push_back(Value{false, a.f != b.f, 0}); break; }

            case OpCode::JMP: pc = in.arg; break;
            case OpCode::JNZ: if (pop().i != 0) pc = in.arg; break;

            case OpCode::LOADL:  stack_.push_back(frames_.back().locals[in.arg]); break;
            case OpCode::STOREL: frames_.back().locals[in.arg] = pop(); break;
            case OpCode::POP:    pop(); break;
            case OpCode::PRINT:  *out_ << valueText(pop()) << "\n"; break;

            case OpCode::CALL: {
                const FuncMeta& f = bc_.funcs[in.arg];
                if (frames_.size() >= kMaxDepth) {
                    return fail("stack overflow: call depth of " + std::to_string(kMaxDepth) +
                                " exceeded in '" + f.name + "' (runaway recursion?)", in.line);
                }
                Frame fr;
                fr.retPc = pc;
                fr.locals.assign(f.slotTypes.size(), Value{});
                for (size_t i = 0; i < f.slotTypes.size(); ++i) fr.locals[i].isFloat = f.slotTypes[i] == Type::Float;
                for (int i = f.nParams - 1; i >= 0; --i) fr.locals[i] = pop(); // last arg is on top
                frames_.push_back(std::move(fr));
                pc = f.entry;
                break;
            }
            case OpCode::RET: {
                Value rv;
                if (in.arg) rv = pop();
                pc = frames_.back().retPc;
                frames_.pop_back();
                if (in.arg) stack_.push_back(rv);
                break;
            }

            case OpCode::ALLOC: {
                if (heap_.size() + static_cast<size_t>(in.arg) + 1 > kMaxHeap) {
                    return fail("out of memory allocating an array of " + std::to_string(in.arg) +
                                " elements", in.line);
                }
                long long base = static_cast<long long>(heap_.size());
                heap_.push_back(Value{false, in.arg, 0.0}); // header: the length
                Value zero;
                zero.isFloat = in.ival != 0;
                for (int i = 0; i < in.arg; ++i) heap_.push_back(zero);
                stack_.push_back(Value{false, base, 0.0});
                break;
            }
            case OpCode::ALOAD:
            case OpCode::ASTORE: {
                Value v;
                if (in.op == OpCode::ASTORE) v = pop();
                Value idx = pop(), base = pop();
                if (base.i <= 0 || base.i >= static_cast<long long>(heap_.size())) {
                    return fail("use of an array that has not been created yet", in.line);
                }
                long long len = heap_[base.i].i;
                if (idx.i < 0 || idx.i >= len) {
                    return fail("array index " + std::to_string(idx.i) + " is out of bounds (array has " +
                                std::to_string(len) + " element" + (len == 1 ? "" : "s") + ")", in.line);
                }
                if (in.op == OpCode::ALOAD) stack_.push_back(heap_[base.i + 1 + idx.i]);
                else heap_[base.i + 1 + idx.i] = v;
                break;
            }
            case OpCode::HALT:
                if (trace) *trace << std::setw(7) << steps_ << std::setw(6) << here << "HALT\n";
                return true;
        }

        if (trace) {
            *trace << std::left << std::setw(7) << steps_ << std::setw(6) << here
                   << std::setw(18) << instrText(in, bc_) << "[";
            for (size_t i = 0; i < stack_.size(); ++i) *trace << (i ? " " : "") << valueText(stack_[i]);
            *trace << "]\n";
        }
    }
}

void VirtualMachine::printState(std::ostream& out) const {
    for (size_t k = 0; k < bc_.userSlots.size(); ++k) {
        int s = bc_.userSlots[k];
        Type elem = bc_.userElem[k];
        out << "  " << std::left << std::setw(10) << bc_.slotNames[s];
        if (elem == Type::Unknown) {
            out << std::setw(7) << typeName(bc_.slotTypes[s]) << "= " << valueText(slots_[s]) << "\n";
            continue;
        }
        long long base = slots_[s].i;
        if (base <= 0 || base >= static_cast<long long>(heap_.size())) {
            out << std::setw(7) << (typeName(elem) + "[]") << "= (not created)\n";
            continue;
        }
        long long len = heap_[base].i;
        out << std::setw(7) << (typeName(elem) + "[" + std::to_string(len) + "]") << "= [";
        for (long long i = 0; i < len; ++i) out << (i ? ", " : "") << valueText(heap_[base + 1 + i]);
        out << "]\n";
    }
}
