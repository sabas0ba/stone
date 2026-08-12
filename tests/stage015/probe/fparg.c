/* 浮動小数点の仮引数 (次の世代で入る)。それまで cc は拒む */
int putc(int c);
int f(double d) { return (int)d; }
int main(void) { putc('0' + f(1.5)); return 0; }
