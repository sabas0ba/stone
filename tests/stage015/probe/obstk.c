/* libiberty の obstack.h (GNU C でない側) の式を，ホストの処理系と突き合わせる
 * (tools/diff17.sh)。docs/stage017-gcc.md 8.15。
 *
 * GCC の cc1 は値番号付け (tree-ssa-sccvn) の表を obstack_alloc で取る。
 * 我々の cc は __GNUC__ を定義しないので，obstack.h は文の式を使わない
 * 側の macro になる。そこで使う形 —— ポインタから (char *) 0 を引いて
 * 整数にし，整数に (char *) 0 を足してポインタに戻す，コンマ式と ?: の
 * 中の代入 —— を，同じ macro で値として見る。 */
int putc(int c);

static void pn(long v) {
  char b[16];
  int n;
  n = 0;
  if (v < 0) { putc('-'); v = -v; }
  if (v == 0) { b[n] = '0'; n = 1; }
  while (v > 0) { b[n] = (char)('0' + v % 10); v = v / 10; n = n + 1; }
  while (n > 0) { n = n - 1; putc(b[n]); }
  putc(' ');
}

#define PTR_INT_TYPE long
#define __PTR_TO_INT(P) ((P) - (char *) 0)
#define __INT_TO_PTR(P) ((P) + (char *) 0)

struct _obstack_chunk { char *limit; struct _obstack_chunk *prev; char contents[4]; };
struct obstack {
  long chunk_size;
  struct _obstack_chunk *chunk;
  char *object_base;
  char *next_free;
  char *chunk_limit;
  PTR_INT_TYPE temp;
  int alignment_mask;
  unsigned maybe_empty_object:1;
};

static int newchunk_calls;
static void _obstack_newchunk(struct obstack *h, int length) { newchunk_calls++; (void)h; (void)length; }

# define obstack_blank_fast(h,n) ((h)->next_free += (n))
# define obstack_blank(h,length)					\
( (h)->temp = (length),							\
  (((h)->chunk_limit - (h)->next_free < (h)->temp)			\
   ? (_obstack_newchunk ((h), (h)->temp), 0) : 0),			\
  obstack_blank_fast (h, (h)->temp))
# define obstack_finish(h)  						\
( ((h)->next_free == (h)->object_base					\
   ? (((h)->maybe_empty_object = 1), 0)					\
   : 0),								\
  (h)->temp = __PTR_TO_INT ((h)->object_base),				\
  (h)->next_free							\
    = __INT_TO_PTR ((__PTR_TO_INT ((h)->next_free)+(h)->alignment_mask)	\
		    & ~ ((h)->alignment_mask)),				\
  (((h)->next_free - (char *) (h)->chunk				\
    > (h)->chunk_limit - (char *) (h)->chunk)				\
   ? ((h)->next_free = (h)->chunk_limit) : 0),				\
  (h)->object_base = (h)->next_free,					\
  (void *) __INT_TO_PTR ((h)->temp))
# define obstack_alloc(h,length)					\
 (obstack_blank ((h), (length)), obstack_finish ((h)))

static union { struct _obstack_chunk c; char pad[4096]; double al; } mem;

int main(void) {
  struct obstack ob;
  struct obstack *h;
  char *a;
  char *b;
  char *c;
  char *base;
  h = &ob;
  /* 先頭の整列は処理系で違う (union の double)。16 に揃えた所から使う */
  base = (char *) (((long) (char *) &mem + 15) & ~15L);
  ob.chunk = (struct _obstack_chunk *) base;
  ob.chunk_limit = base + 4000;
  ob.object_base = base + 8;
  ob.next_free = base + 8;
  ob.alignment_mask = 7;
  ob.maybe_empty_object = 0;
  a = (char *) obstack_alloc (h, 24);
  b = (char *) obstack_alloc (h, 13);
  c = (char *) obstack_alloc (h, 4);
  pn((long)(a - base)); pn((long)(b - base)); pn((long)(c - base));
  pn((long)(ob.next_free - base)); pn((long)(ob.object_base - base));
  pn(newchunk_calls);
  putc('\n');
  return 0;
}
