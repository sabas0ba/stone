/// @file ld18.sc
/// @brief リンカ (Stage 17 第 5 部世代)。GCC の cc1 (128 MB を超えるコード) を組む。
///
/// ld17 は 1 オブジェクトの tcc を組む大きさで作ってあり，cc1 には届かない
/// (docs/stage017-gcc.md 8.15)。
///
/// | | ld17 | ld18 |
/// |---|---|---|
/// | 入力 | 標準入力 (UART)・8 MiB | RAM 上の 384 MiB |
/// | 出力 | 標準出力 (UART)・8 MiB | RAM 上の 384 MiB |
/// | オブジェクト | 64 個 | 8192 個 |
/// | 大域記号 | 8192 個・線形探索・名前 63 字 | 262144 個・ハッシュ・名前の長さに上限なし |
/// | ライブラリ | なし (渡したものはすべて組む) | 未定義の名前を定義する部品だけを取り込む |
/// | main の呼出し | jal (前後 1 MiB) | 前置部の末尾の踏み台から lui + jalr |
/// | データスタック | 256 KiB 固定 | 入力の頭で指定 |
/// | 出力形式 | 'F' / 'K' / 'E' | 'E' だけ |
///
/// @section io 入出力は RAM を介する
/// 200 MB を超える入出力を UART で 1 バイトずつ通すと時間がかかり，出力は
/// ホスト側が詰まると QEMU が途中を捨てる (tools/run-qemu.sh)。ld18 は
/// QEMU の RAM をホストのファイルで裏づけ (STONE_QEMU_RAMFILE)，その中の
/// 決まった番地で受け渡す。詰めて取り出すのは tools/ld18.sh である。
///
///   0x9000_0000  入力 (384 MiB)
///     +0   'LDI1'
///     +4   全体の長さ (バイト。頭を含む)
///     +8   出力形式 ('E')
///     +12  データスタックの大きさ (バイト)
///     +16  記録の列。記録 = 長さ (4) + 印 (4) + オブジェクト (4 の倍数へ詰める)
///          印の bit 0 が 1 ならライブラリの部品 (要るときだけ組む)
///   0xa800_0000  出力の頭: [0] 'LD18' [1] イメージの長さ
///   0xa800_1000  出力イメージ (384 MiB 弱)
///
/// 標準出力 (UART) には診断と経過だけを書く。
///
/// @section far main への呼出し
/// 'E' の前置部は ld17 と同じ語の並びで，main を呼ぶ語 (ehsz + 32) だけが
/// 違う。ld17 は main への jal を直に埋める。ld18 は前置部の末尾に
///
///   lui  x31, %hi(main)
///   jalr x0, %lo(main)(x31)
///
/// の踏み台を置き，そこへ jal する。踏み台は ra を変えないので，main は
/// 前置部の続き (exit) へ戻る。x31 は cc15am の遠距離呼出しと同じく，
/// 生成コードが値を置かないレジスタである。
///
/// @section order 置く順
/// オブジェクトは入力の順に置く。前置部の syscall スタブ (getc / sys_read …) は
/// jal で呼ばれるので，**それを呼ぶ libc の部品は前置部の近く (1 MiB 以内) に
/// 置く**。tools/ld18.sh は libc と実行時ルーチンを先に並べる。遠距離呼出しで
/// 訳した単位 (cc15am の `#pragma stone far_call`) はどこに置いてもよい。

// ---- 領域 ----
char *inp;                ///< 入力 (0x9000_0000)
char *img;                ///< 出力イメージ (0xa800_1000)
int *ohd;                 ///< 出力の頭 (0xa800_0000)
int inn;                  ///< 入力の全体の長さ
int imgn;                 ///< 出力イメージの長さ
int litp;                 ///< 組込みの名前を置く位置 (inp の入力の後ろ)

int nobj;                 ///< オブジェクト数
int oof[8192];            ///< 各オブジェクトの inp 内開始位置
int olib[8192];           ///< 1 = ライブラリの部品
int ouse[8192];           ///< 1 = 組む
int obtx[8192];           ///< .text の配置オフセット
int obbs[8192];           ///< .bss の配置オフセット

