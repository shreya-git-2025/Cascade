#include "../include/slr.h"

#include <algorithm>
#include <deque>
#include <iomanip>

const SlrTable& SlrTable::instance() {
    static const SlrTable table;
    return table;
}

SlrTable::SlrTable() {
    computeFirst();
    computeFollow();
    buildCollection();
    buildTables();
}

// ---------------------------------------------------------------------------
// 1. nullable / FIRST / FOLLOW
// ---------------------------------------------------------------------------

void SlrTable::computeFirst() {
    nullable_.assign(NUM_SYMBOLS, false);
    first_.assign(NUM_SYMBOLS, {});
    for (Symbol t = 0; t < NUM_TERMINALS; ++t) first_[t].insert(t);

    // Iterate to a fixed point: keep applying every production until no
    // FIRST set or nullable flag grows.
    bool changed = true;
    while (changed) {
        changed = false;
        for (const Production& p : grammar()) {
            bool allNullable = true;
            for (Symbol x : p.rhs) {
                size_t before = first_[p.lhs].size();
                first_[p.lhs].insert(first_[x].begin(), first_[x].end());
                if (first_[p.lhs].size() != before) changed = true;
                if (!nullable_[x]) { allNullable = false; break; }
            }
            if (allNullable && !nullable_[p.lhs]) {
                nullable_[p.lhs] = true;
                changed = true;
            }
        }
    }
}

