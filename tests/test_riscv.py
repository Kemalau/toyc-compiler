"""可选端到端检查：真正汇编、链接并模拟执行生成的 RV64 机器码。"""
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

try:
    from riscv_runner import assemble, execute
except ImportError as error:
    raise SystemExit("RISC-V tests need optional dependencies: "
                     "pip install -r tests/requirements.txt\n" + str(error))

COMPILER = str(pathlib.Path(sys.argv.pop(1)).resolve())
ROOT = pathlib.Path(__file__).resolve().parent.parent


class RiscVTests(unittest.TestCase):
    def compile(self, source, opt):
        result = subprocess.run([COMPILER, "-S", opt], input=source,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def check_program(self, source, expected, output="", values=(), chars=""):
        for opt in ("-O0", "-O1"):
            with self.subTest(opt=opt):
                image = assemble(self.compile(source, opt))
                self.assertEqual(execute(image, values, chars), (expected, output))

    def test_arithmetic_and_signed_division(self):
        self.check_program("int main(){int a=20;return (a+3)*2-(-17/5)*10+(-17%5);}", 74)

    def test_comparisons_and_unary(self):
        self.check_program("int main(){int a=-3;int b=2;"
                           "return (a<b)+(a<=b)+(b>a)+(b>=a)+(a!=b)+(a==a)+(!0)+(+b);}", 9)

    def test_relational_grammar_precedence(self):
        # ToyC 修订文法将六种比较运算放在同一层，按左结合解析。
        self.check_program("int main(){return 2==2<2;}", 1)

    def test_shadowing(self):
        self.check_program("int main(){int a=3;{int a=8;putint(a);}return a;}", 3, "8")

    def test_loops_break_continue(self):
        self.check_program("int main(){int i=0;int s=0;while(i<10){i=i+1;"
                           "if(i==2)continue;if(i==6)break;s=s+i;}return s;}", 13)

    def test_nested_loops(self):
        self.check_program("int main(){int i=0;int s=0;while(i<3){int j=0;"
                           "i=i+1;while(j<4){j=j+1;if(j==2)continue;"
                           "if(j==4)break;s=s+1;}}return s;}", 6)

    def test_recursion_and_nested_calls(self):
        self.check_program("int f(int n){if(n<2)return n;return f(n-1)+f(n-2);}"
                           "int add(int a,int b){return a+b;}"
                           "int main(){return add(f(8),add(f(5),f(6)));}", 34)

    def test_short_circuit_and_side_effects(self):
        source = ("int ping(int n){putint(n);return n;}int main(){int a=getint();"
                  "int b=(a&&ping(7))||ping(8);return b;}")
        self.check_program(source, 1, "8", values=(0,))
        self.check_program(source, 1, "7", values=(1,))
        self.check_program("int main(){return (0&&(1/0))+(1||(1/0));}", 1)

    def test_dangling_else(self):
        self.check_program("int main(){int a=0;if(1)if(0)a=1;else a=2;return a;}", 2)

    def test_integer_limits(self):
        self.check_program("int main(){return -2147483648;}", -2147483648)
        self.check_program("int main(){return 2147483647;}", 2147483647)

    def test_io_and_void_return(self):
        self.check_program("void show(int x){putint(x);putch(10);return;}"
                           "int main(){int a=getint();show(a*2);return getch();}",
                           65, "-14\n", values=(-7,), chars="A")

    def test_large_stack_frame(self):
        declarations = "".join(f"int x{i}={i};" for i in range(300))
        self.check_program("int main(){" + declarations + "return x299+x0;}", 299)

    def test_many_arguments_and_stack_offsets(self):
        params = ",".join(f"int a{i}" for i in range(260))
        args = ",".join(str(i) for i in range(260))
        self.check_program(f"int f({params}){{return a0+a8+a259;}}"
                           f"int main(){{return f({args});}}", 267)

    def test_branch_beyond_4k(self):
        body = "a=a+1;" * 600
        source = "int main(){int a=0;if(getint()){" + body + "}return a;}"
        self.check_program(source, 0, values=(0,))
        self.check_program(source, 600, values=(1,))

    def test_independent_c_calling_convention_both_directions(self):
        params = ",".join(f"int a{i}" for i in range(10))
        args = ",".join(str(i + 1) for i in range(10))
        body = "return a0+a1+a2+a3+a4+a5+a6+a7+a8*10+a9*100;"
        source = f"int f({params}){{{body}}}int main(){{return f({args});}}"
        for opt in ("-O0", "-O1"):
            assembly = self.compile(source, opt)
            for replaced in ("f", "main"):
                with self.subTest(opt=opt, replaced=replaced):
                    # 替换一端为独立 C 编译器生成的代码，避免两端犯同样的 ABI 错误。
                    stripped = re.sub(rf"(?ms)^{replaced}:.*?^\.size {replaced}, \.\-{replaced}\n",
                                      "", assembly)
                    helper = (f"int f({params}){{{body}}}" if replaced == "f" else
                              f"extern int f({params});int main(void){{return f({args});}}")
                    self.assertEqual(execute(assemble(stripped, helper)), (1126, ""))

    def test_course_sample(self):
        self.check_program((ROOT / "examples/course_sample.c").read_text(), 1,
                           "161514131211109876432")

    def test_functions_example(self):
        self.check_program((ROOT / "examples/functions.c").read_text(), 0, "120\n8\n")

    def test_native_c_differential(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler not available")
        rng = random.Random(20261108)
        statements = []
        for _ in range(30):
            a, b, c = (rng.randint(-20, 20) for _ in range(3))
            divisor = rng.randint(1, 9)
            statements.append(f"putint((({a})+({b}))*({c})/{divisor});putch(10);")
        source = "int main(){" + "".join(statements) + "return 0;}"
        with tempfile.TemporaryDirectory(prefix="toyc-native-") as directory:
            directory = pathlib.Path(directory)
            c_file = directory / "reference.c"
            c_file.write_text("extern void putint(int);extern void putch(int);\n" + source)
            executable = directory / "reference"
            subprocess.run([compiler, "-O0", str(c_file), str(ROOT / "runtime/io.c"),
                            "-o", str(executable)], check=True, capture_output=True)
            reference = subprocess.run([str(executable)], check=True, text=True, capture_output=True)
            self.check_program(source, 0, reference.stdout)


if __name__ == "__main__":
    unittest.main()
