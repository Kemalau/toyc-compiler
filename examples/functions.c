int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int sum(int n) {
    int result = 0;
    int i = 0;
    while (i < n) {
        i = i + 1;
        if (i == 2) continue;
        if (i > 4) break;
        result = result + i;
    }
    return result;
}

int main() {
    putint(factorial(5));
    putch(10);
    putint(sum(10));
    putch(10);
    return 0;
}
