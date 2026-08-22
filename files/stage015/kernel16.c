/* kernel14.c --- 簡易 OS のカーネル (Stage 14 世代)
 *
 * stage013/kernel.c を出発点に，2 つの穴を塞いだものである
 * (docs/stage014-external.md 13 章)。機能は足していない。
 *
 *   sfs の上書き  既存ファイルを元の割付けより長く書き直しても隣接
 *                 ファイルを壊さない。割付けに収まらない書込みは末尾へ
 *                 引っ越してから行い，カーソルは決して巻き戻らない
 *                 (kernel13 は隣を上書きし，カーソルも巻き戻した)
 *   ELF の検査    セグメントの載せ先がユーザ領域に収まることを確かめて
 *                 から複写する。壊れた ELF でカーネル・退避領域を
 *                 上書きできない。spawn の資源検査も出力ファイルの
 *                 切詰めより前へ移した
 *
 * kernel13 から引き継ぐもの: spawn (500) による逐次実行と親の像の退避，
 * fd 0 / fd 1 のつなぎ替え，fd 0 の read の待ち合わせ。
 *
 * kernel16 (第 6 部): **PT_LOAD をすべて載せる**。kernel15 までは
 * プログラムヘッダの 1 本目だけを見ていた。tcc が吐く実行形式は
 * R / RX / RW の 3 本に分かれるので，1 本目だけでは走らない
 * (docs/stage015-tcc.md 12.11)。
 *
 * それ以外の設計は stage012 のまま: M モードで走り，共有領域の sfs から
 * ELF 実行形式を読み，U モードのユーザプロセスとして走らせる。syscall は
 * RV32 Linux 互換の番号で受け，独自の拡張 (spawn) だけを 500 番台に置く。
 *
 * C では書けないもの (mtvec 設定・レジスタ退避・mret・PMP) はすべて
 * リンカの 'K' 前置部が機械語で持つ。本ファイルは普通の C89 である。
 *
 * 前置部から呼ばれる関数:
 *   main()   起動直後。ユーザプロセスを仕立てて urun へ渡す
 *   ktrap()  trap 入口から。トラップフレームを見て syscall を処理する
 */

/* 前置部が提供する (getc は UART をブロックして待つ 1 バイト読み) */
int putc(int c);
int getc(void);
int exit(int code);
int urun(void);

/* メモリ配置 (docs/stage012-os.md 4.3 / 5.5, docs/stage013-tools.md 3.2) */
#define TFA     0x83700000      /* トラップフレーム */
#define SAVEA   0x81000000      /* spawn の退避領域 (ここから上へ積む) */
#define SAVETOP 0x83700000      /* 退避領域の上限 (= TFA) */
#define SFSA    0x84000000      /* 共有領域 (sfs イメージ) */
#define UBASE   0x86000000      /* ユーザ像のロード位置 */
#define USP     0x87000000      /* ユーザのフレームスタック上端 */
#define UBRKMAX 0x86e00000      /* brk の上限 */
#define UARTA   0x10000000      /* UART */

/* sfs の表 (docs/stage012-os.md 4.3) */
#define ENTSZ   64
#define E_NAME  0
#define E_OFF   52
#define E_LEN   56
#define E_FLAG  60

/* syscall 番号。Linux 互換 (stage012-os.md 5.4) と独自の拡張
 * (500 番台。stage013-tools.md 3.2) */
#define SYS_OPENAT 56
#define SYS_CLOSE  57
#define SYS_READ   63
#define SYS_WRITE  64
#define SYS_EXIT   93
#define SYS_LSEEK  62
#define SYS_BRK    214
#define SYS_SPAWN  500

/* errno (負値で返す) */
#define ENOENT   2
#define E2BIG    7
#define ENOEXEC  8
#define EBADF    9
#define ENOMEM   12
#define EINVAL   22
#define ENOSPC   28
#define ENOSYS   38

#define NFD 16
#define MAXDEP 8

int tblo;                       /* 表の先頭 (sfs 内オフセット) */
int tbln;                       /* 表の件数 */
int fdent[16];                  /* fd -> 表の項目番号 (-1 = 未使用)。NFD 個 */
int fdpos[16];                  /* fd -> 読み書き位置 */
unsigned ubrk;                  /* ユーザの break */

/* fd 0 / fd 1 の結び先 (-1 = UART，それ以外 = sfs の項目番号) */
int fd0ent;
int fd0pos;
int fd1ent;
int fd1pos;