int gnm[262144];          ///< 大域記号の名前 (inp 内の位置)
int gad[262144];          ///< 配置オフセット
int gdef[262144];         ///< 0 = 参照だけ, 1 = オブジェクトが定義, 2 = 前置部が定義
int gobj[262144];         ///< 定義したオブジェクト
int gnext[262144];        ///< 同じハッシュの次 (番号 + 1。0 = 終わり)
int ghead[262144];        ///< ハッシュ -> 最初の番号 + 1
int gcnt;                 ///< 大域記号の数

int symad[262144];        ///< 処理中のオブジェクトの記号番号 -> 配置オフセット (-1 = 未定義)
int symnm[262144];        ///< 未定義の記号の名前 (inp 内の位置)

int base;                 ///< ロードアドレス (0x8600_0000)
int prosz;                ///< 前置部の大きさ (ELF ヘッダと踏み台を含む)
int ehsz;                 ///< ELF ヘッダ + プログラムヘッダの大きさ
int ebss;                 ///< .bss の終端オフセット
int dstk;                 ///< データスタックの大きさ
int nerr;                 ///< 控えた誤りの数 (jal の範囲)

// ---- 入力の読取り ----

/// @brief inp から 1 / 2 / 4 バイトを読む (リトルエンディアン)。
int rd1(int p) { return inp[p]; }
int rd2(int p) { return inp[p] | (inp[p + 1] << 8); }
int rd4(int p) {
  return inp[p] | (inp[p + 1] << 8) | (inp[p + 2] << 16) | (inp[p + 3] << 24);
}

/// @brief オブジェクト o の節 n の欄 f を読む (16 = 位置, 20 = 大きさ, 28 = info)。
int shf(int o, int n, int f) {
  return rd4(oof[o] + rd4(oof[o] + 32) + n * 40 + f);
}

// ---- 診断 ----

/// @brief 文字列を標準出力へ書く。
int msg(char *m) {
  int i;
  i = 0;
  while (m[i]) { putc(m[i]); i = i + 1; }
  return 0;
}

/// @brief inp 内の位置 p の名前を書く。
int msgnm(int p) {
  while (inp[p]) { putc(inp[p]); p = p + 1; }
  return 0;
}

/// @brief 符号なしの 10 進で書く。
int msgnum(int v) {
  char b[12];
  int n;
  n = 0;
  if (v == 0) { putc('0'); return 0; }
  while (v != 0) {
    b[n] = '0' + ((unsigned)v % 10);
    v = (unsigned)v / 10;
    n = n + 1;
  }
  while (n > 0) { n = n - 1; putc(b[n]); }
  return 0;
}

/// @brief オブジェクト o の誤りを「ld18: <m> (オブジェクト <o>): <名前>」の形で書く。
int msgobj(char *m, int o, int p) {
  msg("ld18: ");
  msg(m);
  msg(" (object ");
  msgnum(o);
  msg("): ");
  msgnm(p);
  putc('\n');
  return 0;
}

// ---- 大域記号 ----

/// @brief inp 内の 2 つの名前が同じか。
int nmeq(int a, int b) {
  while (inp[a] && inp[a] == inp[b]) { a = a + 1; b = b + 1; }
  return inp[a] == inp[b];
}

/// @brief 名前のハッシュ (18 bit)。
int nmhash(int p) {
  int h;
  h = 0;
  while (inp[p]) { h = (h * 31 + inp[p]) & 262143; p = p + 1; }
  return h;
}

/// @brief 名前で大域記号を探す。無ければ -1。
int gfind(int p) {
  int i;
  i = ghead[nmhash(p)];
  while (i) {
    if (nmeq(gnm[i - 1], p)) return i - 1;
    i = gnext[i - 1];
  }
  return -1;
}

/// @brief 名前を参照だけの記号として登録する (既にあればその番号)。
int gref(int p) {
  int g; int h;
  g = gfind(p);
  if (g >= 0) return g;
  if (gcnt > 262143) { msg("ld18: too many global symbols\n"); exit(6); }
  g = gcnt;
  gcnt = gcnt + 1;
  h = nmhash(p);
  gnm[g] = p;
  gad[g] = 0;
  gdef[g] = 0;
  gobj[g] = -1;
  gnext[g] = ghead[h];
  ghead[h] = g + 1;
  return g;
}

