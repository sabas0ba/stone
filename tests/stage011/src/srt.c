/* qsort / bsearch の検査 (docs/stage011-libc.md 8.5)
 *
 * 逆順・整列済み・重複・全要素同値・縮退 (nmemb 0 / 1)・要素の大きさが
 * 4 の倍数でない構造体を検査する。リンクするのは stdlib.o だけである。
 */
#include <stddef.h>
#include <stdlib.h>

int putc(int c);

int ok(int c) {
  if (c) putc('o');
  else putc('x');
  return 0;
}

int cmpint(void *a, void *b) {
  int x;
  int y;
  x = *(int *)a;
  y = *(int *)b;
  if (x < y) return -1;
  if (x > y) return 1;
  return 0;
}

/* 大きさ 3 バイト: 要素の大きさが 4 の倍数でなくても整列できること */
struct rec { char k[3]; };

int cmprec(void *a, void *b) {
  char *x;
  char *y;
  int i;
  x = (char *)a;
  y = (char *)b;
  for (i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] - y[i];
  return 0;
}

int issorted(int *v, int n) {
  int i;
  for (i = 1; i < n; i++)
    if (v[i - 1] > v[i]) return 0;
  return 1;
}

int v[16];
struct rec recs[4];

int main() {
  int i;
  int sum;
  int key;
  int *r;
  struct rec rk;
  struct rec *rp;

  /* 逆順 */
  for (i = 0; i < 16; i++) v[i] = 16 - i;
  qsort(v, 16, sizeof(int), cmpint);
  ok(issorted(v, 16) && v[0] == 1 && v[15] == 16);

  /* 整列済みを再整列しても変わらない */
  qsort(v, 16, sizeof(int), cmpint);
  ok(issorted(v, 16) && v[0] == 1 && v[15] == 16);

  /* 重複あり: 整列後も総和と両端が保たれる */
  for (i = 0; i < 16; i++) v[i] = (i * 7) % 4;      /* 0..3 が 4 回ずつ */
  qsort(v, 16, sizeof(int), cmpint);
  sum = 0;
  for (i = 0; i < 16; i++) sum = sum + v[i];
  ok(issorted(v, 16) && sum == 24 && v[0] == 0 && v[15] == 3);

  /* 全要素同値 */
  for (i = 0; i < 16; i++) v[i] = 5;
  qsort(v, 16, sizeof(int), cmpint);
  sum = 0;
  for (i = 0; i < 16; i++) sum = sum + v[i];
  ok(sum == 80);

  /* 縮退: nmemb が 0 / 1 でも呼べる */
  v[0] = 9;
  qsort(v, 0, sizeof(int), cmpint);
  qsort(v, 1, sizeof(int), cmpint);
  ok(v[0] == 9);

  /* 要素の大きさが 3 バイトの構造体 */
  recs[0].k[0] = 'c'; recs[0].k[1] = 'a'; recs[0].k[2] = 0;
  recs[1].k[0] = 'a'; recs[1].k[1] = 'b'; recs[1].k[2] = 0;
  recs[2].k[0] = 'b'; recs[2].k[1] = 'z'; recs[2].k[2] = 0;
  recs[3].k[0] = 'a'; recs[3].k[1] = 'a'; recs[3].k[2] = 0;
  qsort(recs, 4, sizeof(struct rec), cmprec);
  ok(recs[0].k[0] == 'a' && recs[0].k[1] == 'a'
     && recs[1].k[0] == 'a' && recs[1].k[1] == 'b'
     && recs[2].k[0] == 'b' && recs[3].k[0] == 'c');

  /* bsearch: 両端・中央・不在・nmemb 0 */
  for (i = 0; i < 16; i++) v[i] = (i + 1) * 2;      /* 2, 4, ..., 32 */
  key = 2;
  r = (int *)bsearch(&key, v, 16, sizeof(int), cmpint);
  ok(r == &v[0]);
  key = 32;
  r = (int *)bsearch(&key, v, 16, sizeof(int), cmpint);
  ok(r == &v[15]);
  key = 16;
  r = (int *)bsearch(&key, v, 16, sizeof(int), cmpint);
  ok(r != NULL && *r == 16);
  key = 17;
  ok(bsearch(&key, v, 16, sizeof(int), cmpint) == NULL);
  key = 1;
  ok(bsearch(&key, v, 16, sizeof(int), cmpint) == NULL);
  key = 33;
  ok(bsearch(&key, v, 16, sizeof(int), cmpint) == NULL);
  key = 2;
  ok(bsearch(&key, v, 0, sizeof(int), cmpint) == NULL);

  /* bsearch: 構造体の表からの探索 */
  rk.k[0] = 'b'; rk.k[1] = 'z'; rk.k[2] = 0;
  rp = (struct rec *)bsearch(&rk, recs, 4, sizeof(struct rec), cmprec);
  ok(rp != NULL && rp->k[0] == 'b' && rp->k[1] == 'z');

  putc('\n');
  return 0;
}
