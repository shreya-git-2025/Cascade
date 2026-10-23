#ifndef CASCADE_SEMANTIC_H
#define CASCADE_SEMANTIC_H

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "ast.h"

struct SemanticDiag {
    std::string message;
    int line;
    int column;
};

// One declared variable. `unique` is the scope-resolved name used by every
// later stage: the first declaration of x keeps the name "x", a shadowing
// declaration in an inner block becomes "x.1", "x.2", ... ('.' can't
// appear in a source identifier, so these never collide with user names).
struct SymbolEntry {
    std::string name;
    std::string unique;
    Type type;
    int scopeDepth; // 0 = global, +1 per nested block
    int line;
    int column;
    bool assigned;  // has an initializer or assignment been seen yet?
    bool warned = false; // uninitialized-read warning already issued
    bool isArray = false;      // declared with [] (type is then the ELEMENT type)
    long long arraySize = 0;   // element count (0 for array parameters: size unknown)
    std::string owner;         // enclosing function, "" for top-level code
};

// A function's signature, collected in a pre-pass over the program so a
// call may appear before (or inside) the definition it refers to.
struct FuncSig {
    std::string name;
    Type ret = Type::Void;
    std::vector<Type> paramTypes;     // element type for array parameters
    std::vector<bool> paramIsArray;
    int line = 0;
    int column = 0;
    bool defined = false; // false while only a prototype has been seen
};

// Walks the AST with a stack of scopes (pushed on entering a block, popped
// on leaving it) and:
//   - resolves every variable use to its declaration (undeclared -> error)
//   - rejects redeclaration in the same scope (shadowing an outer one is ok)
//   - computes and annotates the type of every expression node
//   - inserts IntToFloat nodes wherever an int is implicitly widened
// Errors don't stop the walk: an ill-typed subexpression gets Type::Error,
// and anything built on Type::Error is silently accepted, so one mistake
// produces one message rather than a cascade of follow-on errors.
class SemanticAnalyzer {
public:
    bool analyze(const NodePtr& program);

    const std::vector<SemanticDiag>& errors() const { return errors_; }
    const std::vector<SemanticDiag>& warnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }

    // Every variable ever declared, in declaration order.
    const std::vector<SymbolEntry>& symbols() const { return entries_; }
    const std::unordered_map<std::string, FuncSig>& functions() const { return funcs_; }
    void printSymbolTable(std::ostream& out) const;

private:
    std::vector<SymbolEntry> entries_;
    std::vector<std::unordered_map<std::string, int>> scopes_; // name -> entries_ index
    std::unordered_map<std::string, int> declCount_;           // for unique-name suffixes
    std::vector<SemanticDiag> errors_;
    std::vector<SemanticDiag> warnings_;

    std::unordered_map<std::string, FuncSig> funcs_;
    std::string curFunc_;          // "" while analysing top-level code
    Type curRet_ = Type::Void;
    bool inFunc_ = false;
    int loopDepth_ = 0;            // > 0 inside a while/for body (for break/continue)

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }
    SymbolEntry* lookup(const std::string& name);

    void error(const Node& at, const std::string& msg);
    void warning(const Node& at, const std::string& msg);

    void declareFunction(const Node& def);
    void visitFuncDef(const NodePtr& def);
    Type visitCall(Node& call);
    void checkIndex(NodePtr& index);
    void visitStmt(const NodePtr& node);
    void visitStmts(const std::vector<NodePtr>& stmts);
    Type visitExpr(NodePtr& node);
    void checkCondition(NodePtr& cond, const std::string& construct);
    void coerceToFloat(NodePtr& node);
    void toBool(NodePtr& node);       // numeric value used as a condition -> (value != 0)
    void boolToInt(NodePtr& node);    // comparison result used as a number -> 0 / 1
    bool checkStore(const Node& at, Type target, NodePtr& value, const std::string& what);
};

#endif // CASCADE_SEMANTIC_H