/// @brief オブジェクト o が名前 p を定義する。二度目の定義は誤り (3)。
int gdefine(int p, int o) {
  int g;
  g = gref(p);
  if (gdef[g]) { msgobj("duplicate symbol", o, p); exit(3); }
  gdef[g] = 1;
  gobj[g] = o;
  return g;
}

/// @brief 組込みの名前 (前置部のスタブ) を inp の入力の後ろへ写して定義する。
int gaddlit(char *nm, int ad) {
  int i; int p; int g;
  p = litp;
  i = 0;
  while (nm[i]) { inp[litp] = nm[i]; litp = litp + 1; i = i + 1; }
  inp[litp] = 0;
  litp = litp + 1;
  g = gref(p);
  gdef[g] = 2;
  gad[g] = ad;
  return 0;
}

/// @brief 名前 (リテラル) で探す。
int gfindlit(char *nm) {
  int i; int p;
  p = litp;
  i = 0;
  while (nm[i]) { inp[p + i] = nm[i]; i = i + 1; }
  inp[p + i] = 0;
  return gfind(p);
}

// ---- 入力の分解 ----

/// @brief 入力の頭と記録の列を読み，オブジェクトを数える。
int readobjs() {
  int p; int sz;
  if (rd4(0) != 0x3149444c) { msg("ld18: bad input magic\n"); exit(7); }
  inn = rd4(4);
  if (rd4(8) != 'E') { msg("ld18: only format E is supported\n"); exit(7); }
  dstk = rd4(12);
  p = 16;
  nobj = 0;
  while (p < inn) {
    sz = rd4(p);
    if (nobj > 8191) { msg("ld18: too many objects\n"); exit(6); }
    oof[nobj] = p + 8;
    olib[nobj] = rd4(p + 4) & 1;
    ouse[nobj] = 0;
    // cc の .o は節が 7 つで，並びが決まっている
    // (1 .text / 2 .bss / 3 .symtab / 4 .strtab / 5 .rela.text)
    if (rd4(oof[nobj]) != 0x464c457f || rd2(oof[nobj] + 16) != 1
        || rd2(oof[nobj] + 18) != 243 || shf(nobj, 3, 4) != 2
        || shf(nobj, 5, 4) != 4) {
      msg("ld18: not a stone object: ");
      msgnum(nobj);
      putc('\n');
      exit(1);
    }
    nobj = nobj + 1;
    p = p + 8 + ((sz + 3) & (0 - 4));
  }
  litp = inn;
  return 0;
}

// ---- 取り込む部品を決める ----

/// @brief オブジェクト o を組む。定義を表に入れ，再配置が使う未定義の名前を参照として入れる。
/// @note 宣言だけで使われない名前 (記号表にはある) は参照に数えない。数えると
///       使わない関数のためにライブラリの部品を引き込み，その部品の参照で
///       さらに引き込む。
int include(int o) {
  int n; int sy; int st; int ns; int nl; int rp; int nr; int k;
  ouse[o] = 1;
  sy = oof[o] + shf(o, 3, 16);
  ns = shf(o, 3, 20) / 16;
  st = oof[o] + shf(o, 4, 16);
  nl = shf(o, 3, 28);
  n = nl;
  while (n < ns) {
    if (rd2(sy + n * 16 + 14) != 0) gdefine(st + rd4(sy + n * 16), o);
    n = n + 1;
  }
  rp = oof[o] + shf(o, 5, 16);
  nr = shf(o, 5, 20) / 12;
  n = 0;
  while (n < nr) {
    k = (rd4(rp + n * 12 + 4) >> 8) & 16777215;
    if (k >= nl && rd2(sy + k * 16 + 14) == 0) gref(st + rd4(sy + k * 16));
    n = n + 1;
  }
  return 0;
}

