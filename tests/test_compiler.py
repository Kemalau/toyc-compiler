"""前端和优化回归检查，无第三方 Python 依赖。"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

COMPILER = str(pathlib.Path(sys.argv.pop(1)).resolve())
ROOT = pathlib.Path(__file__).resolve().parent.parent


class CompilerTests(unittest.TestCase):
    def compile(self, source, *args):
        return subprocess.run([COMPILER, *args], input=source, text=True, capture_output=True)

    def check_ok(self, source, *args):
        result = self.compile(source, *args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        return result.stdout

    def check_error(self, source, stage, message):
        result = self.compile(source, "-S")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn(stage + " error at", result.stderr)
        self.assertIn(message, result.stderr)

    def test_ast_precedence_and_associativity(self):
        tree = self.check_ok("int main(){return 2+3*4-5;}", "--ast")
        self.assertIn("Binary -\n        Binary +\n          Number 2\n          Binary *", tree)
        self.assertIn("Number 5", tree)

    def test_constant_folding(self):
        source = "int main(){int a=2+3*4;return a;}"
        before = self.check_ok(source, "--ir", "-O0")
        after = self.check_ok(source, "--ir", "-O1")
        self.assertIn(" * ", before)
        self.assertIn(" + ", before)
        self.assertIn("return 14", after)
        self.assertNotIn(" * ", after)
        self.assertNotIn(" + ", after)
        assembly = self.check_ok(source, "-S", "-O1")
        self.assertIn("li a0, 14", assembly)
        self.assertNotIn("addw", assembly)

    def test_do_not_fold_division_by_zero(self):
        text = self.check_ok("int main(){return 1/0;}", "--ir", "-O1")
        self.assertIn(" / ", text)

    def test_unreachable_and_side_effects(self):
        text = self.check_ok("int main(){if(0)putint(1);putint(2);return 0;putint(3);}", "--ir", "-O1")
        self.assertNotIn("call putint(1)", text)
        self.assertIn("call putint(2)", text)
        self.assertNotIn("call putint(3)", text)

    def test_integer_limits(self):
        self.check_ok("int main(){return -2147483648;}", "-S", "-O1")
        self.check_error("int main(){return 2147483648;}", "semantic", "32-bit")
        self.check_error("int main(){return 999999999999999999999;}", "syntax", "32-bit")

    def test_syntax_errors(self):
        for source in ("int main(){return 1}", "int main(){", "int main(){int a=;}",
                       "int main(){return 1 2;}", "int global;"):
            with self.subTest(source=source):
                self.check_error(source, "syntax", "expected")

    def test_variable_checks(self):
        cases = [
            ("int main(){return x;}", "undeclared variable"),
            ("int main(){int x; int x;return 0;}", "duplicate variable"),
            ("int main(){const int x=1;x=2;return x;}", "const variable"),
            ("int main(){{int x=1;}return x;}", "undeclared variable"),
            ("int f(int x){int x=2;return x;}int main(){return f(1);}", "duplicate variable"),
        ]
        for source, message in cases:
            with self.subTest(source=source):
                self.check_error(source, "semantic", message)

    def test_function_checks(self):
        cases = [
            ("int f(){return 0;}", "missing int main"),
            ("void main(){}", "int main()"),
            ("int main(int x){return x;}", "int main()"),
            ("int main(){return f();}int f(){return 1;}", "not yet defined"),
            ("int f(int a){return a;}int main(){return f();}", "argument count"),
            ("int main(){return putint(1);}", "void expression"),
            ("void f(){return 1;}int main(){return 0;}", "void function"),
            ("int main(){return;}", "int function"),
            ("int main(){int putint=1;putint(2);return 0;}", "local variable"),
            ("int main(){return 0;}int main(){return 1;}", "duplicate function"),
        ]
        for source, message in cases:
            with self.subTest(source=source):
                self.check_error(source, "semantic", message)

    def test_loop_checks(self):
        for keyword in ("break", "continue"):
            self.check_error(f"int main(){{{keyword};return 0;}}", "semantic", "inside a loop")

    def test_course_sample_all_modes(self):
        source = (ROOT / "examples/course_sample.c").read_text()
        for mode in ("--check", "--ast", "--ir", "-S"):
            for opt in ("-O0", "-O1"):
                with self.subTest(mode=mode, opt=opt):
                    self.check_ok(source, mode, opt)

    def test_large_stack_offsets(self):
        declarations = "".join(f"int x{i}={i};" for i in range(300))
        asm = self.check_ok("int main(){" + declarations + "return x299;}", "-S")
        self.assertIn("li t6, -2416", asm)
        self.assertIn("lw a0, 0(t6)", asm)

    def test_file_options_and_preserve_output_on_error(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "input.c"
            output = pathlib.Path(directory) / "output.s"
            source.write_text("int main(){return 5;}")
            result = subprocess.run([COMPILER, "-S", str(source), "-o", str(output)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertIn("main:", output.read_text())
            output.write_text("previous result")
            source.write_text("int main(){return missing;}")
            result = subprocess.run([COMPILER, "-S", str(source), "-o", str(output)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(output.read_text(), "previous result")


if __name__ == "__main__":
    unittest.main()
