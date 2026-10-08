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
    void printSymbolTable(std::ostream& out) const;

private:
    std::vector<SymbolEntry> entries_;
    std::vector<std::unordered_map<std::string, int>> scopes_; // name -> entries_ index
    std::unordered_map<std::string, int> declCount_;           // for unique-name suffixes
    std::vector<SemanticDiag> errors_;
    std::vector<SemanticDiag> warnings_;

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }
    SymbolEntry* lookup(const std::string& name);

    void error(const Node& at, const std::string& msg);
    void warning(const Node& at, const std::string& msg);

    void visitStmt(const NodePtr& node);
    void visitStmts(const std::vector<NodePtr>& stmts);
    Type visitExpr(NodePtr& node);
    void checkCondition(NodePtr& cond, const std::string& construct);
    void coerceToFloat(NodePtr& node);
    bool checkStore(const Node& at, Type target, NodePtr& value, const std::string& what);
};

#endif // CASCADE_SEMANTIC_H
