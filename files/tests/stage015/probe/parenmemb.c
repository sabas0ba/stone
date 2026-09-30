/* 括弧で囲んだメンバの宣言子を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.12 / stage015/cc15ai.sc。
 *
 * C89 6.5.4 の宣言子は「( declarator )」を含み，括弧は意味を変えない。
 * GCC の reload.h は struct target_reload のメンバを次の形で書く。
 *
 *   enum machine_mode (x_regno_save_mode [FIRST_PSEUDO_REGISTER]
 *                                        [MAX_MOVE_MAX / MIN_UNITS_PER_WORD + 1]);
 *
 * cc15ah までは名前の位置の `(` で構文エラー (1) になった。
 *
 * **次元を読み違えれば配置が変わる。** 前後のメンバの位置と構造体の
 * 大きさを値で見る。 */
int putc(int c);

static void pn(int v) {
  char b[16];
  int n;
  int neg;
  n = 0;
  neg = 0;
  if (v < 0) { neg = 1; v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  if (neg) putc('-');
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}
static void nl(void) { putc('\n'); }

enum mode { M0, M1, M2 };

struct tr {
  char ok;
  enum mode (save [3][2 + 1]);
  int (plain);
  short (outer)[5];
  int last;
};

static struct tr g;

int main(void) {
  struct tr *p;
  p = &g;
  p->ok = 1;
  p->save[2][2] = M2;
  p->save[1][0] = M1;
  p->plain = 40;
  p->outer[4] = 7;
  p->last = 99;
  pn((int)sizeof(struct tr));
  pn((int)((char *)&p->plain - (char *)p));
  pn((int)((char *)&p->outer - (char *)p));
  pn((int)((char *)&p->last - (char *)p));
  pn((int)g.save[2][2] + (int)g.save[1][0]);
  pn(g.plain + g.outer[4] + g.last);
  nl();
  return 0;
}
