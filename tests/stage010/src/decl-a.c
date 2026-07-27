// 宣言の共有: 実体はすべて decl-b.c にある
extern int shared;
int addup(int a, int b);
int show(int v);
static int helper(int v);

int main() {
  int t;
  shared = 5;
  t = addup(shared, 3);
  show(t);
  show(helper(t));
  {
    int inner;
    inner = 100;
    show(inner);
  }
  {
    // 別のブロック。同じ名前を使い回せる (フレームも再利用される)
    int inner;
    int deep;
    inner = 7;
    {
      int deeper;
      deeper = inner * 3;
      deep = deeper;
    }
    show(deep);
  }
  putc(10);
  return 0;
}

static int helper(int v) { return v * 2; }