/* spawn の入れ子。sbase[d] は深さ d の親の退避レコードの先頭 */
int depth;
unsigned savecur;
unsigned sbase[8];              /* MAXDEP 段 */

/* spawn / boot の引数の写し。親の像は子の配置で消えるので，文字列は
 * 先にカーネル側へ写す (docs/stage013-tools.md 3.2) */
char kargs[512];                /* 引数文字列の連結 (NUL 区切り) */
int kargo[8];                   /* kargs 内の各引数の開始位置 */
int kargc;

/* 絶対アドレスの 32 bit 読み書き。アドレスは 0x8000_0000 以上なので
 * unsigned で扱う (int だと負数になり比較を誤る) */
unsigned ld4(unsigned a) { return *(unsigned *)a; }
/* 16 bit 読み。ELF の e_phnum / e_phentsize に要る。境界が保証されない
 * ので 1 バイトずつ組む */
unsigned ld2(unsigned a) {
  return (*(char *)a & 255) | ((*(char *)(a + 1) & 255) << 8);
}
int st4(unsigned a, unsigned v) { *(unsigned *)a = v; return 0; }

/* 語単位の複写。n は 4 の倍数 */
int wcopy(unsigned d, unsigned s, unsigned n) {
  unsigned k;
  for (k = 0; k < n; k = k + 4) *(unsigned *)(d + k) = *(unsigned *)(s + k);
  return 0;
}

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

/* UART から 1 バイト取る。用意が無ければ -1 */
int uartrx(void) {
  char *u;
  u = (char *)UARTA;
  if ((u[5] & 1) == 0) return -1;
  return u[0] & 255;
}

/* ---- sfs ファイルの読み書き (fd 経由・つなぎ替えの両方から使う) ---- */

/* 項目 ei の pos から n バイト読む。返り値は読めたバイト数 */
int sfsread(int ei, int pos, unsigned buf, int n) {
  int i;
  unsigned off;
  int len;
  off = ld4(ent(ei) + E_OFF);
  len = (int)ld4(ent(ei) + E_LEN);
  if (pos >= len) return 0;
  if (n > len - pos) n = len - pos;
  for (i = 0; i < n; i++) *(char *)(buf + i) = *(char *)(SFSA + off + pos + i);
  return n;
}

/* 4 バイト境界へ切上げる */
unsigned rup4(unsigned v) { return (v + 3) / 4 * 4; }

/* 項目 ei が need バイトを持てるようにする。0 = 成功，-1 = 領域不足。
 *
 * 追記割付けの決まり: 各項目は [off, off + rup4(len)) を占め，カーソルは
 * 常に「使用済みの末尾」を指す。したがって
 *
 *     off + rup4(len) == カーソル   ⇔   この項目が最後の割付け
 *
 * である。最後の割付けならその場で伸ばしてよい。そうでなければ後ろに
 * 別の項目がいるので，末尾へ引っ越してから伸ばす。カーソルは決して
 * 巻き戻さない。
 *
 * kernel13 はこの区別をせず off + pos へ無条件に書き，カーソルを
 * off + pos + n へ無条件に置いていた。そのため (1) 元の割付けより長く
 * 書き直すと隣接ファイルを壊し，(2) 末尾でない項目を伸ばすとカーソルが
 * 巻き戻って以後の新規作成が既存データに重なった (#44)。*/
int sfsgrow(int ei, int need) {
  unsigned e;
  unsigned off;
  unsigned cur;
  unsigned tot;
  unsigned nw;
  int len;
  int i;
  e = ent(ei);
  off = ld4(e + E_OFF);
  len = (int)ld4(e + E_LEN);
  cur = ld4(SFSA + 16);
  tot = ld4(SFSA + 4);
  nw = rup4((unsigned)need);
  if ((unsigned)need <= rup4((unsigned)len)) return 0;  /* 今の割付けに収まる */
  if (off + rup4((unsigned)len) == cur) {               /* 末尾なので伸ばすだけ */
    if (nw > tot - off) return -1;
    st4(SFSA + 16, off + nw);
    return 0;
  }
  /* 末尾へ引っ越す。中身 (len バイト) を移してから新しい位置を記録する */
  if (nw > tot - cur) return -1;
  for (i = 0; i < len; i++)
    *(char *)(SFSA + cur + i) = *(char *)(SFSA + off + i);
  st4(e + E_OFF, cur);
  st4(SFSA + 16, cur + nw);
  return 0;
}

