CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRC      := $(wildcard src/*.cpp)
HDR      := $(wildcard include/*.h)
BIN      := cascade

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

test: $(BIN)
	@for f in tests/*.casc; do echo "=== $$f ==="; ./$(BIN) --ast $$f; echo; done

clean:
	rm -f $(BIN)
