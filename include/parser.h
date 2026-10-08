#ifndef CASCADE_PARSER_H
#define CASCADE_PARSER_H

#include <ostream>
#include <string>
#include <vector>
#include "ast.h"
#include "token.h"

struct ParseError {
    std::string message;
    int line;
    int column;
};

// Table-driven shift-reduce parser over SlrTable. Each reduction runs the
// production's semantic action, so the AST is built bottom-up during the
// parse (syntax-directed translation) rather than in a second pass.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    // Returns the Program node, or nullptr on a syntax error. If trace is
    // non-null, every step (state stack, lookahead, action) is written to it.
    NodePtr parse(std::ostream* trace = nullptr);

    const std::vector<ParseError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

private:
    std::vector<Token> tokens_;
    std::vector<ParseError> errors_;
};

#endif // CASCADE_PARSER_H
