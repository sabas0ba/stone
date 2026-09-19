/* kernel.c --- 簡易 OS のカーネル (Stage 12 第 2 部)
 *
 * 設計は docs/stage012-os.md 5 章。M モードで走り，共有領域の sfs から
 * ELF 実行形式を読み，U モードのユーザプロセスとして走らせる。
 * syscall は RV32 Linux 互換の番号で受ける (2.1)。
 *
 * C では書けないもの (mtvec 設定・レジスタ退避・mret・PMP) はすべて
 * リンカの 'K' 前置部が機械語で持つ (5.2)。本ファイルは普通の C89 である。
 *
 * 前置部から呼ばれる関数:
 *   main()   起動直後。ユーザプロセスを仕立てて urun へ渡す
 *   ktrap()  trap 入口から。トラップフレームを見て syscall を処理する
 */

/* 前置部が提供する */
int putc(int c);
int getc(void);
int exit(int code);
int urun(void);

/* メモリ配置 (docs/stage012-os.md 4.3 / 5.5) */
#define TFA     0x83700000      /* トラップフレーム */
#define SFSA    0x84000000      /* 共有領域 (sfs イメージ) */
#define UBASE   0x86000000      /* ユーザ像のロード位置 */
#define USP     0x87000000      /* ユーザのフレームスタック上端 */
#define UBRKMAX 0x86e00000      /* brk の上限 */
#define UARTA   0x10000000      /* UART */

/* sfs の表 (4.3) */
#define ENTSZ   64
#define E_NAME  0
#define E_OFF   52
#define E_LEN   56
#define E_FLAG  60

/* syscall 番号 (5.4) */
#define SYS_OPENAT 56
#define SYS_CLOSE  57
#define SYS_READ   63
#define SYS_WRITE  64
#define SYS_EXIT   93
#define SYS_BRK    214

/* errno (負値で返す) */
#define ENOENT   2
#define EBADF    9
#define ENOMEM   12
#define EINVAL   22
#define ENOSYS   38

#define NFD 16

int tblo;                       /* 表の先頭 (sfs 内オフセット) */
int tbln;                       /* 表の件数 */
int fdent[NFD];                 /* fd -> 表の項目番号 (-1 = 未使用) */
int fdpos[NFD];                 /* fd -> 読み書き位置 */
unsigned ubrk;                  /* ユーザの break */

/* 絶対アドレスの 32 bit 読み書き。アドレスは 0x8000_0000 以上なので
 * unsigned で扱う (int だと負数になり比較を誤る) */
unsigned ld4(unsigned a) { return *(unsigned *)a; }
int st4(unsigned a, unsigned v) { *(unsigned *)a = v; return 0; }

/* 表の項目 i の絶対アドレス */
unsigned ent(int i) { return SFSA + tblo + i * ENTSZ; }

/* 項目の名前と s が一致するか */
int nameeq(unsigned e, char *s) {
  char *p;
  int i;
  p = (char *)e;
  i = 0;
  while (i < 51) {
    if (p[i] != s[i]) return 0;
    if (p[i] == 0) return 1;
    i = i + 1;
  }
  return 0;
}

/* 名前で表を引く。見つからなければ -1 */
int sfsfind(char *name) {
  int i;
  for (i = 0; i < tbln; i++)
    if (ld4(ent(i) + E_FLAG) == 1 && nameeq(ent(i) + E_NAME, name)) return i;
  return -1;
}

/* 新しい項目を作る。データはカーソル位置から追記で伸ばす (4.2) */
int sfsnew(char *name) {
  int i;
  int j;
  unsigned e;
  for (i = 0; i < tbln; i++) {
    e = ent(i);
    if (ld4(e + E_FLAG) == 0) {
      for (j = 0; j < 51 && name[j]; j++) *(char *)(e + j) = name[j];
      *(char *)(e + j) = 0;
      st4(e + E_OFF, ld4(SFSA + 16));   /* カーソル */
      st4(e + E_LEN, 0);
      st4(e + E_FLAG, 1);
      return i;
    }
  }
  return -1;
}

