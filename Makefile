CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRC      := $(wildcard src/*.cpp)
HDR      := $(wildcard include/*.h)
BIN      := cascade

.PHONY: all clean test docs

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

# Full regression suite: every tests/*/*.casc against its .expected file
test: $(BIN)
	@tests/run_tests.sh

# Regenerate the intermediate-result artifacts used in the report
docs: $(BIN)
	@mkdir -p docs/generated
	./$(BIN) --tables > docs/generated/slr-tables.txt
	./$(BIN) --all --parse-trace --trace tests/valid/01_if_else.casc > docs/generated/sample-run-if-else.txt
	./$(BIN) --ast --tac --bytecode tests/valid/06_short_circuit.casc > docs/generated/sample-run-short-circuit.txt
	./$(BIN) --ast --symbols --tac --bytecode tests/valid/16_array_sort.casc > docs/generated/sample-run-functions-arrays.txt

clean:
	rm -f $(BIN)
