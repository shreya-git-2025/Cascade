#ifndef CASCADE_SLR_H
#define CASCADE_SLR_H

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#include "grammar.h"

// SLR(1) table construction, done from first principles over grammar():
//   1. nullable / FIRST / FOLLOW sets by fixed-point iteration
//   2. the canonical collection of LR(0) item sets (CLOSURE + GOTO)
//   3. ACTION/GOTO tables: shift on terminal transitions, reduce
//      A -> alpha . on every terminal in FOLLOW(A), accept on S' -> program .
// Any shift/reduce or reduce/reduce conflict is recorded rather than
// silently resolved, so a grammar edit that breaks SLR-ness is caught
// immediately (cascade --tables prints them).

struct Item {
    int prod; // index into grammar()
    int dot;  // position of the dot within the RHS (0 .. rhs.size())
    bool operator<(const Item& o) const { return prod != o.prod ? prod < o.prod : dot < o.dot; }
    bool operator==(const Item& o) const { return prod == o.prod && dot == o.dot; }
};
using ItemSet = std::vector<Item>; // kept sorted, so sets compare by value

enum class ActKind { Error, Shift, Reduce, Accept };

struct TableEntry {
    ActKind kind = ActKind::Error;
    int target = -1; // Shift: next state; Reduce: production index
};

struct Conflict {
    int state;
    Symbol lookahead;
    std::string description;
};

class SlrTable {
public:
    static const SlrTable& instance(); // built once, on first use

    const TableEntry& action(int state, Symbol terminal) const { return action_[state][terminal]; }
    int gotoState(int state, Symbol nonterminal) const {
        return goto_[state][nonterminal - NUM_TERMINALS];
    }
    int numStates() const { return static_cast<int>(states_.size()); }
    const std::vector<Conflict>& conflicts() const { return conflicts_; }
    // The dangling-else shift/reduce conflict is resolved on purpose (shift:
    // an `else` binds to the nearest `if`, as in C). It is listed here, not in
    // conflicts(), so every OTHER conflict still blocks the compiler.
    const std::vector<Conflict>& resolvedConflicts() const { return resolved_; }

    // Terminals with a non-error ACTION in this state — the "expected" set
    // reported in syntax errors.
    std::vector<Symbol> expectedTerminals(int state) const;

    void printFirstFollow(std::ostream& out) const;
    void printItemSets(std::ostream& out) const;
    void printTable(std::ostream& out) const;

private:
    SlrTable();

    void computeFirst();
    void computeFollow();
    ItemSet closure(ItemSet items) const;
    ItemSet gotoSet(const ItemSet& items, Symbol x) const;
    void buildCollection();
    void buildTables();
    void setAction(int state, Symbol t, TableEntry e);

    std::vector<bool> nullable_;
    std::vector<std::set<Symbol>> first_;
    std::vector<std::set<Symbol>> follow_;

    std::vector<ItemSet> states_;
    std::vector<std::map<Symbol, int>> transitions_; // state -> (symbol -> state)

    std::vector<std::vector<TableEntry>> action_; // [state][terminal]
    std::vector<std::vector<int>> goto_;          // [state][nonterminal]
    std::vector<Conflict> conflicts_;
    std::vector<Conflict> resolved_;
};

#endif // CASCADE_SLR_H
