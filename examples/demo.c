// 注释不会产生 token。
int main() {
    int a = -1;
    if (a <= 0 && a != 2) {
        a = a + 3;
    }
    return a;
}
