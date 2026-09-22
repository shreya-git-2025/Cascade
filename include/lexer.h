#ifndef CASCADE_LEXER_H
#define CASCADE_LEXER_H

#include <string>
#include <vector>
#include "token.h"

// One diagnostic emitted while scanning. Kept separate from Token so the
// caller (main.cpp today, the parser driver later) can decide whether a
// lexical error should abort the pipeline or just be reported and skipped.
struct LexError {
    std::string message;
    int line;
    int column;
};

// Longest-match, single-pass scanner for the Cascade source language.
// tokenize() runs to completion even in the presence of errors: each bad
// character or malformed literal is recorded in errors() and the scanner
// resynchronizes at the next likely token boundary, so one typo doesn't
// hide every other problem in the file (useful for the "deliberately
// broken" test programs called for in the proposal).
class Lexer {
public:
    explicit Lexer(std::string source);

    // Scans the whole input and returns the token stream, terminated by a
    // single END_OF_FILE token. Safe to call once per Lexer instance.
    std::vector<Token> tokenize();

    const std::vector<LexError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

private:
    std::string src_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<LexError> errors_;

    char peek(int offset = 0) const;
    char advance();
    bool isAtEnd() const;
    void skipWhitespaceAndComments();

    Token makeToken(TokenType type, const std::string& lexeme, int startLine, int startCol);
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanOperatorOrPunctuation();
    void recordError(const std::string& message, int startLine, int startCol);
};

#endif // CASCADE_LEXER_H
