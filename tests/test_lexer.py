"""检查题目格式、最长匹配、注释边界以及词法错误。"""

import pathlib
import random
import subprocess
import sys
import unittest


COMPILER = str(pathlib.Path(sys.argv.pop(1)).resolve())
ROOT = pathlib.Path(__file__).resolve().parent.parent


class LexerTests(unittest.TestCase):
    def run_lexer(self, source, args=("-token",)):
        return subprocess.run(
            [COMPILER, *args], input=source, capture_output=True, text=True
        )

    def check_tokens(self, source, expected):
        result = self.run_lexer(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, "".join(
            f"{i}:{token}\n" for i, token in enumerate(expected)
        ))

    def test_exact_format_and_negative_number(self):
        self.check_tokens("int a = -1;", [
            "'int':\"int\"", 'Ident:"a"', "'=':\"=\"",
            "'-':\"-\"", "IntConst:1", "';':\";\"",
        ])

    def test_keywords_and_identifier_boundaries(self):
        keywords = "int void const if else while break continue return".split()
        identifiers = "integer ifx _a A9 main putint".split()
        self.check_tokens(" ".join(keywords + identifiers),
                          [f"'{word}':\"{word}\"" for word in keywords] +
                          [f'Ident:"{word}"' for word in identifiers])

    def test_all_symbols_and_longest_match(self):
        symbols = "( ) { } ; , + - * / % < > <= >= == != = ! && ||".split()
        self.check_tokens(" ".join(symbols),
                          [f"'{symbol}':\"{symbol}\"" for symbol in symbols])
        self.check_tokens("a<=b!=0&&c||!d", [
            'Ident:"a"', "'<=':\"<=\"", 'Ident:"b"', "'!=':\"!=\"",
            "IntConst:0", "'&&':\"&&\"", 'Ident:"c"', "'||':\"||\"",
            "'!':\"!\"", 'Ident:"d"',
        ])

    def test_comments_and_division(self):
        self.check_tokens("a/* ignored // 中文 */ / b // tail\r\n+1", [
            'Ident:"a"', "'/':\"/\"", 'Ident:"b"', "'+':\"+\"", "IntConst:1",
        ])
        self.check_tokens("a/**/b", ['Ident:"a"', 'Ident:"b"'])
        self.check_tokens("/* outer /* inner */x", ['Ident:"x"'])

    def test_empty_input_and_comment_at_eof(self):
        for source in ("", " \t\r\n\v\f", "// no newline", "/**/"):
            with self.subTest(source=source):
                self.check_tokens(source, [])

    def test_decimal_boundaries_and_repeated_minus(self):
        self.check_tokens("0 12 2147483648 012 a--1", [
            "IntConst:0", "IntConst:12", "IntConst:2147483648",
            "IntConst:0", "IntConst:12", 'Ident:"a"',
            "'-':\"-\"", "'-':\"-\"", "IntConst:1",
        ])

    def test_invalid_characters(self):
        for source in ("@", "a & b", "a | b", "int x;\x00", '"hello"'):
            with self.subTest(source=source):
                result = self.run_lexer(source)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn("lexical error at", result.stderr)

    def test_unterminated_comment_and_position(self):
        result = self.run_lexer("int a;\n  /* missing end")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("2:3: unterminated block comment", result.stderr)

    def test_course_sample_landmarks(self):
        # 截图给出了 208 个 token，核对数量及关键编号。
        result = self.run_lexer((ROOT / "examples/course_sample.c").read_text())
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), 208)
        expected = {
            0: "'int':\"int\"", 1: 'Ident:"sum"',
            55: "'void':\"void\"", 69: "'int':\"int\"",
            96: "'+':\"+\"", 98: "'-':\"-\"", 99: "'-':\"-\"",
            100: "IntConst:1", 135: "'&&':\"&&\"",
            139: "'||':\"||\"", 207: "'}':\"}\"",
        }
        for index, token in expected.items():
            self.assertEqual(lines[index], f"{index}:{token}")

    def test_command_line(self):
        result = self.run_lexer("int", args=())
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "0:'int':\"int\"\n")
        result = self.run_lexer("", args=("--unknown",))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("Usage:", result.stderr)

    def test_keywords_are_case_sensitive(self):
        words = ["Int", "WHILE", "Return", "int_value", "if1", "_x9", "main"]
        self.check_tokens(" ".join(words), [f'Ident:"{word}"' for word in words])

    def test_token_mode_does_not_require_valid_syntax(self):
        # 本作业只做词法识别，不能因缺少 main、未声明名字或括号未配对而拒绝输入。
        self.check_tokens("return unknown + (", [
            "'return':\"return\"", 'Ident:"unknown"', "'+':\"+\"", "'(':\"(\"",
        ])

    def test_long_identifier_and_integer(self):
        name = "_" + "abc123" * 700
        number = "9" * 4096
        self.check_tokens(name + " " + number, [f'Ident:"{name}"', f"IntConst:{number}"])

    def test_generated_token_stream(self):
        # 直接从已知 token 构造输入和期望值，避免以编译器自身的输出作为答案。
        rng = random.Random(20261108)
        keywords = "int void const if else while break continue return".split()
        symbols = "( ) { } ; , + - * / % < > <= >= == != = ! && ||".split()
        choices = [(word, f"'{word}':\"{word}\"") for word in keywords + symbols]
        choices += [(word, f'Ident:"{word}"') for word in ("main", "putint", "_x9", "integer", "IF")]
        choices += [(number, f"IntConst:{number}") for number in ("0", "1", "2147483648", "98765432101234567890")]
        separators = [" ", "\t", "\n", "\r\n", "/**/", "/* 注释 * / // */", "// comment\n"]
        selected = [rng.choice(choices) for _ in range(2000)]
        # 分隔符前加空格，避免 '/' 与紧随其后的注释起始符组合成新的注释。
        source = "".join(text + " " + rng.choice(separators) + " " for text, _ in selected)
        self.check_tokens(source, [expected for _, expected in selected])


if __name__ == "__main__":
    unittest.main()
