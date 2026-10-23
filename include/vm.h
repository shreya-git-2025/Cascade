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
    LOADL, STOREL,        // frame-local slot <-> stack (inside functions)
    CALL,                 // call function #arg: args are on the stack, popped into the new frame
    RET,                  // arg=1: pop the return value, pop the frame, push the value for the caller
    POP,                  // discard the top of the stack (unused call result)
    ALLOC,                // new array of arg elements (ival=1: float); push its address
    ALOAD,                // pop index, pop array address; push element (bounds-checked)
    ASTORE,               // pop value, pop index, pop array address; store (bounds-checked)
    PRINT,                // pop and print one value
    HALT
};

struct Instr {
    OpCode op;
    int arg = 0;          // slot index or jump target
    long long ival = 0;   // PUSH_I
    double fval = 0.0;    // PUSH_F
    int line = 0;         // source line, for runtime errors
    int func = -1;        // function this instruction belongs to (-1 = top-level code)
};

struct Value {
    bool isFloat = false;
    long long i = 0;
    double f = 0.0;
};
std::string valueText(const Value& v);

// Per-function metadata: where its code starts and what its frame holds.
struct FuncMeta {
    std::string name;
    Type ret = Type::Void;
    int entry = 0;       // bytecode address of the first instruction
    int nParams = 0;     // parameters occupy local slots 0 .. nParams-1
    std::vector<std::string> slotNames;
    std::vector<Type> slotTypes;
};

struct Bytecode {
    std::vector<Instr> code;
    std::vector<std::string> slotNames; // top-level (global) slots
    std::vector<Type> slotTypes;
    std::vector<int> userSlots; // global slots to report as program output
    std::vector<Type> userElem; // parallel to userSlots: element type if an array, else Unknown
    std::vector<FuncMeta> funcs;
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
    // `out` receives the program's own output (print statements); default std::cout.
    explicit VirtualMachine(const Bytecode& bc, long long maxSteps = 10000000,
                            std::ostream* out = nullptr);

    // Runs to HALT. Returns false on a runtime error (see error()).
    bool run(std::ostream* trace = nullptr);

    const RuntimeError& error() const { return error_; }
    long long steps() const { return steps_; }
    void printState(std::ostream& out) const; // final values of top-level variables

private:
    const Bytecode& bc_;
    long long maxSteps_;
    long long steps_ = 0;
    std::ostream* out_;
    std::vector<Value> slots_;   // top-level (global) variables
    std::vector<Value> stack_;   // operand stack
    // One frame per active call. A frame's locals hold the function's
    // parameters, variables and temporaries, so every recursive call gets
    // its own copies.
    struct Frame {
        int retPc;
        std::vector<Value> locals;
    };
    std::vector<Frame> frames_;
    // All arrays live here. An array is a header cell (its length) followed
    // by its elements; the "address" stored in a variable is the header's
    // index. Cell 0 is a dummy so that address 0 means "no array yet".
    std::vector<Value> heap_;
    static constexpr size_t kMaxDepth = 10000;
    static constexpr size_t kMaxHeap = 10000000;
    RuntimeError error_{"", 0};

    Value pop();
};

#endif // CASCADE_VM_H
