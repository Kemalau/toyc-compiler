#include "optimizer.h"
#include <limits>
#include <optional>
#include <unordered_map>

namespace {
using Integer = std::int32_t;

std::optional<Integer> checked(std::int64_t value) {
    if (value < std::numeric_limits<Integer>::min() || value > std::numeric_limits<Integer>::max())
        return std::nullopt;
    return static_cast<Integer>(value);
}

std::optional<Integer> fold(const Instruction& i) {
    const std::int64_t a = i.a.value;
    const std::int64_t b = i.b.value;
    if (!i.a.immediate) return std::nullopt;
    if (i.kind == IROp::Unary) {
        if (i.op == "+") return i.a.value;
        if (i.op == "-") return checked(-a);
        if (i.op == "!") return a == 0;
        if (i.op == "bool") return a != 0;
    }
    if (i.kind != IROp::Binary || !i.b.immediate) return std::nullopt;
    if (i.op == "+") return checked(a + b);
    if (i.op == "-") return checked(a - b);
    if (i.op == "*") return checked(a * b);
    // 不折叠除零或 INT_MIN / -1，避免编译器自身产生未定义行为。
    if (i.op == "/" || i.op == "%") {
        if (b == 0 || (a == std::numeric_limits<Integer>::min() && b == -1)) return std::nullopt;
        return static_cast<Integer>(i.op == "/" ? a / b : a % b);
    }
    if (i.op == "<") return a < b;
    if (i.op == ">") return a > b;
    if (i.op == "<=") return a <= b;
    if (i.op == ">=") return a >= b;
    if (i.op == "==") return a == b;
    if (i.op == "!=") return a != b;
    return std::nullopt;
}

void propagate(IRFunction& function) {
    std::unordered_map<int, Integer> constants;
    std::vector<Instruction> result;
    auto substitute = [&](Operand& value) {
        if (!value.immediate) {
            const auto found = constants.find(value.value);
            if (found != constants.end()) value = Operand::constant(found->second);
        }
    };
    for (auto i : function.code) {
        // 只在基本块内传播。标签处可能有多个前驱，不能沿用单一路径的常量。
        if (i.kind == IROp::Label) constants.clear();
        substitute(i.a);
        substitute(i.b);
        for (auto& arg : i.args) substitute(arg);
        if (i.kind == IROp::Unary || i.kind == IROp::Binary) {
            if (const auto value = fold(i)) {
                i.kind = IROp::Move;
                i.a = Operand::constant(*value);
                i.b = Operand::constant(0);
                i.op.clear();
            }
        }
        if (i.dest >= 0) {
            constants.erase(i.dest);
            if (i.kind == IROp::Move && i.a.immediate) constants[i.dest] = i.a.value;
        }
        if ((i.kind == IROp::BranchZero || i.kind == IROp::BranchNonzero) && i.a.immediate) {
            const bool taken = (i.a.value == 0) == (i.kind == IROp::BranchZero);
            constants.clear();
            if (!taken) continue;
            i.kind = IROp::Jump;
        }
        if (i.kind == IROp::Jump || i.kind == IROp::BranchZero ||
            i.kind == IROp::BranchNonzero || i.kind == IROp::Return) constants.clear();
        result.push_back(std::move(i));
    }
    function.code = std::move(result);
}

void pruneUnreachable(IRFunction& function) {
    // 对指令构造控制流边，从入口标记可达指令，删除不可达分支和返回后的代码。
    const auto count = function.code.size();
    if (count == 0) return;
    std::unordered_map<int, std::size_t> labels;
    for (std::size_t n = 0; n < count; ++n)
        if (function.code[n].kind == IROp::Label) labels[function.code[n].label] = n;
    std::vector<bool> seen(count, false);
    std::vector<std::size_t> work{0};
    while (!work.empty()) {
        const auto n = work.back(); work.pop_back();
        if (n >= count || seen[n]) continue;
        seen[n] = true;
        const auto& i = function.code[n];
        if (i.kind == IROp::Jump || i.kind == IROp::BranchZero || i.kind == IROp::BranchNonzero)
            work.push_back(labels.at(i.label));
        if (i.kind != IROp::Jump && i.kind != IROp::Return) work.push_back(n + 1);
    }
    std::vector<Instruction> result;
    for (std::size_t n = 0; n < count; ++n)
        if (seen[n]) result.push_back(std::move(function.code[n]));
    function.code = std::move(result);
}

void removeUnusedTemps(IRFunction& function) {
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<int> uses(function.registerCount, 0);
        auto use = [&](Operand operand) { if (!operand.immediate) ++uses.at(operand.value); };
        for (const auto& i : function.code) {
            switch (i.kind) {
            case IROp::Move: case IROp::Unary: case IROp::BranchZero:
            case IROp::BranchNonzero: case IROp::Return: use(i.a); break;
            case IROp::Binary: use(i.a); use(i.b); break;
            case IROp::Call: for (auto arg : i.args) use(arg); break;
            default: break;
            }
        }
        std::vector<Instruction> result;
        for (auto i : function.code) {
            if (i.dest >= function.localCount && uses[i.dest] == 0) {
                if (i.kind == IROp::Move || i.kind == IROp::Unary || i.kind == IROp::Binary) {
                    changed = true;
                    continue;
                }
                // 函数调用可能有副作用，永远保留调用本身。
                if (i.kind == IROp::Call) i.dest = -1;
            }
            result.push_back(std::move(i));
        }
        function.code = std::move(result);
    }
}
} // namespace

void optimize(IRProgram& program) {
    for (auto& function : program.functions) {
        propagate(function);
        pruneUnreachable(function);
        removeUnusedTemps(function);
    }
}
