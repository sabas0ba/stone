/* 共用体による表現の読み替え。バイト順の検査などで使われる */
int putc(int c);
union u { unsigned w; unsigned char b[4]; };
int main(void) {
  union u v;
  v.w = 1;
  putc('0' + v.b[0]);
  putc('0' + v.b[3]);
  putc('\n');
  return 0;
}
