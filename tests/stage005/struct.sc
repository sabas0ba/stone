// 仕様 2.2 節: 構造体・メンバアクセス (. ->)・構造体ポインタ・連結リスト。
struct node {
  int val;
  struct node *next;
};
struct node n1;
struct node n2;
struct node n3;
struct point {
  int x;
  int y;
  char name[8];
};
struct point pt;
int main() {
  struct node *p;
  int s;
  n1.val = 1;
  n2.val = 2;
  n3.val = 3;
  n1.next = &n2;
  n2.next = &n3;
  n3.next = 0;
  s = 0;
  p = &n1;
  while (p) { s = s + p->val; p = p->next; }
  if (s == 6) putc(111); else putc(88);
  pt.x = 3;
  pt.y = 4;
  pt.name[0] = 112;
  pt.name[1] = 116;
  pt.name[2] = 0;
  if (pt.x * pt.x + pt.y * pt.y == 25) putc(111); else putc(88);
  putc(pt.name[0]);
  putc(pt.name[1]);
  putc(10);
  return 0;
}