/// @brief ライブラリの部品 o が，まだ定義されていない参照を定義するか。
int wanted(int o) {
  int n; int sy; int st; int ns; int g;
  sy = oof[o] + shf(o, 3, 16);
  ns = shf(o, 3, 20) / 16;
  st = oof[o] + shf(o, 4, 16);
  n = shf(o, 3, 28);
  while (n < ns) {
    if (rd2(sy + n * 16 + 14) != 0) {
      g = gfind(st + rd4(sy + n * 16));
      if (g >= 0 && gdef[g] == 0) return 1;
    }
    n = n + 1;
  }
  return 0;
}

/// @brief ライブラリでない入力をすべて組み，参照が尽きるまで部品を取り込む。
int selectobjs() {
  int o; int more;
  o = 0;
  while (o < nobj) {
    if (!olib[o]) include(o);
    o = o + 1;
  }
  more = 1;
  while (more) {
    more = 0;
    o = 0;
    while (o < nobj) {
      if (olib[o] && !ouse[o] && wanted(o)) { include(o); more = 1; }
      o = o + 1;
    }
  }
  return 0;
}

/// @brief 定義されないまま参照された名前を全部書き，あれば 2 で止める。
int undefs() {
  int g; int n;
  n = 0;
  g = 0;
  while (g < gcnt) {
    if (gdef[g] == 0) {
      msg("ld18: undefined symbol: ");
      msgnm(gnm[g]);
      putc('\n');
      n = n + 1;
    }
    g = g + 1;
  }
  if (n) exit(2);
  return 0;
}

// ---- 配置 ----

/// @brief 組むオブジェクトの .text を前置部の後ろへ，.bss をその後ろへ並べる。
int layout() {
  int i; int p;
  p = prosz;
  i = 0;
  while (i < nobj) {
    if (ouse[i]) {
      obtx[i] = p;
      p = p + shf(i, 1, 20);
      p = (p + 3) & (0 - 4);
    }
    i = i + 1;
  }
  imgn = p;
  i = 0;
  while (i < nobj) {
    if (ouse[i]) {
      obbs[i] = p;
      p = p + shf(i, 2, 20);
      p = (p + 3) & (0 - 4);
    }
    i = i + 1;
  }
  ebss = p;
  return 0;
}

/// @brief オブジェクトが定義した大域記号に配置オフセットを与える。
int place() {
  int i; int n; int sy; int st; int ns; int shn; int g;
  i = 0;
  while (i < nobj) {
    if (ouse[i]) {
      sy = oof[i] + shf(i, 3, 16);
      ns = shf(i, 3, 20) / 16;
      st = oof[i] + shf(i, 4, 16);
      n = shf(i, 3, 28);
      while (n < ns) {
        shn = rd2(sy + n * 16 + 14);
        if (shn != 0) {
          g = gfind(st + rd4(sy + n * 16));
          if (shn == 1) gad[g] = obtx[i] + rd4(sy + n * 16 + 4);
          else gad[g] = obbs[i] + rd4(sy + n * 16 + 4);
        }
        n = n + 1;
      }
    }
    i = i + 1;
  }
  return 0;
}

// ---- 出力と再配置 ----

/// @brief img へ 32 / 16 bit をリトルエンディアンで書く。img から 32 bit を読む。
int iw4(int p, int w) {
  img[p] = w & 255;
  img[p + 1] = (w >> 8) & 255;
  img[p + 2] = (w >> 16) & 255;
  img[p + 3] = (w >> 24) & 255;
  return 0;
}
int iw2(int p, int w) {
  img[p] = w & 255;
  img[p + 1] = (w >> 8) & 255;
  return 0;
}
int ir4(int p) {
  return img[p] | (img[p + 1] << 8) | (img[p + 2] << 16) | (img[p + 3] << 24);
}


/// @brief J 形式の即値を命令語のビット位置へ散らす。
int jenc(int rel) {
  return (((rel >> 20) & 1) << 31) | (((rel >> 1) & 1023) << 21)
       | (((rel >> 11) & 1) << 20) | (((rel >> 12) & 255) << 12);
}

