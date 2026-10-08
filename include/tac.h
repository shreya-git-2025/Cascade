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
    Halt
};

struct Quad {
    TacOp op;
    Operand result, arg1, arg2;
    TokenType relop = TokenType::END_OF_FILE; // IfGoto only
    bool isFloat = false; // arithmetic / comparison done in floating point
    int target = -1;      // IfGoto / Goto; -1 = not yet backpatched
    int line = 0;         // source line, for runtime error messages
};

struct TacProgram {
    std::vector<Quad> code;
    // Every storage slot the program uses (user variables then temporaries)
    // with its type, in first-use order — the VM allocates from this.
    std::vector<std::pair<std::string, Type>> slots;
    std::vector<std::string> userVars; // resolved names, declaration order

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

    TacProgram prog_;
    std::map<std::string, Type> slotType_;
    int tempCount_ = 0;
    int curLine_ = 0;

    int nextInstr() const { return static_cast<int>(prog_.code.size()); }
    int emit(Quad q);
    Operand newTemp(Type t);
    void addSlot(const std::string& name, Type t);

    static List makeList(int i) { return List{i}; }
    static List merge(const List& a, const List& b);
    void backpatch(const List& list, int target);

    List genStmt(const NodePtr& node);
    List genStmts(const std::vector<NodePtr>& stmts);
    Operand genValue(const NodePtr& node);       // numeric expression -> operand
    BoolLists genCond(const NodePtr& node);       // boolean expression -> jump lists
    Operand materializeBool(const NodePtr& node); // boolean -> 0/1 int temp
};

#endif // CASCADE_TAC_H