/* 項目 ei の pos へ n バイト書く。返り値は書けたバイト数か -errno */
int sfswrite(int ei, int pos, unsigned buf, int n) {
  int i;
  unsigned e;
  unsigned off;
  int len;
  e = ent(ei);
  len = (int)ld4(e + E_LEN);
  if (pos + n > len) {
    if (sfsgrow(ei, pos + n) < 0) return 0 - ENOSPC;
  }
  off = ld4(e + E_OFF);                 /* 引っ越したかもしれない */
  /* len を飛び越して書く場合，間は 0 で埋める (旧領域の残りが見えない
   * ようにする)。現状の fd はこの形にならないが，意味を決めておく */
  for (i = len; i < pos; i++) *(char *)(SFSA + off + i) = 0;
  for (i = 0; i < n; i++) *(char *)(SFSA + off + pos + i) = *(char *)(buf + i);
  if (pos + n > len) st4(e + E_LEN, (unsigned)(pos + n));
  return n;
}

/* ---- syscall ---- */

int sys_write(int fd, unsigned buf, int n) {
  int i;
  int w;
  if (n < 0) return 0 - EINVAL;
  if (fd == 1 && fd1ent >= 0) {
    w = sfswrite(fd1ent, fd1pos, buf, n);
    if (w < 0) return w;                /* 領域不足。位置は進めない */
    fd1pos = fd1pos + w;
    return w;
  }
  if (fd == 1 || fd == 2) {
    for (i = 0; i < n; i++) putc(*(char *)(buf + i) & 255);
    return n;
  }
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  w = sfswrite(fdent[fd], fdpos[fd], buf, n);
  if (w < 0) return w;
  fdpos[fd] = fdpos[fd] + w;
  return w;
}

/* 読み書き位置を動かす (第 4 部で追加。SEEK_SET/CUR/END = 0/1/2)。
 * tcc が .o と .a を読むときに使う。標準入出力には効かない */
int sys_lseek(int fd, int off, int whence) {
  int base;
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  base = 0;
  if (whence == 1) base = fdpos[fd];
  else if (whence == 2) base = (int)ld4(ent(fdent[fd]) + E_LEN);
  else if (whence != 0) return 0 - EINVAL;
  if (base + off < 0) return 0 - EINVAL;
  fdpos[fd] = base + off;
  return fdpos[fd];
}

