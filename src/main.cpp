#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "../include/lexer.h"

namespace {
std::string readFile(const std::string& path, bool& ok) {
    std::ifstream file(path);
    if (!file) {
        ok = false;
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    ok = true;
    return ss.str();
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: cascade_lexer <source-file>\n";
        return 2;
    }

    bool ok = false;
    std::string source = readFile(argv[1], ok);
    if (!ok) {
        std::cerr << "error: could not open '" << argv[1] << "'\n";
        return 2;
    }

    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    std::cout << std::left
               << std::setw(6)  << "LINE"
               << std::setw(6)  << "COL"
               << std::setw(16) << "TYPE"
               << "LEXEME\n";
    std::cout << std::string(50, '-') << "\n";

    for (const Token& tok : tokens) {
        if (tok.type == TokenType::END_OF_FILE) continue;
        std::cout << std::left
                   << std::setw(6)  << tok.line
                   << std::setw(6)  << tok.column
                   << std::setw(16) << tokenTypeName(tok.type)
                   << tok.lexeme << "\n";
    }

    std::cout << "\n" << tokens.size() - 1 << " token(s) scanned.\n";

    if (lexer.hasErrors()) {
        std::cout << "\n" << lexer.errors().size() << " lexical error(s):\n";
        for (const LexError& err : lexer.errors()) {
            std::cout << "  line " << err.line << ", col " << err.column
                        << ": " << err.message << "\n";
        }
        return 1;
    }

    return 0;
}
