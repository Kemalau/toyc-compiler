#include "semantic.h"
#include <unordered_map>

namespace {
struct Variable { int id; bool isConst; };
struct Signature { ValueType type; std::size_t arity; };

class Semantic {
public:
    void run(Program& program) {
        // 截图用到了 putint；这里提供常见的四个标量运行库接口。
        functions_ = {{"getint", {ValueType::Int, 0}}, {"getch", {ValueType::Int, 0}},
                      {"putint", {ValueType::Void, 1}}, {"putch", {ValueType::Void, 1}}};
        bool hasMain = false;
        for (auto& f : program.functions) {
            if (functions_.count(f.name.text)) error(f.name, "duplicate function '" + f.name.text + "'");
            // 当前函数先登记，因此支持递归；后定义的函数尚不可见。
            functions_[f.name.text] = {f.returnType, f.params.size()};
            if (f.name.text == "main") {
                hasMain = true;
                if (f.returnType != ValueType::Int || !f.params.empty())
                    error(f.name, "main must have signature int main()");
            }
            scopes_.clear();
            scopes_.emplace_back();
            nextSymbol_ = 0;
            loopDepth_ = 0;
            returnType_ = f.returnType;
            for (auto& param : f.params) param.symbol = declare(param.at.text, false, param.at);
            // 参数与最外层函数体处于同一作用域。
            block(*f.body, false);
            f.localCount = nextSymbol_;
        }
        if (!hasMain) {
            const Token at = program.functions.empty() ? Token{} : program.functions.back().name;
            error(at, "missing int main() entry point");
        }
    }

private:
    std::unordered_map<std::string, Signature> functions_;
    std::vector<std::unordered_map<std::string, Variable>> scopes_;
    int nextSymbol_ = 0;
    int loopDepth_ = 0;
    ValueType returnType_ = ValueType::Int;

    [[noreturn]] static void error(const Token& at, const std::string& message) {
        compileError("semantic", at, message);
    }
    const Variable* lookup(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }
    int declare(const std::string& name, bool isConst, const Token& at) {
        auto& scope = scopes_.back();
        if (scope.count(name)) error(at, "duplicate variable '" + name + "'");
        const int id = nextSymbol_++;
        scope.emplace(name, Variable{id, isConst});
        return id;
    }
    static void requireInt(ValueType type, const Token& at) {
        if (type != ValueType::Int) error(at, "void expression cannot be used as an integer");
    }

    ValueType expression(Expr& e, bool allowMinMagnitude = false) {
        e.type = ValueType::Int;
        switch (e.kind) {
        case Expr::Kind::Number:
            if (e.number > 2147483647LL && !allowMinMagnitude)
                error(e.at, "integer literal exceeds signed 32-bit range");
            break;
        case Expr::Kind::Variable: {
            const auto* var = lookup(e.text);
            if (!var) error(e.at, "undeclared variable '" + e.text + "'");
            e.symbol = var->id;
            break;
        }
        case Expr::Kind::Unary:
            requireInt(expression(*e.left, e.text == "-" && e.left->kind == Expr::Kind::Number), e.at);
            break;
        case Expr::Kind::Binary:
            requireInt(expression(*e.left), e.left->at);
            requireInt(expression(*e.right), e.right->at);
            break;
        case Expr::Kind::Call: {
            if (lookup(e.text)) error(e.at, "called name '" + e.text + "' is a local variable");
            const auto found = functions_.find(e.text);
            if (found == functions_.end()) error(e.at, "function '" + e.text + "' is not yet defined");
            if (found->second.arity != e.args.size()) error(e.at, "wrong argument count for '" + e.text + "'");
            for (auto& arg : e.args) requireInt(expression(*arg), arg->at);
            e.type = found->second.type;
            break;
        }
        }
        return e.type;
    }

    void block(Stmt& s, bool newScope) {
        if (newScope) scopes_.emplace_back();
        for (auto& child : s.statements) statement(*child);
        if (newScope) scopes_.pop_back();
    }

    void statement(Stmt& s) {
        switch (s.kind) {
        case Stmt::Kind::Block: block(s, true); break;
        case Stmt::Kind::Empty: break;
        case Stmt::Kind::Declaration:
            s.symbol = declare(s.name, s.isConst, s.at);
            if (s.expr) requireInt(expression(*s.expr), s.expr->at);
            break;
        case Stmt::Kind::Assignment: {
            const auto* var = lookup(s.name);
            if (!var) error(s.at, "undeclared variable '" + s.name + "'");
            if (var->isConst) error(s.at, "cannot assign to const variable '" + s.name + "'");
            s.symbol = var->id;
            requireInt(expression(*s.expr), s.expr->at);
            break;
        }
        case Stmt::Kind::Expression: expression(*s.expr); break;
        case Stmt::Kind::If:
            requireInt(expression(*s.expr), s.expr->at);
            statement(*s.first);
            if (s.second) statement(*s.second);
            break;
        case Stmt::Kind::While:
            requireInt(expression(*s.expr), s.expr->at);
            ++loopDepth_;
            statement(*s.first);
            --loopDepth_;
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            if (loopDepth_ == 0) error(s.at, "break/continue is only allowed inside a loop");
            break;
        case Stmt::Kind::Return:
            if (returnType_ == ValueType::Void && s.expr)
                error(s.at, "void function cannot return a value");
            if (returnType_ == ValueType::Int && !s.expr)
                error(s.at, "int function must return an integer value");
            if (s.expr) requireInt(expression(*s.expr), s.expr->at);
            break;
        }
    }
};
} // namespace

void analyze(Program& program) { Semantic().run(program); }
