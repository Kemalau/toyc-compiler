CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
SOURCES := main.cpp lexer.cpp parser.cpp semantic.cpp ir.cpp optimizer.cpp codegen.cpp
HEADERS := token.h lexer.h ast.h parser.h semantic.h ir.h optimizer.h codegen.h

.PHONY: all test test-riscv clean

all: compiler

compiler: $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

test: compiler
	python3 tests/test_lexer.py ./compiler
	python3 tests/test_compiler.py ./compiler

test-riscv: compiler
	python3 tests/test_riscv.py ./compiler

clean:
	rm -f compiler