int sys_read(int fd, unsigned buf, int n) {
  int i;
  int c;
  if (n < 0) return 0 - EINVAL;
  if (fd == 0) {
    if (fd0ent >= 0) {
      n = sfsread(fd0ent, fd0pos, buf, n);
      fd0pos = fd0pos + n;
      return n;
    }
    /* UART: 最初の 1 バイトは届くまで待ち，あとは届いている分だけ返す
     * (docs/stage013-tools.md 3.4)。入力の終わりは約束 (EOT) で表す */
    for (i = 0; i < n; i++) {
      if (i == 0) c = getc();
      else c = uartrx();
      if (c < 0) return i;
      *(char *)(buf + i) = c;
    }
    return n;
  }
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  n = sfsread(fdent[fd], fdpos[fd], buf, n);
  fdpos[fd] = fdpos[fd] + n;
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

/* ---- プロセスの配置 ---- */

/* 項目 i が載せられる ELF かを検べる。1 = 可，0 = 否。
 *
 * kernel13 は先頭 2 バイトしか見ずに p_vaddr / p_filesz / p_memsz を
 * そのまま信じて複写していた。壊れた ELF はユーザ領域の外——カーネル
 * 本体や spawn の退避領域——を書き潰せた (#49)。載せる前にここで
 * 「ファイルの中に収まっているか」と「載せ先がユーザ領域か」を確かめる。
 *
 * 引き算はすべて「引かれる側が大きいこと」を確かめてから行う。
 * unsigned なので a + b > c の形は桁あふれで通ってしまう */
int elfok(int i) {
  unsigned src;
  unsigned flen;
  unsigned ph;
  unsigned pnum;
  unsigned pesz;
  unsigned k;
  unsigned e;
  unsigned poff;
  unsigned pva;
  unsigned fsz;
  unsigned msz;
  flen = ld4(ent(i) + E_LEN);
  if (flen < 52) return 0;                      /* ELF ヘッダに足りない */
  src = SFSA + ld4(ent(i) + E_OFF);
  if ((*(char *)src & 255) != 127) return 0;
  if (*(char *)(src + 1) != 'E') return 0;
  if (*(char *)(src + 2) != 'L') return 0;
  if (*(char *)(src + 3) != 'F') return 0;
  ph = ld4(src + 28);                           /* e_phoff */
  pesz = ld2(src + 42);                         /* e_phentsize */
  pnum = ld2(src + 44);                         /* e_phnum */
  if (pesz < 32) return 0;
  if (pnum == 0) return 0;
  if (ph > flen) return 0;
  if ((flen - ph) / pesz < pnum) return 0;      /* PH の表がファイルの外 */
  for (k = 0; k < pnum; k = k + 1) {
    e = ph + k * pesz;
    if (ld4(src + e) != 1) continue;            /* PT_LOAD 以外は載せない */
    poff = ld4(src + e + 4);
    pva = ld4(src + e + 8);
    fsz = ld4(src + e + 16);
    msz = ld4(src + e + 20);
    if (fsz > msz) return 0;                    /* 中身が器より大きい */
    if (poff > flen || flen - poff < fsz) return 0;   /* 中身がファイルの外 */
    if (msz > UBRKMAX - UBASE) return 0;        /* ユーザ領域より大きい */
    if (pva < UBASE) return 0;                  /* 載せ先が下に外れる */
    if (pva > UBRKMAX - msz) return 0;          /* 載せ先が上に外れる */
  }
  return 1;
}

/* ELF 実行形式を配置する。返り値は entry (失敗なら 0) */
unsigned loadelf(int i) {
  unsigned src;
  unsigned ph;
  unsigned pnum;
  unsigned pesz;
  unsigned n;
  unsigned e;
  unsigned poff;
  unsigned pva;
  unsigned fsz;
  unsigned msz;
  unsigned k;
  unsigned top;
  if (!elfok(i)) return 0;
  src = SFSA + ld4(ent(i) + E_OFF);
  ph = ld4(src + 28);                   /* e_phoff */
  pesz = ld2(src + 42);
  pnum = ld2(src + 44);
  top = UBASE;
  for (n = 0; n < pnum; n = n + 1) {
    e = ph + n * pesz;
    if (ld4(src + e) != 1) continue;    /* PT_LOAD 以外 */
    poff = ld4(src + e + 4);
    pva = ld4(src + e + 8);
    fsz = ld4(src + e + 16);
    msz = ld4(src + e + 20);
    for (k = 0; k < fsz; k++) *(char *)(pva + k) = *(char *)(src + poff + k);
    while (k < msz) { *(char *)(pva + k) = 0; k = k + 1; }
    if (pva + msz > top) top = pva + msz;
  }
  ubrk = top;
  return ld4(src + 24);                 /* e_entry */
}

/* kargs / kargo / kargc の引数列を積んだユーザスタックを作り，その先頭
 * (argc の位置) を返す。配置は [argc][argv...][0][0] で，文字列の実体は
 * USP - 512 から置く。前置部が sp から読む (docs/stage012-os.md 5.5) */
unsigned setupstack(void) {
  unsigned w;
  unsigned sp;
  int i;
  int j;
  w = USP - 512;
  sp = USP - 560;                       /* 512 + argc/argv/番兵ぶん 48 */
  st4(sp, (unsigned)kargc);
  for (i = 0; i < kargc; i++) {
    st4(sp + 4 + i * 4, w);
    j = kargo[i];
    while (kargs[j]) {
      *(char *)w = kargs[j];
      w = w + 1;
      j = j + 1;
    }
    *(char *)w = 0;
    w = w + 1;
  }
  st4(sp + 4 + kargc * 4, 0);
  st4(sp + 8 + kargc * 4, 0);
  return sp;
}

/* ---- spawn (docs/stage013-tools.md 3.2) ---- */

/* ユーザ空間の文字列を d (容量 cap) へ写す。収まれば長さ，長すぎれば -1 */
int cpystr(char *d, unsigned s, int cap) {
  int i;
  for (i = 0; i < cap; i++) {
    d[i] = *(char *)(s + i);
    if (d[i] == 0) return i;
  }
  return -1;
}

/* 親の像を退避して子を配置する。返り値は 0 か -errno。
 * 退避レコードの配置 (バイト): +0 tf 33 語, +132 fdent 16 語,
 * +196 fdpos 16 語, +260 つなぎ替え 4 語, +276 ubrk/sp/imgsz/stksz の 4 語,
 * +292 から像 [UBASE, ubrk) とフレームスタック [sp, USP) の複写 */
int sys_spawn(unsigned sa) {
  unsigned *tf;
  char path[64];
  char name[64];
  unsigned argvp;
  unsigned p;
  int ci;
  int ie;
  int ipos;
  int oe;
  int opos;
  int i;
  int n;
  unsigned b;
  unsigned imgsz;
  unsigned psp;
  unsigned stksz;
  unsigned entry;

  tf = (unsigned *)TFA;
  if (depth >= MAXDEP) return 0 - ENOMEM;
  if (cpystr(path, ld4(sa), 63) < 0) return 0 - E2BIG;
  ci = sfsfind(path);
  if (ci < 0) return 0 - ENOENT;
  /* 親を壊す前に，載せられる ELF であることを確かめる。elfok が通れば
   * 後段の loadelf は失敗しない */
  if (!elfok(ci)) return 0 - ENOEXEC;

  /* 退避領域が足りるかも先に検べる。つなぎ替えの解決は出力ファイルを
   * 切り詰めるので，その前に「起動できない」と判る失敗は済ませておく
   * (kernel13 は切詰めの後で ENOMEM を返し，起動しないのに出力ファイル
   * だけが空になった。#49) */
  imgsz = (ubrk - UBASE + 3) / 4 * 4;
  psp = tf[1] / 4 * 4;
  stksz = USP - psp;
  if (savecur + 292 + imgsz + stksz > SAVETOP) return 0 - ENOMEM;

  /* 引数をカーネル側へ写す。argv が 0 なら {path} 相当 */
  argvp = ld4(sa + 4);
  kargc = 0;
  n = 0;
  if (argvp == 0) {
    kargo[0] = 0;
    for (i = 0; path[i]; i++) kargs[i] = path[i];
    kargs[i] = 0;
    kargc = 1;
  } else {
    while (kargc < 8) {
      p = ld4(argvp + kargc * 4);
      if (p == 0) break;
      kargo[kargc] = n;
      i = cpystr(kargs + n, p, 511 - n);
      if (i < 0) return 0 - E2BIG;
      n = n + i + 1;
      kargc = kargc + 1;
    }
    if (kargc == 0) return 0 - EINVAL;
  }

  /* つなぎ替えの解決。0 なら親の結び先と位置を引き継ぐ */
  ie = fd0ent;
  ipos = fd0pos;
  p = ld4(sa + 8);
  if (p != 0) {
    if (cpystr(name, p, 63) < 0) return 0 - E2BIG;
    ie = sfsfind(name);
    if (ie < 0) return 0 - ENOENT;
    ipos = 0;
  }
  oe = fd1ent;
  opos = fd1pos;
  p = ld4(sa + 12);
  if (p != 0) {
    if (cpystr(name, p, 63) < 0) return 0 - E2BIG;
    oe = sfsfind(name);
    if (oe < 0) oe = sfsnew(name);
    else st4(ent(oe) + E_LEN, 0);               /* 切詰め */
    if (oe < 0) return 0 - ENOMEM;
    opos = 0;
  }

  /* 親を退避する (大きさと空きは検査済み) */
  b = savecur;
  sbase[depth] = b;
  wcopy(b, TFA, 132);
  for (i = 0; i < NFD; i++) {
    st4(b + 132 + i * 4, (unsigned)fdent[i]);
    st4(b + 196 + i * 4, (unsigned)fdpos[i]);
  }
  st4(b + 260, (unsigned)fd0ent);
  st4(b + 264, (unsigned)fd0pos);
  st4(b + 268, (unsigned)fd1ent);
  st4(b + 272, (unsigned)fd1pos);
  st4(b + 276, ubrk);
  st4(b + 280, psp);
  st4(b + 284, imgsz);
  st4(b + 288, stksz);
  wcopy(b + 292, UBASE, imgsz);
  wcopy(b + 292 + imgsz, psp, stksz);
  savecur = b + 292 + imgsz + stksz;
  depth = depth + 1;

  /* 子を配置する。ELF は検査済みなのでここでは失敗しない */
  entry = loadelf(ci);
  for (i = 0; i < 33; i++) tf[i] = 0;
  tf[1] = setupstack();
  tf[31] = entry;
  for (i = 3; i < NFD; i++) fdent[i] = -1;
  fd0ent = ie;
  fd0pos = ipos;
  fd1ent = oe;
  fd1pos = opos;
  return 0;
}

/* 子の exit。親を復元し，親の a0 へ渡す終了コードを返す */
int spexit(int code) {
  unsigned b;
  unsigned imgsz;
  unsigned psp;
  unsigned stksz;
  int i;
  depth = depth - 1;
  b = sbase[depth];
  wcopy(TFA, b, 132);
  for (i = 0; i < NFD; i++) {
    fdent[i] = (int)ld4(b + 132 + i * 4);
    fdpos[i] = (int)ld4(b + 196 + i * 4);
  }
  fd0ent = (int)ld4(b + 260);
  fd0pos = (int)ld4(b + 264);
  fd1ent = (int)ld4(b + 268);
  fd1pos = (int)ld4(b + 272);
  ubrk = ld4(b + 276);
  psp = ld4(b + 280);
  imgsz = ld4(b + 284);
  stksz = ld4(b + 288);
  wcopy(UBASE, b + 292, imgsz);
  wcopy(psp, b + 292 + imgsz, stksz);
  savecur = b;
  return code & 255;
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
  r = 0;
  if (n == SYS_WRITE) r = sys_write((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_READ) r = sys_read((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_OPENAT) r = sys_openat((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_CLOSE) r = sys_close((int)tf[9]);
  else if (n == SYS_LSEEK) r = sys_lseek((int)tf[9], (int)tf[10], (int)tf[11]);
  else if (n == SYS_BRK) r = (int)sys_brk(tf[9]);
  else if (n == SYS_SPAWN) r = sys_spawn(tf[9]);
  else if (n == SYS_EXIT) {
    if (depth > 0) r = spexit((int)tf[9]);      /* 子の終わり。親へ戻る */
    else exit((int)tf[9]);
  }
  else r = 0 - ENOSYS;
  tf[9] = (unsigned)r;          /* a0 = 結果 (spawn 直後は子の a0。未使用) */
  return 0;
}

/* ---- 起動 ---- */

int main(void) {
  unsigned *tf;
  unsigned entry;
  int i;
  int b;
  int n;
  int used;
  char name[64];

  if (ld4(SFSA) != 0x31736673) {        /* 'sfs1' */
    putc('?');
    putc('\n');
    return 1;
  }
  tblo = (int)ld4(SFSA + 8);
  tbln = (int)ld4(SFSA + 12);
  for (i = 0; i < NFD; i++) fdent[i] = -1;
  fd0ent = -1;
  fd1ent = -1;
  depth = 0;
  savecur = SAVEA;

  /* 起動するプログラムは sfs 内の boot に 1 行で書く。空白で区切り，
   * 引数として渡す (docs/stage013-tools.md 3.5) */
  b = sfsfind("boot");
  if (b < 0) { putc('B'); putc('\n'); return 1; }
  n = (int)ld4(ent(b) + E_LEN);
  if (n > 63) n = 63;
  for (i = 0; i < n; i++) name[i] = *(char *)(SFSA + ld4(ent(b) + E_OFF) + i);
  while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r')) n = n - 1;
  name[n] = 0;

  kargc = 0;
  used = 0;
  i = 0;
  while (kargc < 8) {
    while (name[i] == ' ' || name[i] == '\t') i = i + 1;
    if (name[i] == 0) break;
    kargo[kargc] = used;
    while (name[i] != 0 && name[i] != ' ' && name[i] != '\t') {
      kargs[used] = name[i];
      used = used + 1;
      i = i + 1;
    }
    kargs[used] = 0;
    used = used + 1;
    kargc = kargc + 1;
  }
  if (kargc == 0) { putc('N'); putc('\n'); return 1; }

  i = sfsfind(kargs);                   /* 最初の語 (kargo[0] = 0) */
  if (i < 0) { putc('N'); putc('\n'); return 1; }
  entry = loadelf(i);
  if (entry == 0) { putc('L'); putc('\n'); return 1; }

  /* トラップフレームを仕立てて U モードへ渡す */
  tf = (unsigned *)TFA;
  for (i = 0; i < 33; i++) tf[i] = 0;
  tf[1] = setupstack();                 /* x2 = sp */
  tf[31] = entry;                       /* mepc */
  urun();
  return 0;
}