/// @brief 'E' ユーザ前置部を img へ置く (ELF ヘッダの直後から)。ld17 と同じ語の並び。
/// @return 常に 0
/// @note データスタックは .bss の後ろにリンカが割り付ける (patchdstk が埋める)。
///       フレームスタックはカーネルが与えた sp をそのまま使う。
///       sp 上の argc / argv を本処理系の呼出し規約で main へ渡し，
///       返却値を exit へ渡す。syscall スタブは a7 に番号を置いて ecall する。
int prologuee() {
  int p;
  p = ehsz;
  iw4(p, 0x862004b7); p = p + 4;
  iw4(p, 0x00048493); p = p + 4;
  iw4(p, 0x00012503); p = p + 4;
  iw4(p, 0x00410593); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00b4a023); p = p + 4;
  iw4(p, 0x00000000); p = p + 4;
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00448493); p = p + 4;
  iw4(p, 0x05d00893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0000006f); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0x0084a603); p = p + 4;
  iw4(p, 0x00c4a583); p = p + 4;
  iw4(p, 0x0104a503); p = p + 4;
  iw4(p, 0x03f00893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x01448493); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0x0084a603); p = p + 4;
  iw4(p, 0x00c4a583); p = p + 4;
  iw4(p, 0x0104a503); p = p + 4;
  iw4(p, 0x04000893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x01448493); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0x0084a683); p = p + 4;
  iw4(p, 0x00c4a603); p = p + 4;
  iw4(p, 0x0104a583); p = p + 4;
  iw4(p, 0x0144a503); p = p + 4;
  iw4(p, 0x03800893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x01848493); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0x0084a503); p = p + 4;
  iw4(p, 0x03900893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x00c48493); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0x0084a503); p = p + 4;
  iw4(p, 0x0d600893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x00c48493); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x0004a023); p = p + 4;
  iw4(p, 0x00000513); p = p + 4;
  iw4(p, 0x00048593); p = p + 4;
  iw4(p, 0x00100613); p = p + 4;
  iw4(p, 0x03f00893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004c503); p = p + 4;
  iw4(p, 0x00448493); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x00848493); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xff848493); p = p + 4;
  iw4(p, 0x00d4a023); p = p + 4;
  iw4(p, 0x0114a223); p = p + 4;
  iw4(p, 0x00100513); p = p + 4;
  iw4(p, 0x00848593); p = p + 4;
  iw4(p, 0x00100613); p = p + 4;
  iw4(p, 0x04000893); p = p + 4;
  iw4(p, 0x00000073); p = p + 4;
  iw4(p, 0x0004a683); p = p + 4;
  iw4(p, 0x0044a883); p = p + 4;
  iw4(p, 0x00848493); p = p + 4;
  iw4(p, 0x0004a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  // sys_ecall(n, a, b, c): a7 = n, a0..a2 = a, b, c で ecall する汎用スタブ
  // (ehsz + 448)。x17 (a7) は callee-saved なのでデータスタックへ退避する
  // (x13 の退避は他のスタブと形を揃えるためで，ここでは使っていない)。
  // 引数は積まれた順の逆で 8(x9) = c, 12(x9) = b, 16(x9) = a, 20(x9) = n
  iw4(p, 0xff848493); p = p + 4;    // addi x9, x9, -8
  iw4(p, 0x00d4a023); p = p + 4;    // sw   x13, 0(x9)
  iw4(p, 0x0114a223); p = p + 4;    // sw   x17, 4(x9)
  iw4(p, 0x0084a603); p = p + 4;    // lw   x12, 8(x9)   (c -> a2)
  iw4(p, 0x00c4a583); p = p + 4;    // lw   x11, 12(x9)  (b -> a1)
  iw4(p, 0x0104a503); p = p + 4;    // lw   x10, 16(x9)  (a -> a0)
  iw4(p, 0x0144a883); p = p + 4;    // lw   x17, 20(x9)  (n -> a7)
  iw4(p, 0x00000073); p = p + 4;    // ecall
  iw4(p, 0x0004a683); p = p + 4;    // lw   x13, 0(x9)
  iw4(p, 0x0044a883); p = p + 4;    // lw   x17, 4(x9)
  iw4(p, 0x01848493); p = p + 4;    // addi x9, x9, 24   (退避 8 + 引数 16)
  iw4(p, 0xffc48493); p = p + 4;    // addi x9, x9, -4
  iw4(p, 0x00a4a023); p = p + 4;    // sw   x10, 0(x9)   (返り値を積む)
  iw4(p, 0x00008067); p = p + 4;    // ret
  return 0;
}

