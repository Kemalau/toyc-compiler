/* 可选示例运行库。课程平台若提供自己的运行库，应使用平台版本。 */
#include <stdio.h>

int getint(void) {
    int value = 0;
    if (scanf("%d", &value) != 1) return 0;
    return value;
}

int getch(void) { return getchar(); }
void putint(int value) { printf("%d", value); }
void putch(int value) { putchar(value); }