/* UART から 1 バイト取る。用意が無ければ -1 (read は EOF として扱う) */
int uartrx(void) {
  char *u;
  u = (char *)UARTA;
  if ((u[5] & 1) == 0) return -1;
  return u[0] & 255;
}

/* ---- syscall ---- */

int sys_write(int fd, unsigned buf, int n) {
  int i;
  unsigned e;
  unsigned off;
  int len;
  int pos;
  if (n < 0) return 0 - EINVAL;
  if (fd == 1 || fd == 2) {
    for (i = 0; i < n; i++) putc(*(char *)(buf + i) & 255);
    return n;
  }
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  e = ent(fdent[fd]);
  off = ld4(e + E_OFF);
  len = (int)ld4(e + E_LEN);
  pos = fdpos[fd];
  for (i = 0; i < n; i++) *(char *)(SFSA + off + pos + i) = *(char *)(buf + i);
  pos = pos + n;
  fdpos[fd] = pos;
  if (pos > len) {
    st4(e + E_LEN, (unsigned)pos);
    /* 末尾を伸ばしたのでカーソルも進める (4 バイト境界へ) */
    st4(SFSA + 16, (off + pos + 3) / 4 * 4);
  }
  return n;
}

int sys_read(int fd, unsigned buf, int n) {
  int i;
  int c;
  unsigned e;
  unsigned off;
  int len;
  int pos;
  if (n < 0) return 0 - EINVAL;
  if (fd == 0) {
    for (i = 0; i < n; i++) {
      c = uartrx();
      if (c < 0) return i;
      *(char *)(buf + i) = c;
    }
    return n;
  }
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  e = ent(fdent[fd]);
  off = ld4(e + E_OFF);
  len = (int)ld4(e + E_LEN);
  pos = fdpos[fd];
  if (pos >= len) return 0;
  if (n > len - pos) n = len - pos;
  for (i = 0; i < n; i++) *(char *)(buf + i) = *(char *)(SFSA + off + pos + i);
  fdpos[fd] = pos + n;
  return n;
}

int sys_openat(int dirfd, unsigned path, int flags) {
  int i;
  int fd;
  i = sfsfind((char *)path);
  if (i < 0) {
    if ((flags & 64) == 0) return 0 - ENOENT;   /* O_CREAT が無い */
    i = sfsnew((char *)path);
    if (i < 0) return 0 - ENOMEM;
  } else if (flags & 512) {                     /* O_TRUNC */
    st4(ent(i) + E_LEN, 0);
  }
  for (fd = 3; fd < NFD; fd++) {
    if (fdent[fd] < 0) {
      fdent[fd] = i;
      fdpos[fd] = 0;
      return fd;
    }
  }
  return 0 - ENOMEM;
}

int sys_close(int fd) {
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  fdent[fd] = -1;
  return 0;
}

/* Linux 生の brk: 成否によらず「新しいブレーク」を返す */
unsigned sys_brk(unsigned a) {
  if (a >= ubrk && a < UBRKMAX) ubrk = a;
  return ubrk;
}

/* 16 進 8 桁で出す (診断用) */
int phex(unsigned v) {
  int i;
  int d;
  for (i = 7; i >= 0; i = i - 1) {
    d = (int)((v >> (i * 4)) & 15);
    if (d < 10) putc('0' + d);
    else putc('a' + d - 10);
  }
  return 0;
}

/* ---- トラップ ---- */

