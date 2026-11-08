#pragma once
#include "ast.h"
#include <ostream>

// 所有运算都使用有符号 32 位整数；虚拟寄存器稍后映射到栈槽。
struct Operand {
    bool immediate = true;
    std::int32_t value = 0; // immediate 时是常量，否则是虚拟寄存器编号
    static Operand constant(std::int32_t value) { return {true, value}; }
    static Operand reg(int id) { return {false, id}; }
};

enum class IROp { Move, Unary, Binary, Label, Jump, BranchZero, BranchNonzero, Call, Return };

struct Instruction {
    IROp kind;
    int dest = -1;
    Operand a;
    Operand b;
    std::string op; // 运算符或函数名
    int label = -1;
    std::vector<Operand> args;
    explicit Instruction(IROp k) : kind(k) {}
};

struct IRFunction {
    std::string name;
    ValueType returnType = ValueType::Int;
    std::vector<int> params;
    int localCount = 0;
    int registerCount = 0;
    std::vector<Instruction> code;
};

struct IRProgram { std::vector<IRFunction> functions; };

IRProgram generateIR(const Program& program);
void printIR(const IRProgram& program, std::ostream& out);
