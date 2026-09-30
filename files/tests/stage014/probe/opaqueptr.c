/* 不透明構造体 (完成しない前方宣言) へのポインタをメンバに持つ構造体。
 * zlib の z_stream (struct internal_state *state) で常用 */
int putc(int c);
struct hidden;
typedef struct s { struct hidden *state; int a; } zs;
zs g;
int f(zs *p) { return p->a; }
int main(void) {
  zs v;
  v.state = 0;
  v.a = 5;
  g.a = 2;
  putc('0' + f(&v));
  putc('0' + g.a);
  putc('\n');
  return 0;
}
