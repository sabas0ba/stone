/* 自己参照する構造体 (連結リスト) と typedef の相互参照 */
int putc(int c);
typedef struct node Node;
struct node { int v; Node *next; };
int main(void) {
  Node a;
  Node b;
  Node *p;
  int n;
  a.v = 2; a.next = &b;
  b.v = 3; b.next = 0;
  n = 0;
  for (p = &a; p != 0; p = p->next) n = n + p->v;
  putc('0' + n);
  putc('\n');
  return 0;
}
