#include "lexer.h"
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {
// 手写扫描器：pos_ 指向下一个尚未处理的字符。
class LexerImpl {
public:
    explicit LexerImpl(std::string source) : source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (true) {
            skipWhitespaceAndComments();
            if (atEnd()) {
                return tokens;
            }

            const auto tokenLine = line_;
            const auto tokenColumn = column_;
            const char c = peek();
            if (isIdentifierStart(c)) {
                tokens.push_back(readIdentifier());
            } else if (isDigit(c)) {
                tokens.push_back(readNumber());
            } else {
                tokens.push_back(readSymbol());
            }
            tokens.back().line = tokenLine;
            tokens.back().column = tokenColumn;
        }
    }

private:
    std::string source_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    bool atEnd() const { return pos_ == source_.size(); }

    char peek(std::size_t offset = 0) const {
        return offset < source_.size() - pos_ ? source_[pos_ + offset] : '\0';
    }

    char advance() {
        const char c = source_[pos_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    static bool isIdentifierStart(char c) {
        return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    static bool isIdentifierPart(char c) {
        return isIdentifierStart(c) || isDigit(c);
    }

    static bool isWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\f' || c == '\v';
    }

    [[noreturn]] static void fail(std::size_t line, std::size_t column,
                                  const std::string& message) {
        throw std::runtime_error("lexical error at " + std::to_string(line) +
                                 ":" + std::to_string(column) + ": " + message);
    }

    void skipWhitespaceAndComments() {
        while (!atEnd()) {
            if (isWhitespace(peek())) {
                advance();
            } else if (peek() == '/' && peek(1) == '/') {
                // 单行注释可以在换行或文件末尾结束。
                advance();
                advance();
                while (!atEnd() && peek() != '\n' && peek() != '\r') {
                    advance();
                }
            } else if (peek() == '/' && peek(1) == '*') {
                const auto startLine = line_;
                const auto startColumn = column_;
                advance();
                advance();
                // 块注释在最近的 */ 结束，不支持嵌套。
                while (!atEnd() && !(peek() == '*' && peek(1) == '/')) {
                    advance();
                }
                if (atEnd()) {
                    fail(startLine, startColumn, "unterminated block comment");
                }
                advance();
                advance();
            } else {
                return;
            }
        }
    }

    Token readIdentifier() {
        static const std::unordered_set<std::string> keywords = {
            "int", "void", "const", "if", "else", "while",
            "break", "continue", "return"
        };
        const auto start = pos_;
        while (!atEnd() && isIdentifierPart(peek())) {
            advance();
        }
        const std::string text = source_.substr(start, pos_ - start);
        // 先读完整个单词再判断，避免把 integer 拆成 int 和 eger。
        return {keywords.count(text) ? "'" + text + "'" : "Ident", text};
    }

    Token readNumber() {
        const auto start = pos_;
        // 按 0|[1-9][0-9]* 匹配；负号单独作为 token。
        if (peek() == '0') {
            advance();
        } else {
            while (!atEnd() && isDigit(peek())) {
                advance();
            }
        }
        // 保留数字文本，避免在词法阶段做整数转换而溢出。
        return {"IntConst", source_.substr(start, pos_ - start)};
    }

    Token readSymbol() {
        static const std::unordered_set<std::string> doubleSymbols = {
            "<=", ">=", "==", "!=", "&&", "||"
        };
        static const std::string singleSymbols = "(){};,+-*/%<>=!";

        // 最长匹配：例如 >= 应读作一个 token，而不是 > 和 =。
        const std::string pair = source_.substr(pos_, 2);
        if (doubleSymbols.count(pair)) {
            advance();
            advance();
            return {"'" + pair + "'", pair};
        }
        if (singleSymbols.find(peek()) != std::string::npos) {
            const std::string text(1, advance());
            return {"'" + text + "'", text};
        }
        fail(line_, column_, "unexpected byte " +
             std::to_string(static_cast<unsigned char>(peek())));
    }
};


} // namespace

std::vector<Token> tokenize(std::string source) {
    return LexerImpl(std::move(source)).scan();
}

void printTokens(const std::vector<Token>& tokens, std::ostream& out) {
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        out << i << ':' << token.type << ':';
        if (token.type == "IntConst") out << token.text;
        else out << '"' << token.text << '"';
        out << '\n';
    }
}
