#include "ir.h"
#include <limits>
#include <utility>

namespace {
class IRBuilder {
public:
    IRFunction build(const Function& function) {
        current_.name = function.name.text;
        current_.returnType = function.returnType;
        current_.localCount = function.localCount;
        current_.registerCount = function.localCount;
        for (const auto& param : function.params) current_.params.push_back(param.symbol);
        statement(*function.body);
        // main 未显式 return 时返回 0。其他函数落到末尾同样提供保底出口。
        emitReturn(Operand::constant(0));
        return std::move(current_);
    }

private:
    IRFunction current_;
    int nextLabel_ = 0;
    std::vector<std::pair<int, int>> loops_; // continue 目标、break 目标

    int temp() { return current_.registerCount++; }
    int label() { return nextLabel_++; }
    void emit(Instruction ins) { current_.code.push_back(std::move(ins)); }
    void mark(int target) { Instruction i(IROp::Label); i.label = target; emit(std::move(i)); }
    void jump(int target) { Instruction i(IROp::Jump); i.label = target; emit(std::move(i)); }
    void branch(Operand condition, int target, bool nonzero = false) {
        Instruction i(nonzero ? IROp::BranchNonzero : IROp::BranchZero);
        i.a = condition; i.label = target; emit(std::move(i));
    }
    void move(int dest, Operand value) {
        Instruction i(IROp::Move); i.dest = dest; i.a = value; emit(std::move(i));
    }
    void emitReturn(Operand value) { Instruction i(IROp::Return); i.a = value; emit(std::move(i)); }
    Operand unary(const std::string& op, Operand value, int dest = -1) {
        Instruction i(IROp::Unary);
        i.dest = dest < 0 ? temp() : dest;
        i.op = op; i.a = value;
        const auto result = Operand::reg(i.dest);
        emit(std::move(i));
        return result;
    }

    Operand expression(const Expr& e) {
        switch (e.kind) {
        case Expr::Kind::Number:
            // 2147483648 只可能来自已核验的一元负号。
            return Operand::constant(e.number == 2147483648LL
                ? std::numeric_limits<std::int32_t>::min() : static_cast<std::int32_t>(e.number));
        case Expr::Kind::Variable: return Operand::reg(e.symbol);
        case Expr::Kind::Unary:
            if (e.text == "-" && e.left->kind == Expr::Kind::Number && e.left->number == 2147483648LL)
                return Operand::constant(std::numeric_limits<std::int32_t>::min());
            return unary(e.text, expression(*e.left));
        case Expr::Kind::Binary: {
            // && / || 必须生成控制流，不能提前计算右操作数。
            if (e.text == "&&" || e.text == "||") {
                const int result = temp();
                const int done = label();
                move(result, Operand::constant(e.text == "||" ? 1 : 0));
                const auto left = expression(*e.left);
                branch(left, done, e.text == "||");
                const auto right = expression(*e.right);
                unary("bool", right, result);
                mark(done);
                return Operand::reg(result);
            }
            const auto left = expression(*e.left);
            const auto right = expression(*e.right);
            Instruction i(IROp::Binary);
            i.dest = temp(); i.op = e.text; i.a = left; i.b = right;
            const auto result = Operand::reg(i.dest);
            emit(std::move(i));
            return result;
        }
        case Expr::Kind::Call: {
            Instruction i(IROp::Call);
            i.op = e.text;
            for (const auto& arg : e.args) i.args.push_back(expression(*arg));
            if (e.type == ValueType::Int) i.dest = temp();
            const auto result = i.dest < 0 ? Operand::constant(0) : Operand::reg(i.dest);
            emit(std::move(i));
            return result;
        }
        }
        throw std::logic_error("unknown expression kind");
    }

    void statement(const Stmt& s) {
        switch (s.kind) {
        case Stmt::Kind::Block:
            for (const auto& child : s.statements) statement(*child);
            break;
        case Stmt::Kind::Empty: break;
        case Stmt::Kind::Declaration:
        case Stmt::Kind::Assignment:
            if (s.expr) move(s.symbol, expression(*s.expr));
            break;
        case Stmt::Kind::Expression: expression(*s.expr); break;
        case Stmt::Kind::If: {
            const int otherwise = label();
            const int done = label();
            branch(expression(*s.expr), otherwise);
            statement(*s.first);
            jump(done);
            mark(otherwise);
            if (s.second) statement(*s.second);
            mark(done);
            break;
        }
        case Stmt::Kind::While: {
            const int condition = label();
            const int done = label();
            mark(condition);
            branch(expression(*s.expr), done);
            loops_.emplace_back(condition, done);
            statement(*s.first);
            loops_.pop_back();
            jump(condition);
            mark(done);
            break;
        }
        case Stmt::Kind::Break: jump(loops_.back().second); break;
        case Stmt::Kind::Continue: jump(loops_.back().first); break;
        case Stmt::Kind::Return:
            emitReturn(s.expr ? expression(*s.expr) : Operand::constant(0));
            break;
        }
    }
};

std::string operand(Operand value) {
    return (value.immediate ? "" : "%") + std::to_string(value.value);
}
} // namespace

IRProgram generateIR(const Program& program) {
    IRProgram result;
    for (const auto& function : program.functions)
        result.functions.push_back(IRBuilder().build(function));
    return result;
}

void printIR(const IRProgram& program, std::ostream& out) {
    for (const auto& function : program.functions) {
        out << "function " << function.name << '(';
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            if (i) out << ", ";
            out << '%' << function.params[i];
        }
        out << ") " << (function.returnType == ValueType::Int ? "int" : "void") << " {\n";
        for (const auto& i : function.code) {
            if (i.kind != IROp::Label) out << "  ";
            switch (i.kind) {
            case IROp::Move: out << '%' << i.dest << " = " << operand(i.a); break;
            case IROp::Unary: out << '%' << i.dest << " = " << i.op << ' ' << operand(i.a); break;
            case IROp::Binary: out << '%' << i.dest << " = " << operand(i.a) << ' ' << i.op << ' ' << operand(i.b); break;
            case IROp::Label: out << 'L' << i.label << ':'; break;
            case IROp::Jump: out << "jump L" << i.label; break;
            case IROp::BranchZero: out << "ifzero " << operand(i.a) << " jump L" << i.label; break;
            case IROp::BranchNonzero: out << "ifnonzero " << operand(i.a) << " jump L" << i.label; break;
            case IROp::Call:
                if (i.dest >= 0) out << '%' << i.dest << " = ";
                out << "call " << i.op << '(';
                for (std::size_t n = 0; n < i.args.size(); ++n) {
                    if (n) out << ", ";
                    out << operand(i.args[n]);
                }
                out << ')';
                break;
            case IROp::Return: out << "return " << operand(i.a); break;
            }
            out << '\n';
        }
        out << "}\n";
    }
}