/// @brief 前置部の main 呼出しを埋める (@ref far)。
/// @note ehsz + 32 の語を前置部の末尾の踏み台への jal にし，踏み台に
///       lui x31, %hi(main) / jalr x0, %lo(main)(x31) を置く。
int patchmain() {
  int g; int at; int tr; int v;
  g = gfindlit("main");
  if (g < 0 || gdef[g] == 0) { msg("ld18: undefined symbol: main\n"); exit(5); }
  at = ehsz + 32;
  tr = ehsz + 504;
  iw4(at, jenc(tr - at) | 0xef);
  v = base + gad[g];
  iw4(tr, (((v + 2048) >> 12) << 12) | (31 << 7) | 55);
  iw4(tr + 4, ((v & 4095) << 20) | (31 << 15) | 103);
  return 0;
}

/// @brief データスタックの上端を前置部へ埋める (.bss の後ろに dstk バイト)。
int patchdstk() {
  int v;
  v = base + ebss + dstk;
  iw4(ehsz, ((v + 2048) >> 12 << 12) | (9 << 7) | 55);
  iw4(ehsz + 4, ((v & 4095) << 20) | (9 << 15) | (9 << 7) | 19);
  return 0;
}

/// @brief ELF 実行形式のヘッダとプログラムヘッダを置く (ld17 と同じ。memsz のデータスタックだけ違う)。
int elfhdr() {
  img[0] = 127; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
  img[4] = 1;
  img[5] = 1;
  img[6] = 1;
  iw2(16, 2);
  iw2(18, 243);
  iw4(20, 1);
  iw4(24, base + ehsz);
  iw4(28, 52);
  iw4(32, 0);
  iw4(36, 0);
  iw2(40, 52);
  iw2(42, 32);
  iw2(44, 1);
  iw2(46, 0); iw2(48, 0); iw2(50, 0);
  iw4(52, 1);
  iw4(56, 0);
  iw4(60, base);
  iw4(64, base);
  iw4(68, imgn);
  iw4(72, ebss + dstk);
  iw4(76, 7);
  iw4(80, 4096);
  return 0;
}

/// @brief 組むオブジェクトの .text を img へ写す。
int copytext() {
  int i; int n; int sz; int src; int dst;
  i = 0;
  while (i < nobj) {
    if (ouse[i]) {
      src = oof[i] + shf(i, 1, 16);
      dst = obtx[i];
      sz = shf(i, 1, 20);
      n = 0;
      while (n < sz) {
        img[dst + n] = inp[src + n];
        n = n + 1;
      }
    }
    i = i + 1;
  }
  return 0;
}

/// @brief 再配置を 1 件適用する。o と k は jal の範囲を外れたときの診断に使う。
int applyrel(int p, int ty, int s, int a, int o, int nm) {
  int v; int w; int rel;
  if (ty == 1) {                     // R_RISCV_32
    iw4(p, base + s + a);
    return 0;
  }
  if (ty == 17) {                    // R_RISCV_JAL
    rel = (s + a) - p;
    if (rel >= 1048576 || rel < (0 - 1048576)) {
      // **最初の 1 つで止めない。** 届かない呼出しを全部並べる。
      // 遠距離呼出しで訳していない単位がどれかが 1 回で判る
      if (nerr < 64) msgobj("jal out of range", o, nm);
      nerr = nerr + 1;
      return 0;
    }
    iw4(p, ir4(p) | jenc(rel));
    return 0;
  }
  v = base + s + a;
  if (ty == 26) {                    // R_RISCV_HI20
    w = ir4(p) & 4095;
    iw4(p, w | (((v + 2048) >> 12) << 12));
    return 0;
  }
  if (ty == 27) {                    // R_RISCV_LO12_I
    w = ir4(p) & 1048575;
    iw4(p, w | ((v & 4095) << 20));
    return 0;
  }
  msg("ld18: unknown relocation type ");
  msgnum(ty);
  putc('\n');
  exit(1);
  return 0;
}

