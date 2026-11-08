#include "parser.h"
#include <algorithm>
#include <utility>

namespace {
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {
        if (!tokens.empty()) {
            end_.line = tokens.back().line;
            end_.column = tokens.back().column + tokens.back().text.size();
        }
    }

    Program program() {
        Program result;
        while (!atEnd()) result.functions.push_back(function());
        return result;
    }

private:
    const std::vector<Token>& tokens_;
    std::size_t pos_ = 0;
    Token end_{"End", "<end of input>"};

    bool atEnd() const { return pos_ == tokens_.size(); }
    const Token& peek(std::size_t ahead = 0) const {
        return ahead < tokens_.size() - pos_ ? tokens_[pos_ + ahead] : end_;
    }
    bool is(const std::string& text) const { return !atEnd() && peek().text == text; }
    Token take() {
        if (atEnd()) compileError("syntax", end_, "unexpected end of input");
        return tokens_[pos_++];
    }
    bool match(const std::string& text) {
        if (!is(text)) return false;
        ++pos_;
        return true;
    }
    Token expect(const std::string& text) {
        if (!is(text)) compileError("syntax", peek(), "expected '" + text + "'");
        return take();
    }
    Token identifier() {
        if (peek().type != "Ident") compileError("syntax", peek(), "expected identifier");
        return take();
    }

    Function function() {
        Function result;
        if (match("int")) result.returnType = ValueType::Int;
        else if (match("void")) result.returnType = ValueType::Void;
        else compileError("syntax", peek(), "expected function definition (int or void)");
        result.name = identifier();
        expect("(");
        if (!is(")")) {
            do {
                expect("int");
                result.params.push_back({identifier(), -1});
            } while (match(","));
        }
        expect(")");
        result.body = block();
        return result;
    }

    std::unique_ptr<Stmt> block() {
        auto result = std::make_unique<Stmt>(Stmt::Kind::Block, expect("{"));
        while (!is("}")) {
            if (atEnd()) compileError("syntax", peek(), "expected '}'");
            result->statements.push_back(statement());
        }
        expect("}");
        return result;
    }

    std::unique_ptr<Stmt> statement() {
        if (is("{")) return block();
        const Token start = peek();
        auto result = std::make_unique<Stmt>(Stmt::Kind::Empty, start);
        if (match(";")) return result;
        if (match("if")) {
            result->kind = Stmt::Kind::If;
            expect("(");
            result->expr = expression();
            expect(")");
            result->first = statement();
            if (match("else")) result->second = statement();
            return result;
        }
        if (match("while")) {
            result->kind = Stmt::Kind::While;
            expect("(");
            result->expr = expression();
            expect(")");
            result->first = statement();
            return result;
        }
        if (match("break")) result->kind = Stmt::Kind::Break;
        else if (match("continue")) result->kind = Stmt::Kind::Continue;
        else if (match("return")) {
            result->kind = Stmt::Kind::Return;
            if (!is(";")) result->expr = expression();
        } else if (is("const") || is("int")) {
            result->kind = Stmt::Kind::Declaration;
            result->isConst = match("const");
            expect("int");
            result->name = identifier().text;
            if (result->isConst) {
                expect("=");
                result->expr = expression();
            } else if (match("=")) result->expr = expression();
        } else if (peek().type == "Ident" && peek(1).text == "=") {
            result->kind = Stmt::Kind::Assignment;
            result->name = take().text;
            expect("=");
            result->expr = expression();
        } else {
            result->kind = Stmt::Kind::Expression;
            result->expr = expression();
        }
        expect(";");
        return result;
    }

    // 每一层对应文法的一个优先级；循环实现左结合。
    std::unique_ptr<Expr> expression(int level = 0) {
        static const std::vector<std::vector<std::string>> operators = {
            {"||"}, {"&&"}, {"<", ">", "<=", ">=", "==", "!="},
            {"+", "-"}, {"*", "/", "%"}
        };
        if (level == static_cast<int>(operators.size())) return unary();
        auto left = expression(level + 1);
        const auto& choices = operators[level];
        while (!atEnd() && std::find(choices.begin(), choices.end(), peek().text) != choices.end()) {
            auto node = std::make_unique<Expr>(Expr::Kind::Binary, take());
            node->left = std::move(left);
            node->right = expression(level + 1);
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Expr> unary() {
        if (is("+") || is("-") || is("!")) {
            auto node = std::make_unique<Expr>(Expr::Kind::Unary, take());
            node->left = unary();
            return node;
        }
        if (match("(")) {
            auto node = expression();
            expect(")");
            return node;
        }
        if (peek().type == "IntConst") {
            auto node = std::make_unique<Expr>(Expr::Kind::Number, take());
            // 允许 2147483648 暂存，留给语义阶段识别 -2147483648。
            for (char c : node->text) {
                node->number = node->number * 10 + (c - '0');
                if (node->number > 2147483648LL)
                    compileError("syntax", node->at, "integer literal exceeds 32-bit range");
            }
            return node;
        }
        if (peek().type == "Ident") {
            auto node = std::make_unique<Expr>(Expr::Kind::Variable, take());
            if (match("(")) {
                node->kind = Expr::Kind::Call;
                if (!is(")")) {
                    do { node->args.push_back(expression()); } while (match(","));
                }
                expect(")");
            }
            return node;
        }
        compileError("syntax", peek(), "expected expression");
    }
};

void dumpExpr(const Expr& e, std::ostream& out, int depth) {
    const std::string pad(depth * 2, ' ');
    switch (e.kind) {
    case Expr::Kind::Number: out << pad << "Number " << e.number << '\n'; break;
    case Expr::Kind::Variable: out << pad << "Variable " << e.text << '\n'; break;
    case Expr::Kind::Call: out << pad << "Call " << e.text << '\n'; break;
    case Expr::Kind::Unary: out << pad << "Unary " << e.text << '\n'; break;
    case Expr::Kind::Binary: out << pad << "Binary " << e.text << '\n'; break;
    }
    if (e.left) dumpExpr(*e.left, out, depth + 1);
    if (e.right) dumpExpr(*e.right, out, depth + 1);
    for (const auto& arg : e.args) dumpExpr(*arg, out, depth + 1);
}

void dumpStmt(const Stmt& s, std::ostream& out, int depth) {
    static const char* names[] = {"Block", "Empty", "Declaration", "Assignment",
        "Expression", "If", "While", "Break", "Continue", "Return"};
    out << std::string(depth * 2, ' ') << names[static_cast<int>(s.kind)];
    if (!s.name.empty()) out << (s.isConst ? " const " : " ") << s.name;
    out << '\n';
    if (s.expr) dumpExpr(*s.expr, out, depth + 1);
    if (s.first) dumpStmt(*s.first, out, depth + 1);
    if (s.second) dumpStmt(*s.second, out, depth + 1);
    for (const auto& child : s.statements) dumpStmt(*child, out, depth + 1);
}
} // namespace

Program parse(const std::vector<Token>& tokens) { return Parser(tokens).program(); }

void printAst(const Program& program, std::ostream& out) {
    for (const auto& function : program.functions) {
        out << "Function " << (function.returnType == ValueType::Int ? "int " : "void ")
            << function.name.text << '\n';
        for (const auto& param : function.params) out << "  Parameter int " << param.at.text << '\n';
        dumpStmt(*function.body, out, 1);
    }
}
