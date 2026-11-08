#include "codegen.h"
#include <algorithm>
#include <cstdint>

namespace {
std::int64_t align16(std::int64_t bytes) { return (bytes + 15) / 16 * 16; }

class Codegen {
public:
    explicit Codegen(std::ostream& out) : out_(out) {}

    void function(const IRFunction& f, std::size_t index) {
        prefix_ = ".Ltoyc_" + std::to_string(index) + "_";
        const auto frame = align16(16 + 8LL * f.registerCount);
        out_ << "\n.p2align 2\n.globl " << f.name << "\n.type " << f.name
             << ", @function\n" << f.name << ":\n";
        // s0 指向进入函数时的 sp。保存 ra、s0，栈帧始终按 16 字节对齐。
        line("li t6, " + std::to_string(frame));
        line("sub sp, sp, t6");
        line("add t6, sp, t6");
        line("sd ra, -8(t6)");
        line("sd s0, -16(t6)");
        line("mv s0, t6");
        for (std::size_t n = 0; n < f.params.size(); ++n) {
            if (n < 8) store(f.params[n], "a" + std::to_string(n));
            else {
                memory("lw", "t0", "s0", 8LL * (n - 8));
                store(f.params[n], "t0");
            }
        }
        for (const auto& i : f.code) instruction(i);
        out_ << prefix_ << "return:\n";
        line("ld ra, -8(s0)");
        line("mv sp, s0");
        line("ld s0, -16(sp)");
        line("ret");
        out_ << ".size " << f.name << ", .-" << f.name << '\n';
    }

private:
    std::ostream& out_;
    std::string prefix_;

    void line(const std::string& text) { out_ << "    " << text << '\n'; }
    std::string label(int n) const { return prefix_ + std::to_string(n); }
    // 栈槽超过 12 位立即数范围时，先用 t6 计算地址。
    void memory(const std::string& op, const std::string& reg,
                const std::string& base, std::int64_t offset) {
        if (offset >= -2048 && offset <= 2047) {
            line(op + " " + reg + ", " + std::to_string(offset) + "(" + base + ")");
        } else {
            line("li t6, " + std::to_string(offset));
            line("add t6, " + base + ", t6");
            line(op + " " + reg + ", 0(t6)");
        }
    }
    void load(Operand value, const std::string& reg) {
        if (value.immediate) line("li " + reg + ", " + std::to_string(value.value));
        else memory("lw", reg, "s0", -24 - 8LL * value.value);
    }
    void store(int slot, const std::string& reg) { memory("sw", reg, "s0", -24 - 8LL * slot); }

    void instruction(const Instruction& i) {
        switch (i.kind) {
        case IROp::Move: load(i.a, "t0"); store(i.dest, "t0"); break;
        case IROp::Unary:
            load(i.a, "t0");
            if (i.op == "+") line("mv t2, t0");
            else if (i.op == "-") line("negw t2, t0");
            else if (i.op == "!") line("seqz t2, t0");
            else if (i.op == "bool") line("snez t2, t0");
            else throw std::logic_error("unknown unary operation");
            store(i.dest, "t2");
            break;
        case IROp::Binary:
            load(i.a, "t0"); load(i.b, "t1");
            binary(i.op);
            store(i.dest, "t2");
            break;
        case IROp::Label: out_ << label(i.label) << ":\n"; break;
        case IROp::Jump: line("j " + label(i.label)); break;
        case IROp::BranchZero:
        case IROp::BranchNonzero: {
            load(i.a, "t0");
            // 条件分支只跳到紧邻标签，远目标使用 j，避免 4 KiB 分支范围限制。
            const auto skip = prefix_ + "skip_" + std::to_string(branchCount_++);
            line(std::string(i.kind == IROp::BranchZero ? "bnez" : "beqz") + " t0, " + skip);
            line("j " + label(i.label));
            out_ << skip << ":\n";
            break;
        }
        case IROp::Call: {
            const auto extra = align16(8LL * (i.args.size() > 8 ? i.args.size() - 8 : 0));
            adjustStack(extra, true);
            // 所有实参已在 IR 阶段求值并保存在当前栈帧，嵌套调用不会覆盖它们。
            for (std::size_t n = 0; n < i.args.size(); ++n) {
                if (n < 8) load(i.args[n], "a" + std::to_string(n));
                else {
                    load(i.args[n], "t0");
                    memory("sd", "t0", "sp", 8LL * (n - 8));
                }
            }
            line("call " + i.op);
            adjustStack(extra, false);
            if (i.dest >= 0) store(i.dest, "a0");
            break;
        }
        case IROp::Return:
            load(i.a, "a0");
            line("j " + prefix_ + "return");
            break;
        }
    }

    std::size_t branchCount_ = 0;
    void adjustStack(std::int64_t bytes, bool allocate) {
        if (!bytes) return;
        line("li t6, " + std::to_string(bytes));
        line(std::string(allocate ? "sub" : "add") + " sp, sp, t6");
    }
    void binary(const std::string& op) {
        if (op == "+") line("addw t2, t0, t1");
        else if (op == "-") line("subw t2, t0, t1");
        else if (op == "*") line("mulw t2, t0, t1");
        else if (op == "/") line("divw t2, t0, t1");
        else if (op == "%") line("remw t2, t0, t1");
        else if (op == "<") line("slt t2, t0, t1");
        else if (op == ">") line("slt t2, t1, t0");
        else if (op == "<=") { line("slt t2, t1, t0"); line("xori t2, t2, 1"); }
        else if (op == ">=") { line("slt t2, t0, t1"); line("xori t2, t2, 1"); }
        else if (op == "==") { line("xor t2, t0, t1"); line("seqz t2, t2"); }
        else if (op == "!=") { line("xor t2, t0, t1"); line("snez t2, t2"); }
        else throw std::logic_error("unknown binary operation");
    }
};
} // namespace

void generateAssembly(const IRProgram& program, std::ostream& out) {
    out << ".text\n";
    Codegen generator(out);
    for (std::size_t n = 0; n < program.functions.size(); ++n) generator.function(program.functions[n], n);
    out << "\n.section .note.GNU-stack,\"\",@progbits\n";
}
