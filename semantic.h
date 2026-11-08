#pragma once
#include "ast.h"

// 核验并绑定 AST 中的变量；失败时抛出含位置的语义错误。
void analyze(Program& program);
