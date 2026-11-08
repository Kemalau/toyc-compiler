#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>

struct Token {
    std::string type;
    std::string text;
    std::size_t line = 1;
    std::size_t column = 1;
};

[[noreturn]] inline void compileError(const std::string& stage, const Token& at,
                                     const std::string& message) {
    throw std::runtime_error(stage + " error at " + std::to_string(at.line) +
                             ":" + std::to_string(at.column) + ": " + message);
}
