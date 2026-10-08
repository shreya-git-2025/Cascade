#include "../include/parser.h"

#include <iomanip>
#include <sstream>
#include "../include/grammar.h"
#include "../include/slr.h"

namespace {
std::string describeToken(const Token& t) {
    switch (t.type) {
        case TokenType::END_OF_FILE:   return "end of input";
        case TokenType::IDENTIFIER:    return "identifier '" + t.lexeme + "'";
        case TokenType::INT_LITERAL:
        case TokenType::FLOAT_LITERAL: return "number '" + t.lexeme + "'";
        default:                       return "'" + t.lexeme + "'";
    }
}

std::string stackText(const std::vector<int>& states, const std::vector<Symbol>& symbols) {
    std::ostringstream ss;
    ss << states[0];
    for (size_t i = 0; i < symbols.size(); ++i) ss << " " << symbolName(symbols[i]) << " " << states[i + 1];
    return ss.str();
}
} // namespace

NodePtr Parser::parse(std::ostream* trace) {
    const SlrTable& table = SlrTable::instance();
    const auto& g = grammar();

    std::vector<int> states{0};   // state stack
    std::vector<Symbol> symbols;  // grammar symbols (only used for the trace)
    std::vector<SemVal> values;   // parallel value stack

    if (trace) {
        *trace << std::left << std::setw(48) << "STACK" << std::setw(14) << "LOOKAHEAD" << "ACTION\n";
    }

    size_t pos = 0;
    for (;;) {
        const Token& tok = tokens_[pos];
        Symbol a = term(tok.type);
        int s = states.back();
        const TableEntry& e = table.action(s, a);

        if (trace) {
            std::string st = stackText(states, symbols);
            if (st.size() > 46) st = "... " + st.substr(st.size() - 42);
            *trace << std::setw(48) << st << std::setw(14) << symbolName(a);
        }

        switch (e.kind) {
            case ActKind::Shift: {
                if (trace) *trace << "shift " << e.target << "\n";
                states.push_back(e.target);
                symbols.push_back(a);
                SemVal v;
                v.tok = tok;
                values.push_back(v);
                ++pos;
                break;
            }
            case ActKind::Reduce: {
                const Production& p = g[e.target];
                size_t n = p.rhs.size();
                std::vector<SemVal> rhs(values.end() - n, values.end());
                values.resize(values.size() - n);
                states.resize(states.size() - n);
                symbols.resize(symbols.size() - n);

                SemVal lhs = p.action(rhs);
                int next = table.gotoState(states.back(), p.lhs);
                if (trace) *trace << "reduce " << e.target << ": " << productionText(e.target) << "\n";
                states.push_back(next);
                symbols.push_back(p.lhs);
                values.push_back(lhs);
                break;
            }
            case ActKind::Accept:
                if (trace) *trace << "accept\n";
                return values.back().node;

            case ActKind::Error: {
                if (trace) *trace << "error\n";
                std::vector<Symbol> expected = table.expectedTerminals(s);
                std::ostringstream msg;
                msg << "unexpected " << describeToken(tok);
                if (!expected.empty() && expected.size() <= 8) {
                    msg << ", expected ";
                    for (size_t i = 0; i < expected.size(); ++i) {
                        if (i) msg << (i + 1 == expected.size() ? " or " : ", ");
                        msg << "'" << symbolName(expected[i]) << "'";
                    }
                } else if (table.action(s, term(TokenType::SEMICOLON)).kind != ActKind::Error) {
                    // too many alternatives to list, but a ';' would have
                    // been accepted here — by far the most common mistake
                    msg << " (missing ';' before it?)";
                }
                errors_.push_back(ParseError{msg.str(), tok.line, tok.column});
                // Stop at the first syntax error. Panic-mode recovery
                // (skip to ';' / '}' and resume) is planned for the final
                // phase so multiple syntax errors can be reported per run.
                return nullptr;
            }
        }
    }
}
