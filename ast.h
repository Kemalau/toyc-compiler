#pragma once
#include "token.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

enum class ValueType { Int, Void };

// AST 中的名字先按源码保存，语义分析后 symbol 才指向唯一的局部变量编号。
struct Expr {
    enum class Kind { Number, Variable, Unary, Binary, Call };
    Kind kind;
    Token at;
    std::string text; // 变量名、运算符或函数名
    std::int64_t number = 0;
    int symbol = -1;
    ValueType type = ValueType::Int;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr>> args;
    Expr(Kind k, Token token) : kind(k), at(std::move(token)), text(at.text) {}
};

struct Stmt {
    enum class Kind { Block, Empty, Declaration, Assignment, Expression,
                      If, While, Break, Continue, Return };
    Kind kind;
    Token at;
    std::string name;
    bool isConst = false;
    int symbol = -1;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Stmt> first;  // then 或循环体
    std::unique_ptr<Stmt> second; // else
    std::vector<std::unique_ptr<Stmt>> statements;
    Stmt(Kind k, Token token) : kind(k), at(std::move(token)) {}
};

struct Parameter {
    Token at;
    int symbol = -1;
};

struct Function {
    Token name;
    ValueType returnType = ValueType::Int;
    std::vector<Parameter> params;
    std::unique_ptr<Stmt> body;
    int localCount = 0;
};

struct Program {
    std::vector<Function> functions;
};
