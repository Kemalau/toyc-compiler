#pragma once
#include "ast.h"
#include <ostream>

Program parse(const std::vector<Token>& tokens);
void printAst(const Program& program, std::ostream& out);
