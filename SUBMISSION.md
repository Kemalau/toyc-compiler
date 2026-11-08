# 希冀平台词法分析测评提交说明

本次对应截图中的 **ToyC 词法分析器作业**。默认输出 token；完整编译器的其他模式不影响词法测评。

## 平台填写

- 仓库地址：`https://github.com/Kemalau/toyc-compiler.git`
- 分支：`main`
- 构建：Make 或 CMake，C++17，无需安装第三方运行依赖。
- 程序：`compiler`；调用参数：`-token`。
- 输入：标准输入中的 ToyC 源代码。
- 输出：标准输出中的 token，每行一项；不输出日志、提示语或 EOF token。

仓库为私有仓库。若平台要求带凭据的仓库地址，请在平台按下列格式填写，将占位符替换为自己有本仓库读取权限的访问令牌：

```text
https://Kemalau:<访问令牌>@github.com/Kemalau/toyc-compiler.git main
```

访问令牌由 GitHub 生成，在平台填写；不要将实际令牌提交到仓库。若平台将地址、令牌和分支拆成独立字段，则分别填写即可。

## Make 构建和运行

```sh
make
./compiler -token < examples/course_sample.c
```

不带 `-token` 时也输出相同格式的 token：

```sh
echo 'int a = 1;' | ./compiler
```

预期输出：

```text
0:'int':"int"
1:Ident:"a"
2:'=':"="
3:IntConst:1
4:';':";"
```

## CMake 构建和运行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/compiler -token < examples/course_sample.c
```

## 对照截图的功能

- 识别 ToyC 文法中的关键字、标识符、十进制整数和全部运算符、分隔符。
- 标识符遵循 `[_A-Za-z][_A-Za-z0-9]*`。
- 忽略空白、`//` 注释及在最近的 `*/` 结束的块注释。
- 对 `<= >= == != && ||` 做最长匹配。
- 按样例将负号输出为独立 token；例如 `-1` 为 `'-'`、`IntConst:1`。
- token 序号从 0 开始，标识符类型为 `Ident`，整数类型为 `IntConst`。
- 整数内容不加引号，其他 token 内容加双引号，token 类型按截图格式输出。
- 支持标准输入输出、文件重定向和 Linux 编译。

截图的整数正则与负数样例有歧义，本实现遵循给出的样例输出。截图未规定错误输入的输出协议，本实现对非法字符及未闭合块注释返回非零状态，并向标准错误输出位置说明。

GitHub 的 Linux 自动测试可验证构建及本项目测试；课程平台的隐藏测例仍需实际提交后确认。测评若失败，请保留平台的构建日志、运行命令、输入和实际输出，以便定位。
