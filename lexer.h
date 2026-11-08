#pragma once
#include "token.h"
#include <ostream>
#include <vector>

std::vector<Token> tokenize(std::string source);
void printTokens(const std::vector<Token>& tokens, std::ostream& out);