/* トラップフレーム: tf[0]=x1 ... tf[30]=x31, tf[31]=mepc, tf[32]=mcause */
int ktrap(void) {
  unsigned *tf;
  unsigned n;
  int r;
  tf = (unsigned *)TFA;
  if (tf[32] != 8) {            /* U モードからの ecall 以外は想定外 */
    /* 原因と PC を出して停止する。mcause の意味は RISC-V 特権仕様の表
     * (1 = 命令アクセス例外, 2 = 不正命令, 5 = ロード, 7 = ストア) */
    putc('!');
    phex(tf[32]);
    putc(' ');
    phex(tf[31]);
    putc('\n');
    exit(9);
  }
  tf[31] = tf[31] + 4;          /* ecall の次から再開する */
  n = tf[16];                   /* a7 = 番号 */
  if (n == SYS_WRITE) r = sys_write((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_READ) r = sys_read((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_OPENAT) r = sys_openat((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_CLOSE) r = sys_close((int)tf[9]);
  else if (n == SYS_BRK) r = (int)sys_brk(tf[9]);
  else if (n == SYS_EXIT) exit((int)tf[9]);
  else r = 0 - ENOSYS;
  tf[9] = (unsigned)r;          /* a0 = 結果 */
  return 0;
}

/* ---- 起動 ---- */

/* ELF 実行形式を配置する。返り値は entry (失敗なら 0) */
unsigned loadelf(int i) {
  unsigned src;
  unsigned ph;
  unsigned poff;
  unsigned pva;
  unsigned fsz;
  unsigned msz;
  unsigned k;
  src = SFSA + ld4(ent(i) + E_OFF);
  if ((*(char *)src & 255) != 127) return 0;
  if (*(char *)(src + 1) != 'E') return 0;
  ph = ld4(src + 28);                   /* e_phoff */
  poff = ld4(src + ph + 4);
  pva = ld4(src + ph + 8);
  fsz = ld4(src + ph + 16);
  msz = ld4(src + ph + 20);
  for (k = 0; k < fsz; k++) *(char *)(pva + k) = *(char *)(src + poff + k);
  while (k < msz) { *(char *)(pva + k) = 0; k = k + 1; }
  ubrk = pva + msz;
  return ld4(src + 24);                 /* e_entry */
}

/* argv を積んだユーザスタックを作り，その先頭 (argc の位置) を返す。
 * 配置は [argc][argv0][0][0] で，前置部が sp から読む (5.5) */
unsigned setupstack(char *name) {
  unsigned strp;
  unsigned sp;
  int i;
  strp = USP - 256;
  for (i = 0; i < 200 && name[i]; i++) *(char *)(strp + i) = name[i];
  *(char *)(strp + i) = 0;
  sp = USP - 320;
  st4(sp, 1);
  st4(sp + 4, strp);
  st4(sp + 8, 0);
  st4(sp + 12, 0);
  return sp;
}

int main(void) {
  unsigned *tf;
  unsigned entry;
  unsigned sp;
  int i;
  int b;
  int n;
  char name[64];

  if (ld4(SFSA) != 0x31736673) {        /* 'sfs1' */
    putc('?');
    putc('\n');
    return 1;
  }
  tblo = (int)ld4(SFSA + 8);
  tbln = (int)ld4(SFSA + 12);
  for (i = 0; i < NFD; i++) fdent[i] = -1;

  /* 起動するプログラムの名前は sfs 内の boot に 1 行で書く (5.5) */
  b = sfsfind("boot");
  if (b < 0) { putc('B'); putc('\n'); return 1; }
  n = (int)ld4(ent(b) + E_LEN);
  if (n > 63) n = 63;
  for (i = 0; i < n; i++) name[i] = *(char *)(SFSA + ld4(ent(b) + E_OFF) + i);
  while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r')) n = n - 1;
  name[n] = 0;

  i = sfsfind(name);
  if (i < 0) { putc('N'); putc('\n'); return 1; }
  entry = loadelf(i);
  if (entry == 0) { putc('L'); putc('\n'); return 1; }
  sp = setupstack(name);

  /* トラップフレームを仕立てて U モードへ渡す */
  tf = (unsigned *)TFA;
  for (i = 0; i < 33; i++) tf[i] = 0;
  tf[1] = sp;                           /* x2 = sp */
  tf[31] = entry;                       /* mepc */
  urun();
  return 0;
}
