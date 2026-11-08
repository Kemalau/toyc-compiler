#include "codegen.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "semantic.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace {
enum class Mode { Tokens, Ast, IR, Assembly, Check };
void usage(const char* command) {
    std::cerr << "Usage: " << command
              << " [-token | --ast | --ir | -S | --check] [-O0 | -O1]"
                 " [input.c] [-o output]\n";
}
} // namespace

int main(int argc, char* argv[]) {
    Mode mode = Mode::Tokens; // 保留最初词法作业的默认行为。
    bool modeSet = false;
    bool optimized = false;
    std::string inputPath;
    std::string outputPath;
    for (int n = 1; n < argc; ++n) {
        const std::string arg = argv[n];
        Mode requested = mode;
        if (arg == "-token") requested = Mode::Tokens;
        else if (arg == "--ast") requested = Mode::Ast;
        else if (arg == "--ir") requested = Mode::IR;
        else if (arg == "-S") requested = Mode::Assembly;
        else if (arg == "--check") requested = Mode::Check;
        else if (arg == "-O0" || arg == "-O1") {
            optimized = arg == "-O1";
            continue;
        } else if (arg == "-o") {
            if (++n >= argc || !outputPath.empty()) { usage(argv[0]); return 2; }
            outputPath = argv[n];
            continue;
        } else if (!arg.empty() && arg[0] != '-' && inputPath.empty()) {
            inputPath = arg;
            continue;
        } else { usage(argv[0]); return 2; }
        if (modeSet && requested != mode) { usage(argv[0]); return 2; }
        mode = requested;
        modeSet = true;
    }

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    try {
        std::ifstream file;
        std::istream* input = &std::cin;
        if (!inputPath.empty()) {
            file.open(inputPath, std::ios::binary);
            if (!file) throw std::runtime_error("cannot open input: " + inputPath);
            input = &file;
        }
        const std::string source((std::istreambuf_iterator<char>(*input)),
                                  std::istreambuf_iterator<char>());
        if (input->bad()) throw std::runtime_error("failed to read input");
        const auto tokens = tokenize(source);
        // 全部成功后再写输出文件，出错时保留原有输出文件。
        std::ostringstream result;
        if (mode == Mode::Tokens) {
            printTokens(tokens, result);
        } else {
            auto program = parse(tokens);
            analyze(program);
            if (mode == Mode::Ast) printAst(program, result);
            else if (mode != Mode::Check) {
                auto ir = generateIR(program);
                if (optimized) optimize(ir);
                if (mode == Mode::IR) printIR(ir, result);
                else generateAssembly(ir, result);
            }
        }
        if (outputPath.empty()) {
            std::cout << result.str();
            if (!std::cout) throw std::runtime_error("failed to write standard output");
        } else {
            std::ofstream output(outputPath, std::ios::binary);
            output << result.str();
            output.close();
            if (!output) throw std::runtime_error("failed to write output: " + outputPath);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
