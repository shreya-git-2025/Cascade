#ifndef CASCADE_VM_H
#define CASCADE_VM_H

#include <ostream>
#include <string>
#include <vector>
#include "tac.h"

// Stack-machine instruction set. Arithmetic and comparisons are typed
// (_I / _F) because the type of every operation is already known
// statically — the VM never inspects a value to decide what to do.
enum class OpCode {
    PUSH_I, PUSH_F,       // push constant
    LOAD, STORE,          // slot <-> stack
    ADD_I, SUB_I, MUL_I, DIV_I, MOD_I, NEG_I,
    ADD_F, SUB_F, MUL_F, DIV_F, NEG_F,
    I2F,                  // int -> float on top of stack
    LT_I, LE_I, GT_I, GE_I, EQ_I, NE_I, // pop b, pop a, push (a op b) as int 0/1
    LT_F, LE_F, GT_F, GE_F, EQ_F, NE_F,
    JMP,                  // pc = arg
    JNZ,                  // pop; if != 0 then pc = arg
    HALT
};

struct Instr {
    OpCode op;
    int arg = 0;          // slot index or jump target
    long long ival = 0;   // PUSH_I
    double fval = 0.0;    // PUSH_F
    int line = 0;         // source line, for runtime errors
};

struct Value {
    bool isFloat = false;
    long long i = 0;
    double f = 0.0;
};
std::string valueText(const Value& v);

struct Bytecode {
    std::vector<Instr> code;
    std::vector<std::string> slotNames;
    std::vector<Type> slotTypes;
    std::vector<int> userSlots; // slots to report as program output
    void print(std::ostream& out) const;
};

// TAC -> bytecode. Each quad becomes a short load/compute/store sequence;
// quad-number jump targets are remapped to bytecode addresses afterwards.
Bytecode assemble(const TacProgram& tac);

struct RuntimeError {
    std::string message;
    int line;
};

class VirtualMachine {
public:
    explicit VirtualMachine(const Bytecode& bc, long long maxSteps = 10000000)
        : bc_(bc), maxSteps_(maxSteps) {}

    // Runs to HALT. Returns false on a runtime error (see error()).
    bool run(std::ostream* trace = nullptr);

    const RuntimeError& error() const { return error_; }
    long long steps() const { return steps_; }
    void printState(std::ostream& out) const; // final values of user variables

private:
    const Bytecode& bc_;
    long long maxSteps_;
    long long steps_ = 0;
    std::vector<Value> slots_;
    std::vector<Value> stack_;
    RuntimeError error_{"", 0};

    Value pop();
};

#endif // CASCADE_VM_H
