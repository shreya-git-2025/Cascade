CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRC := src/main.cpp src/lexer.cpp src/token.cpp
BIN := cascade_lexer

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC) include/lexer.h include/token.h
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

# Runs the lexer over every sample program in tests/ so you can eyeball the
# token streams and confirm errors are reported where expected.
test: $(BIN)
	@for f in tests/*.casc; do \
		echo "=== $$f ==="; \
		./$(BIN) $$f; \
		echo; \
	done

clean:
	rm -f $(BIN)
