cmake_minimum_required(VERSION 3.16)
project(ToyCCompiler LANGUAGES CXX)

add_executable(compiler
    main.cpp lexer.cpp parser.cpp semantic.cpp ir.cpp optimizer.cpp codegen.cpp
)
target_compile_features(compiler PRIVATE cxx_std_17)
set_target_properties(compiler PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 构建编译器不依赖 Python；仅在存在 Python 时注册回归测试。
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME lexer
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lexer.py" "$<TARGET_FILE:compiler>")
        add_test(NAME frontend
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compiler.py" "$<TARGET_FILE:compiler>")
    endif()
endif()