void SlrTable::computeFollow() {
    follow_.assign(NUM_SYMBOLS, {});
    follow_[nonterm(NonTerminal::ProgramPrime)].insert(END_MARKER);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const Production& p : grammar()) {
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                Symbol b = p.rhs[i];
                if (isTerminal(b)) continue;
                size_t before = follow_[b].size();

                // FOLLOW(B) ⊇ FIRST(beta) for A -> alpha B beta
                bool betaNullable = true;
                for (size_t j = i + 1; j < p.rhs.size(); ++j) {
                    Symbol y = p.rhs[j];
                    follow_[b].insert(first_[y].begin(), first_[y].end());
                    if (!nullable_[y]) { betaNullable = false; break; }
                }
                // FOLLOW(B) ⊇ FOLLOW(A) if beta =>* epsilon
                if (betaNullable) follow_[b].insert(follow_[p.lhs].begin(), follow_[p.lhs].end());

                if (follow_[b].size() != before) changed = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 2. LR(0) canonical collection
// ---------------------------------------------------------------------------

ItemSet SlrTable::closure(ItemSet items) const {
    const auto& g = grammar();
    std::set<Item> seen(items.begin(), items.end());
    for (size_t k = 0; k < items.size(); ++k) {
        Item it = items[k];
        const Production& p = g[it.prod];
        if (it.dot >= static_cast<int>(p.rhs.size())) continue;
        Symbol b = p.rhs[it.dot];
        if (isTerminal(b)) continue;
        // [A -> alpha . B beta] in I  =>  add [B -> . gamma] for every B-production
        for (int q = 0; q < static_cast<int>(g.size()); ++q) {
            if (g[q].lhs != b) continue;
            Item fresh{q, 0};
            if (seen.insert(fresh).second) items.push_back(fresh);
        }
    }
    std::sort(items.begin(), items.end());
    return items;
}

ItemSet SlrTable::gotoSet(const ItemSet& items, Symbol x) const {
    ItemSet moved;
    for (const Item& it : items) {
        const Production& p = grammar()[it.prod];
        if (it.dot < static_cast<int>(p.rhs.size()) && p.rhs[it.dot] == x) {
            moved.push_back(Item{it.prod, it.dot + 1});
        }
    }
    return moved.empty() ? moved : closure(moved);
}

void SlrTable::buildCollection() {
    std::map<ItemSet, int> index;
    states_.push_back(closure({Item{0, 0}}));
    index[states_[0]] = 0;
    transitions_.emplace_back();

    for (size_t s = 0; s < states_.size(); ++s) {
        // Visit outgoing symbols in the order they appear after a dot, so
        // state numbering follows the usual textbook construction order.
        std::vector<Symbol> order;
        for (const Item& it : states_[s]) {
            const Production& p = grammar()[it.prod];
            if (it.dot < static_cast<int>(p.rhs.size())) {
                Symbol x = p.rhs[it.dot];
                if (std::find(order.begin(), order.end(), x) == order.end()) order.push_back(x);
            }
        }
        for (Symbol x : order) {
            ItemSet next = gotoSet(states_[s], x);
            auto found = index.find(next);
            int target;
            if (found == index.end()) {
                target = static_cast<int>(states_.size());
                index[next] = target;
                states_.push_back(next);
                transitions_.emplace_back();
            } else {
                target = found->second;
            }
            transitions_[s][x] = target;
        }
    }
}

// ---------------------------------------------------------------------------
// 3. ACTION / GOTO
// ---------------------------------------------------------------------------

namespace {
std::string describe(const TableEntry& e) {
    switch (e.kind) {
        case ActKind::Shift:  return "s" + std::to_string(e.target);
        case ActKind::Reduce: return "r" + std::to_string(e.target);
        case ActKind::Accept: return "acc";
        case ActKind::Error:  return "";
    }
    return "";
}
} // namespace

void SlrTable::setAction(int state, Symbol t, TableEntry e) {
    TableEntry& slot = action_[state][t];
    if (slot.kind != ActKind::Error && (slot.kind != e.kind || slot.target != e.target)) {
        bool sr = slot.kind == ActKind::Shift || e.kind == ActKind::Shift;
        if (sr && t == term(TokenType::KW_ELSE)) {
            // dangling else: `if (a) if (b) s1; else s2;` — shift, so the
            // else pairs with the nearest unmatched if
            resolved_.push_back(Conflict{
                state, t,
                "shift/reduce conflict in state " + std::to_string(state) + " on 'else': " +
                    describe(slot) + " vs " + describe(e) + "  -> resolved as shift (dangling else)"});
            if (e.kind == ActKind::Shift) slot = e;
            return;
        }
        conflicts_.push_back(Conflict{
            state, t,
            std::string(sr ? "shift/reduce" : "reduce/reduce") + " conflict in state " +
                std::to_string(state) + " on '" + symbolName(t) + "': " + describe(slot) +
                " vs " + describe(e)});
        return; // keep the first entry; the conflict is reported, not hidden
    }
    slot = e;
}

void SlrTable::buildTables() {
    int n = numStates();
    action_.assign(n, std::vector<TableEntry>(NUM_TERMINALS));
    goto_.assign(n, std::vector<int>(NUM_NONTERMINALS, -1));

    for (int s = 0; s < n; ++s) {
        for (const auto& [sym, target] : transitions_[s]) {
            if (isTerminal(sym)) setAction(s, sym, TableEntry{ActKind::Shift, target});
            else goto_[s][sym - NUM_TERMINALS] = target;
        }
        for (const Item& it : states_[s]) {
            const Production& p = grammar()[it.prod];
            if (it.dot != static_cast<int>(p.rhs.size())) continue;
            if (it.prod == 0) {
                setAction(s, END_MARKER, TableEntry{ActKind::Accept, 0});
            } else {
                for (Symbol a : follow_[p.lhs]) setAction(s, a, TableEntry{ActKind::Reduce, it.prod});
            }
        }
    }
}

std::vector<Symbol> SlrTable::expectedTerminals(int state) const {
    std::vector<Symbol> out;
    for (Symbol t = 0; t < NUM_TERMINALS; ++t) {
        if (action_[state][t].kind != ActKind::Error) out.push_back(t);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Dumps (for the report / review): cascade --tables
// ---------------------------------------------------------------------------

namespace {
void printSet(std::ostream& out, const std::set<Symbol>& s) {
    out << "{ ";
    bool firstItem = true;
    for (Symbol x : s) {
        if (!firstItem) out << ", ";
        out << symbolName(x);
        firstItem = false;
    }
    out << " }";
}
} // namespace

void SlrTable::printFirstFollow(std::ostream& out) const {
    out << "GRAMMAR (augmented, numbered)\n";
    for (int i = 0; i < static_cast<int>(grammar().size()); ++i) {
        out << "  " << std::setw(3) << i << ")  " << productionText(i) << "\n";
    }
    out << "\nFIRST / FOLLOW SETS\n";
    for (int k = 0; k < NUM_NONTERMINALS; ++k) {
        Symbol a = NUM_TERMINALS + k;
        out << "  " << std::left << std::setw(12) << symbolName(a) << std::right << " FIRST  = ";
        printSet(out, first_[a]);
        out << "\n  " << std::setw(12) << "" << " FOLLOW = ";
        printSet(out, follow_[a]);
        out << "\n";
    }
}

void SlrTable::printItemSets(std::ostream& out) const {
    out << "\nCANONICAL LR(0) COLLECTION (" << numStates() << " states)\n";
    for (int s = 0; s < numStates(); ++s) {
        out << "\nI" << s << ":\n";
        for (const Item& it : states_[s]) {
            const Production& p = grammar()[it.prod];
            out << "    " << symbolName(p.lhs) << " ->";
            for (int i = 0; i <= static_cast<int>(p.rhs.size()); ++i) {
                if (i == it.dot) out << " .";
                if (i < static_cast<int>(p.rhs.size())) out << " " << symbolName(p.rhs[i]);
            }
            out << "\n";
        }
        for (const auto& [sym, target] : transitions_[s]) {
            out << "    goto(I" << s << ", " << symbolName(sym) << ") = I" << target << "\n";
        }
    }
}

void SlrTable::printTable(std::ostream& out) const {
    out << "\nSLR PARSING TABLE (non-error entries only; sN = shift to N, rN = reduce by production N)\n";
    for (int s = 0; s < numStates(); ++s) {
        out << "  " << std::setw(3) << s << " | ACTION:";
        for (Symbol t = 0; t < NUM_TERMINALS; ++t) {
            const TableEntry& e = action_[s][t];
            if (e.kind != ActKind::Error) out << "  " << symbolName(t) << "=" << describe(e);
        }
        bool any = false;
        for (int k = 0; k < NUM_NONTERMINALS; ++k) {
            if (goto_[s][k] < 0) continue;
            if (!any) out << "  | GOTO:";
            any = true;
            out << "  " << symbolName(NUM_TERMINALS + k) << "=" << goto_[s][k];
        }
        out << "\n";
    }
    out << "\n" << conflicts_.size() << " conflict(s)";
    if (conflicts_.empty()) out << " — the grammar is SLR(1).\n";
    else out << ":\n";
    for (const Conflict& c : conflicts_) out << "  " << c.description << "\n";
    if (!resolved_.empty()) {
        out << resolved_.size() << " conflict(s) resolved on purpose:\n";
        for (const Conflict& c : resolved_) out << "  " << c.description << "\n";
    }
}