/// @brief オブジェクト o の記号番号 -> 配置オフセットの表を作る。
int mksymad(int o) {
  int n; int sy; int st; int ns; int shn; int g;
  sy = oof[o] + shf(o, 3, 16);
  ns = shf(o, 3, 20) / 16;
  st = oof[o] + shf(o, 4, 16);
  if (ns > 262143) { msg("ld18: too many symbols in one object\n"); exit(6); }
  n = 0;
  while (n < ns) {
    shn = rd2(sy + n * 16 + 14);
    symnm[n] = st + rd4(sy + n * 16);
    if (shn == 1) symad[n] = obtx[o] + rd4(sy + n * 16 + 4);
    else if (shn == 2) symad[n] = obbs[o] + rd4(sy + n * 16 + 4);
    else {
      symad[n] = -1;
      if (n) {
        g = gfind(symnm[n]);
        if (g >= 0 && gdef[g]) symad[n] = gad[g];
      }
    }
    n = n + 1;
  }
  return 0;
}

/// @brief 組むオブジェクトの再配置をすべて適用する。
int relocate() {
  int i; int n; int nr; int rp; int info; int k;
  i = 0;
  while (i < nobj) {
    if (ouse[i]) {
      mksymad(i);
      rp = oof[i] + shf(i, 5, 16);
      nr = shf(i, 5, 20) / 12;
      n = 0;
      while (n < nr) {
        info = rd4(rp + n * 12 + 4);
        k = (info >> 8) & 16777215;
        // 未定義は select の後の undefs で尽きているはずだが，表に
        // 残っていれば名前を言って止める
        if (symad[k] == -1) { msgobj("undefined symbol", i, symnm[k]); exit(2); }
        applyrel(obtx[i] + rd4(rp + n * 12), info & 255, symad[k],
                 rd4(rp + n * 12 + 8), i, symnm[k]);
        n = n + 1;
      }
    }
    i = i + 1;
  }
  if (nerr) {
    msg("ld18: ");
    msgnum(nerr);
    msg(" jal relocations out of range\n");
    exit(4);
  }
  return 0;
}

// ---- 駆動部 ----

/// @brief 経過を 1 行で書く。
int report() {
  int i; int n; int ln;
  n = 0;
  ln = 0;
  i = 0;
  while (i < nobj) {
    if (ouse[i]) { n = n + 1; if (olib[i]) ln = ln + 1; }
    i = i + 1;
  }
  msg("ld18: objects ");
  msgnum(nobj);
  msg(" used ");
  msgnum(n);
  msg(" (library ");
  msgnum(ln);
  msg(") globals ");
  msgnum(gcnt);
  msg(" text ");
  msgnum(imgn);
  msg(" memsz ");
  msgnum(ebss + dstk);
  putc('\n');
  return 0;
}

/// @brief リンカ本体。RAM 上の入力から 'E' の実行形式を RAM 上へ書く。
int main() {
  inp = (char *)0x90000000;
  ohd = (int *)0xa8000000;
  img = (char *)0xa8001000;
  ohd[0] = 0;
  ohd[1] = 0;
  gcnt = 0;
  nerr = 0;
  ehsz = 84;
  base = 0x86000000;
  prosz = ehsz + 512;                // ld17 の前置部 504 + 踏み台 8
  readobjs();
  gaddlit("getc", ehsz + 324);
  gaddlit("putc", ehsz + 396);
  gaddlit("exit", ehsz + 36);
  gaddlit("sys_read", ehsz + 56);
  gaddlit("sys_write", ehsz + 112);
  gaddlit("sys_openat", ehsz + 168);
  gaddlit("sys_close", ehsz + 228);
  gaddlit("sys_brk", ehsz + 276);
  gaddlit("sys_ecall", ehsz + 448);
  selectobjs();
  undefs();
  layout();
  place();
  report();
  if (imgn > 402644992) { msg("ld18: image too large\n"); exit(6); }
  prologuee();
  copytext();
  patchmain();
  patchdstk();
  elfhdr();
  relocate();
  ohd[1] = imgn;
  ohd[0] = 0x3831444c;
  return 0;
}
