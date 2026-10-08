#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/semantic.h"
#include "../include/slr.h"
#include "../include/tac.h"
#include "../include/vm.h"

namespace {

struct Options {
    bool tokens = false;
    bool tables = false;
    bool parseTrace = false;
    bool ast = false;
    bool symbols = false;
    bool tac = false;
    bool bytecode = false;
    bool trace = false;
    long long maxSteps = 10000000;
    std::string stopAfter; // "", "lex", "parse", "sema", "tac"
    std::string file;
};

void usage() {
    std::cerr <<
        "usage: cascade [options] <source-file>\n"
        "       cascade --tables              (dump the SLR construction, no file needed)\n"
        "\n"
        "  --tokens         print the token stream                (stage 1)\n"
        "  --tables         print FIRST/FOLLOW, LR(0) item sets, SLR table\n"
        "  --parse-trace    print every shift/reduce step         (stage 2)\n"
        "  --ast            print the AST (annotated after stage 3)\n"
        "  --symbols        print the symbol table                (stage 3)\n"
        "  --tac            print three-address code              (stage 4)\n"
        "  --bytecode       print VM bytecode                     (stage 5)\n"
        "  --trace          trace VM execution step by step       (stage 5)\n"
        "  --all            --tokens --ast --symbols --tac --bytecode\n"
        "  --max-steps=N    VM step limit (default 10000000)\n"
        "  --stop-after=S   stop after stage S: lex | parse | sema | tac\n"
        "\n"
        "exit status: 0 ok, 1 compile error, 2 usage/I-O error, 3 runtime error\n";
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--tokens") o.tokens = true;
        else if (a == "--tables") o.tables = true;
        else if (a == "--parse-trace") o.parseTrace = true;
        else if (a == "--ast") o.ast = true;
        else if (a == "--symbols") o.symbols = true;
        else if (a == "--tac") o.tac = true;
        else if (a == "--bytecode") o.bytecode = true;
        else if (a == "--trace") o.trace = true;
        else if (a == "--all") o.tokens = o.ast = o.symbols = o.tac = o.bytecode = true;
        else if (a.rfind("--max-steps=", 0) == 0) o.maxSteps = std::atoll(a.c_str() + 12);
        else if (a.rfind("--stop-after=", 0) == 0) o.stopAfter = a.substr(13);
        else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option '" << a << "'\n"; return false; }
        else if (o.file.empty()) o.file = a;
        else return false;
    }
    return !o.file.empty() || o.tables;
}

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream file(path);
    if (!file) { ok = false; return ""; }
    std::ostringstream ss;
    ss << file.rdbuf();
    ok = true;
    return ss.str();
}

void header(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

void printTokens(const std::vector<Token>& tokens) {
    std::cout << std::left << std::setw(6) << "LINE" << std::setw(6) << "COL"
              << std::setw(16) << "TYPE" << "LEXEME\n";
    std::cout << std::string(50, '-') << "\n";
    for (const Token& tok : tokens) {
        if (tok.type == TokenType::END_OF_FILE) continue;
        std::cout << std::left << std::setw(6) << tok.line << std::setw(6) << tok.column
                  << std::setw(16) << tokenTypeName(tok.type) << tok.lexeme << "\n";
    }
    std::cout << "\n" << tokens.size() - 1 << " token(s) scanned.\n";
}

} // namespace

// Exit codes: 0 ok, 1 compile-time error (any stage), 2 usage / I/O.
int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) { usage(); return 2; }

    const SlrTable& table = SlrTable::instance();
    if (opt.tables) {
        table.printFirstFollow(std::cout);
        table.printItemSets(std::cout);
        table.printTable(std::cout);
        if (opt.file.empty()) return table.conflicts().empty() ? 0 : 1;
    }
    if (!table.conflicts().empty()) {
        std::cout << "internal error: grammar is not SLR(1) (run --tables for details)\n";
        return 1;
    }

    bool ok = false;
    std::string source = readFile(opt.file, ok);
    if (!ok) { std::cerr << "error: could not open '" << opt.file << "'\n"; return 2; }

    // ---- Stage 1: lexical analysis ----
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    if (opt.tokens) { header("TOKENS"); printTokens(tokens); }
    if (lexer.hasErrors()) {
        std::cout << lexer.errors().size() << " lexical error(s):\n";
        for (const LexError& err : lexer.errors()) {
            std::cout << "  line " << err.line << ", col " << err.column
                      << ": lexical error: " << err.message << "\n";
        }
        return 1;
    }
    if (opt.stopAfter == "lex") return 0;

    // ---- Stage 2: SLR parsing -> AST ----
    Parser parser(tokens);
    if (opt.parseTrace) header("PARSE TRACE");
    NodePtr program = parser.parse(opt.parseTrace ? &std::cout : nullptr);
    if (parser.hasErrors()) {
        for (const ParseError& err : parser.errors()) {
            std::cout << "  line " << err.line << ", col " << err.column
                      << ": syntax error: " << err.message << "\n";
        }
        return 1;
    }
    if (opt.stopAfter == "parse") {
        if (opt.ast) { header("AST"); printAst(program, std::cout); }
        return 0;
    }

    // ---- Stage 3: semantic analysis ----
    SemanticAnalyzer sema;
    sema.analyze(program);
    for (const SemanticDiag& w : sema.warnings()) {
        std::cout << "  line " << w.line << ", col " << w.column << ": warning: " << w.message << "\n";
    }
    if (opt.ast) { header("ANNOTATED AST"); printAst(program, std::cout); }
    if (opt.symbols) { header("SYMBOL TABLE"); sema.printSymbolTable(std::cout); }
    if (sema.hasErrors()) {
        std::cout << sema.errors().size() << " semantic error(s):\n";
        for (const SemanticDiag& err : sema.errors()) {
            std::cout << "  line " << err.line << ", col " << err.column
                      << ": semantic error: " << err.message << "\n";
        }
        return 1;
    }
    if (opt.stopAfter == "sema") return 0;

    // ---- Stage 4: intermediate code generation ----
    TacGenerator icg;
    TacProgram tac = icg.generate(program, sema.symbols());
    if (opt.tac) { header("THREE-ADDRESS CODE"); tac.print(std::cout); }
    if (opt.stopAfter == "tac") return 0;

    // ---- Stage 5: assemble to bytecode and run on the stack VM ----
    Bytecode bc = assemble(tac);
    if (opt.bytecode) { header("BYTECODE"); bc.print(std::cout); }
    VirtualMachine vm(bc, opt.maxSteps);
    if (opt.trace) header("EXECUTION TRACE");
    bool ran = vm.run(opt.trace ? &std::cout : nullptr);
    if (!ran) {
        std::cout << "runtime error";
        if (vm.error().line > 0) std::cout << " at line " << vm.error().line;
        std::cout << ": " << vm.error().message << "\n";
        return 3;
    }
    header("PROGRAM STATE (after " + std::to_string(vm.steps()) + " VM steps)");
    vm.printState(std::cout);
    return 0;
}
