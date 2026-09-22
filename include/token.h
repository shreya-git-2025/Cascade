#ifndef CASCADE_TOKEN_H
#define CASCADE_TOKEN_H

#include <string>

// One entry per terminal the lexer can produce. Keeping keywords and
// punctuation as distinct enum values (rather than lumping everything into
// IDENTIFIER / OPERATOR) makes the token stream self-describing, which is
// what the SLR parser will pattern-match against in the next stage.
enum class TokenType {
    // literals & identifiers
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,

    // keywords
    KW_INT,
    KW_FLOAT,
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_TRUE,
    KW_FALSE,

    // arithmetic operators
    PLUS, MINUS, STAR, SLASH, PERCENT,

    // relational operators
    LT, LE, GT, GE, EQ, NE,

    // logical operators (short-circuit)
    AND_AND, OR_OR, NOT,

    // assignment
    ASSIGN,

    // punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, SEMICOLON, COMMA,

    // control
    END_OF_FILE,
    LEX_ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;   // raw text as it appeared in source
    int line;             // 1-indexed line number, for diagnostics
    int column;           // 1-indexed column of the first character

    Token(TokenType t, std::string lex, int ln, int col)
        : type(t), lexeme(std::move(lex)), line(ln), column(col) {}
};

// Human-readable name for a token type — used by the pretty-printer and by
// error messages. Kept in one place so the parser stage can reuse it too.
std::string tokenTypeName(TokenType type);

#endif // CASCADE_TOKEN_H
