#include "../include/token.h"

std::string tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::IDENTIFIER:    return "IDENTIFIER";
        case TokenType::INT_LITERAL:   return "INT_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";

        case TokenType::KW_INT:        return "KW_INT";
        case TokenType::KW_FLOAT:      return "KW_FLOAT";
        case TokenType::KW_IF:         return "KW_IF";
        case TokenType::KW_ELSE:       return "KW_ELSE";
        case TokenType::KW_WHILE:      return "KW_WHILE";
        case TokenType::KW_TRUE:       return "KW_TRUE";
        case TokenType::KW_FALSE:      return "KW_FALSE";

        case TokenType::PLUS:          return "PLUS";
        case TokenType::MINUS:         return "MINUS";
        case TokenType::STAR:          return "STAR";
        case TokenType::SLASH:         return "SLASH";
        case TokenType::PERCENT:       return "PERCENT";

        case TokenType::LT:            return "LT";
        case TokenType::LE:            return "LE";
        case TokenType::GT:            return "GT";
        case TokenType::GE:            return "GE";
        case TokenType::EQ:            return "EQ";
        case TokenType::NE:            return "NE";

        case TokenType::AND_AND:       return "AND_AND";
        case TokenType::OR_OR:         return "OR_OR";
        case TokenType::NOT:           return "NOT";

        case TokenType::ASSIGN:        return "ASSIGN";

        case TokenType::LPAREN:        return "LPAREN";
        case TokenType::RPAREN:        return "RPAREN";
        case TokenType::LBRACE:        return "LBRACE";
        case TokenType::RBRACE:        return "RBRACE";
        case TokenType::SEMICOLON:     return "SEMICOLON";
        case TokenType::COMMA:         return "COMMA";

        case TokenType::END_OF_FILE:   return "EOF";
        case TokenType::LEX_ERROR:     return "LEX_ERROR";
    }
    return "UNKNOWN";
}
