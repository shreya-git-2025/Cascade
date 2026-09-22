#include "../include/lexer.h"
#include <cctype>
#include <unordered_map>

namespace {
// Keyword table, checked after an identifier is fully scanned (maximal
// munch): this is what lets "iffy" lex as one IDENTIFIER rather than the
// keyword "if" followed by "fy".
const std::unordered_map<std::string, TokenType>& keywordTable() {
    static const std::unordered_map<std::string, TokenType> table = {
        {"int", TokenType::KW_INT},
        {"float", TokenType::KW_FLOAT},
        {"if", TokenType::KW_IF},
        {"else", TokenType::KW_ELSE},
        {"while", TokenType::KW_WHILE},
        {"true", TokenType::KW_TRUE},
        {"false", TokenType::KW_FALSE},
    };
    return table;
}
} // namespace

Lexer::Lexer(std::string source) : src_(std::move(source)) {}

bool Lexer::isAtEnd() const { return pos_ >= src_.size(); }

char Lexer::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    if (idx >= src_.size()) return '\0';
    return src_[idx];
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            // Line comment. Not part of the formal grammar in the proposal,
            // but harmless to skip here and makes hand-written test
            // programs easier to annotate.
            while (!isAtEnd() && peek() != '\n') advance();
        } else {
            return;
        }
    }
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme, int startLine, int startCol) {
    return Token(type, lexeme, startLine, startCol);
}

void Lexer::recordError(const std::string& message, int startLine, int startCol) {
    errors_.push_back(LexError{message, startLine, startCol});
}

Token Lexer::scanIdentifierOrKeyword() {
    int startLine = line_, startCol = col_;
    std::string lexeme;
    while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        lexeme += advance();
    }
    auto it = keywordTable().find(lexeme);
    if (it != keywordTable().end()) {
        return makeToken(it->second, lexeme, startLine, startCol);
    }
    return makeToken(TokenType::IDENTIFIER, lexeme, startLine, startCol);
}

Token Lexer::scanNumber() {
    int startLine = line_, startCol = col_;
    std::string lexeme;
    bool isFloat = false;
    bool malformed = false;

    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        lexeme += advance();
    }

    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        isFloat = true;
        lexeme += advance(); // consume '.'
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            lexeme += advance();
        }
    } else if (peek() == '.') {
        // A trailing dot with no fractional digits (e.g. "12.") is a
        // malformed literal, not a valid int followed by '.': consume it so
        // the error message shows the whole bad token, then keep scanning.
        isFloat = true;
        malformed = true;
        lexeme += advance();
    }

    // A second decimal point (e.g. "1.2.3") also makes the literal
    // malformed; fold it into the same error token rather than silently
    // splitting into "1.2", ".", "3".
    if (peek() == '.' && isFloat) {
        malformed = true;
        while (!isAtEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')) {
            lexeme += advance();
        }
    }

    // A letter immediately following a numeric literal (e.g. "123abc") is
    // also malformed under maximal munch — it can't be two tokens because
    // there's no separator, so it must be reported as one bad literal.
    if (!isAtEnd() && std::isalpha(static_cast<unsigned char>(peek()))) {
        malformed = true;
        while (!isAtEnd() && std::isalnum(static_cast<unsigned char>(peek()))) {
            lexeme += advance();
        }
    }

    if (malformed) {
        recordError("malformed numeric literal '" + lexeme + "'", startLine, startCol);
        return makeToken(TokenType::LEX_ERROR, lexeme, startLine, startCol);
    }

    return makeToken(isFloat ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL,
                      lexeme, startLine, startCol);
}

Token Lexer::scanOperatorOrPunctuation() {
    int startLine = line_, startCol = col_;
    char c = advance();

    // Two-character operators are tried first (longest match): '=' alone is
    // ASSIGN, but '==' is EQ, so we must look ahead before committing.
    switch (c) {
        case '+': return makeToken(TokenType::PLUS, "+", startLine, startCol);
        case '-': return makeToken(TokenType::MINUS, "-", startLine, startCol);
        case '*': return makeToken(TokenType::STAR, "*", startLine, startCol);
        case '/': return makeToken(TokenType::SLASH, "/", startLine, startCol);
        case '%': return makeToken(TokenType::PERCENT, "%", startLine, startCol);

        case '(': return makeToken(TokenType::LPAREN, "(", startLine, startCol);
        case ')': return makeToken(TokenType::RPAREN, ")", startLine, startCol);
        case '{': return makeToken(TokenType::LBRACE, "{", startLine, startCol);
        case '}': return makeToken(TokenType::RBRACE, "}", startLine, startCol);
        case ';': return makeToken(TokenType::SEMICOLON, ";", startLine, startCol);
        case ',': return makeToken(TokenType::COMMA, ",", startLine, startCol);

        case '<':
            if (peek() == '=') { advance(); return makeToken(TokenType::LE, "<=", startLine, startCol); }
            return makeToken(TokenType::LT, "<", startLine, startCol);
        case '>':
            if (peek() == '=') { advance(); return makeToken(TokenType::GE, ">=", startLine, startCol); }
            return makeToken(TokenType::GT, ">", startLine, startCol);
        case '=':
            if (peek() == '=') { advance(); return makeToken(TokenType::EQ, "==", startLine, startCol); }
            return makeToken(TokenType::ASSIGN, "=", startLine, startCol);
        case '!':
            if (peek() == '=') { advance(); return makeToken(TokenType::NE, "!=", startLine, startCol); }
            return makeToken(TokenType::NOT, "!", startLine, startCol);
        case '&':
            if (peek() == '&') { advance(); return makeToken(TokenType::AND_AND, "&&", startLine, startCol); }
            recordError("unexpected character '&' (did you mean '&&'?)", startLine, startCol);
            return makeToken(TokenType::LEX_ERROR, "&", startLine, startCol);
        case '|':
            if (peek() == '|') { advance(); return makeToken(TokenType::OR_OR, "||", startLine, startCol); }
            recordError("unexpected character '|' (did you mean '||'?)", startLine, startCol);
            return makeToken(TokenType::LEX_ERROR, "|", startLine, startCol);

        default: {
            std::string lexeme(1, c);
            recordError("unexpected character '" + lexeme + "'", startLine, startCol);
            return makeToken(TokenType::LEX_ERROR, lexeme, startLine, startCol);
        }
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    for (;;) {
        skipWhitespaceAndComments();
        if (isAtEnd()) break;

        char c = peek();
        Token tok = [&]() -> Token {
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                return scanIdentifierOrKeyword();
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                return scanNumber();
            }
            return scanOperatorOrPunctuation();
        }();

        // LEX_ERROR tokens are still pushed into the stream: the parser
        // stage can choose to skip them, but hiding them here would make
        // "how many errors did this program have" harder to answer from
        // the token list alone.
        tokens.push_back(tok);
    }

    tokens.push_back(makeToken(TokenType::END_OF_FILE, "", line_, col_));
    return tokens;
}
