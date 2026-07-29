// 補遺 2: 配列への単項 & (docs/stage010-c89.md 20 章)
//
// &a の値は先頭要素のアドレスと同じで，型が「配列へのポインタ」になる。
// 検査するのは値そのものより「型の変更が演算に反映されること」で，
//   - &a + 1 が配列 1 個ぶん進むこと
//   - sizeof(*&a) が配列全体の大きさになること
//   - offsetof の慣用的な定義 (キャスト・メンバ参照・& の組合せ) が
//     配列メンバでも通ること
// を確かめる。
struct s { char a; int b; char c[8]; short d; };
int a[2][3];
char b[10];
int main() {
  int i;
  for (i = 0; i < 6; i++) a[i / 3][i % 3] = i;
  putc('0' + (&b == (char *)b));                    // 値は先頭要素のアドレス
  putc('0' + ((char *)(&b + 1) - (char *)b == 10)); // 配列 1 個ぶん進む
  putc('0' + (sizeof(*&b) == 10));                  // 参照はがしで配列に戻る
  putc('0' + (sizeof(*&a) == 24));
  putc('0' + ((*(&a[0] + 1))[0] == 3));             // &a[0] は int (*)[3]
  putc('0' + ((char *)(&a + 1) - (char *)a == 24));
  putc('0' + ((unsigned)&(((struct s *)0)->c) == 8));   // offsetof の形
  putc('\n');
  return 0;
}
