#ifndef CASCADE_TAC_H
#define CASCADE_TAC_H

#include <map>
#include <ostream>
#include <string>
#include <vector>
#include "ast.h"
#include "semantic.h"

// Three-address code, stored as quadruples (op, arg1, arg2, result).
// Instruction numbers are vector indices, which is what backpatching
// writes into the `target` field of jump instructions.

struct Operand {
    enum class Kind { None, Var, Temp, IntConst, FloatConst };
    Kind kind = Kind::None;
    std::string name;  // Var / Temp
    long long ival = 0;
    double fval = 0.0;

    static Operand var(const std::string& n) { Operand o; o.kind = Kind::Var; o.name = n; return o; }
    static Operand temp(const std::string& n) { Operand o; o.kind = Kind::Temp; o.name = n; return o; }
    static Operand intConst(long long v) { Operand o; o.kind = Kind::IntConst; o.ival = v; return o; }
    static Operand floatConst(double v) { Operand o; o.kind = Kind::FloatConst; o.fval = v; return o; }
    bool isSlot() const { return kind == Kind::Var || kind == Kind::Temp; }
};
std::string operandText(const Operand& o);

enum class TacOp {
    Copy,    // result = arg1
    Add, Sub, Mul, Div, Mod, // result = arg1 op arg2
    Neg,     // result = - arg1
    IntToFloat, // result = (float) arg1
    IfGoto,  // if arg1 relop arg2 goto target
    Goto,    // goto target
    Param,   // push arg1 as the next argument of the upcoming call
    Call,    // [result =] call func, argc        (result absent: value discarded)
    Return,  // return [arg1]
    ArrLoad, // result = arg1[arg2]               (arg1 = array variable)
    ArrStore,// arg1[arg2] = result               (result holds the VALUE being stored)
    Alloc,   // result = new array of arg1 elements (isFloat: float elements)
    Print,   // print arg1
    Halt
};

struct Quad {
    TacOp op;
    Operand result, arg1, arg2;
    TokenType relop = TokenType::END_OF_FILE; // IfGoto only
    bool isFloat = false; // arithmetic / comparison done in floating point
    int target = -1;      // IfGoto / Goto; -1 = not yet backpatched
    std::string func = ""; // Call: callee name
    int argc = 0;         // Call: number of Param quads that precede it
    int line = 0;         // source line, for runtime error messages
};

// One function's slice of the program. Its code is a contiguous run of
// quads starting at `entry`; its variables and temporaries are frame-local
// (slots: parameters first, then everything else), so recursion works.
struct TacFunc {
    std::string name;
    Type ret = Type::Void;
    int entry = 0;
    int nParams = 0;
    std::vector<std::pair<std::string, Type>> slots;
};

struct TacProgram {
    std::vector<Quad> code;
    // Storage slots of the top-level code (user variables then temporaries)
    // with their types, in first-use order — the VM allocates from this.
    // Function-local storage lives in `funcs[i].slots` instead.
    std::vector<std::pair<std::string, Type>> slots;
    std::vector<std::string> userVars; // top-level variables, declaration order
    std::vector<TacFunc> funcs;
    // array variable -> element type. The variable's slot itself holds an
    // int (the array's address); this says what the elements are.
    std::map<std::string, Type> arrayElem;

    void print(std::ostream& out) const;
};

// Syntax-directed translation of the annotated AST into TAC, using the
// textbook backpatching scheme:
//   boolean expressions -> truelist / falselist of unfilled jumps
//   statements          -> nextlist of jumps to whatever follows
// Jump targets are filled in (backpatched) only once the instruction they
// should point to has actually been emitted — no labels are generated up
// front and there is no second fix-up pass over the program.
class TacGenerator {
public:
    TacProgram generate(const NodePtr& program, const std::vector<SymbolEntry>& symbols);

private:
    using List = std::vector<int>;
    struct BoolLists { List truelist, falselist; };

    struct LoopCtx { List breaks, continues; };

    TacProgram prog_;
    std::map<std::string, Type> slotType_;
    std::map<std::string, Type> funcRet_;
    std::vector<LoopCtx> loops_;
    int curFunc_ = -1;   // index into prog_.funcs while generating a function body
    int tempCount_ = 0;
    int curLine_ = 0;

    int nextInstr() const { return static_cast<int>(prog_.code.size()); }
    int emit(Quad q);
    Operand newTemp(Type t);
    void addSlot(const std::string& name, Type t);                 // to the current function
    void addSlotTo(int func, const std::string& name, Type t);     // -1 = top-level code

    static List makeList(int i) { return List{i}; }
    static List merge(const List& a, const List& b);
    void backpatch(const List& list, int target);

    List genStmt(const NodePtr& node);
    List genStmts(const std::vector<NodePtr>& stmts);
    Operand genValue(const NodePtr& node);       // numeric expression -> operand
    Operand genCall(const Node& call, bool wantValue);
    BoolLists genCond(const NodePtr& node);       // boolean expression -> jump lists
    Operand materializeBool(const NodePtr& node); // boolean -> 0/1 int temp
};

#endif // CASCADE_TAC_H
