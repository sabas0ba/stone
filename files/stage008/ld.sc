/// @file ld.sc
/// @brief ELF リロケータブルオブジェクトを結合して実行像を作るリンカ。
///
/// 設計は docs/stage008-elf-ld.md。cc が出力した ET_REL オブジェクトを
/// 複数受け取り，配置を決め，シンボルを解決し，再配置を適用して
/// ロードアドレス 0x8000_0000 のフラットバイナリを出力する。
///
/// @section input 入力の与え方
/// 実行環境は UART のみでファイルを開けないため，オブジェクトを連結して
/// 標準入力から受け取る。オブジェクトは自己記述的 (ELF ヘッダから全体長を
/// 算出できる) なので順に切り出せる。列の終わりは 0x7f 以外の 1 バイトで示す
/// (ELF の先頭バイトが 0x7f なので，それ以外なら「次は無い」と判る)。
///
///   cat a.o b.o; printf '\\0'   をリンカへ流し込む
///
/// @section layout 配置
///   [0]      ランタイム前置部 32 語 (crt0 相当。本ファイルが持つ)
///   [128..]  各オブジェクトの .text を入力順に連結
///   [....]   .bss を連結 (NOBITS なので出力はしない)
///
/// @section addr アドレスの扱い
/// 内部ではすべて「像の先頭からのオフセット」で計算し，0x8000_0000 は
/// 再配置の値を作る瞬間にだけ足す。sc に符号なし整数がないため，
/// 0x8000_0000 を含む値を大小比較すると負数として扱われてしまうからである。
/// 右シフトは論理シフトなので，値を作る側の計算は最上位ビットが立っていても正しい。

// ---- 領域 ----
char inp[1048576];        ///< 入力オブジェクトを連結して保持する
int inn;                  ///< inp の有効長
char img[1048576];        ///< 出力する実行像
int imgn;                 ///< img の有効長

int oof[64];              ///< 各オブジェクトの inp 内開始位置
int nobj;                 ///< オブジェクト数
int obtx[64];             ///< 各オブジェクトの .text の最終配置オフセット
int obbs[64];             ///< 各オブジェクトの .bss の最終配置オフセット

char gnm[32768];          ///< 大域シンボル名 (16 バイト x 2048)
int gad[2048];            ///< 大域シンボルの最終オフセット
int gcnt;                 ///< 大域シンボル数

int symad[8192];          ///< 処理中のオブジェクトのシンボル番号 -> 最終オフセット

int base;                 ///< ロードアドレス 0x8000_0000
int prosz;                ///< ランタイム前置部の大きさ (バイト)

// ---- 入力の読取り ----

/// @brief inp から 1 バイト読む。
int rd1(int p) { return inp[p]; }
/// @brief inp から 16 bit をリトルエンディアンで読む。
int rd2(int p) { return inp[p] | (inp[p + 1] << 8); }
/// @brief inp から 32 bit をリトルエンディアンで読む。
int rd4(int p) {
  return inp[p] | (inp[p + 1] << 8) | (inp[p + 2] << 16) | (inp[p + 3] << 24);
}

/// @brief オブジェクト o のセクションヘッダ n のフィールドを読む。
/// @param o オブジェクト番号
/// @param n セクション番号
/// @param f フィールドのバイトオフセット (0=name 4=type 16=offset 20=size 24=link 28=info)
/// @return フィールドの値
int shf(int o, int n, int f) {
  return rd4(oof[o] + rd4(oof[o] + 32) + n * 40 + f);
}

/// @brief オブジェクトを標準入力から読み込む。
/// @return 常に 0
/// @note ELF ヘッダの e_shoff と e_shnum から全体長を求め，その分だけ読む。
///       これを繰り返し，先頭バイトが 0x7f でなくなったら終わりとする。
int readobjs() {
  int c; int i; int tot;
  inn = 0;
  nobj = 0;
  c = getc();
  while (c == 127) {
    if (nobj > 63) exit(6);
    oof[nobj] = inn;
    inp[inn] = c;
    inn = inn + 1;
    // マジックは読んだ直後に検査する。ヘッダを読み切ってから調べると，
    // ELF でない短い入力に対して残りのバイトを待ち続けてしまう
    inp[inn] = getc(); if (inp[inn] != 'E') exit(1); inn = inn + 1;
    inp[inn] = getc(); if (inp[inn] != 'L') exit(1); inn = inn + 1;
    inp[inn] = getc(); if (inp[inn] != 'F') exit(1); inn = inn + 1;
    i = 0;
    while (i < 48) { inp[inn] = getc(); inn = inn + 1; i = i + 1; }
    if (rd2(oof[nobj] + 16) != 1) exit(1);      // e_type = ET_REL
    if (rd2(oof[nobj] + 18) != 243) exit(1);    // e_machine = EM_RISCV
    tot = rd4(oof[nobj] + 32) + rd2(oof[nobj] + 48) * 40;
    while (inn < oof[nobj] + tot) {
      if (inn > 1048575) exit(6);
      inp[inn] = getc();
      inn = inn + 1;
    }
    nobj = nobj + 1;
    c = getc();
  }
  return 0;
}

// ---- シンボル ----

/// @brief 大域シンボルを名前で探す。
/// @param p .strtab 内の名前の位置 (inp 内の絶対位置)
/// @return シンボル番号。見つからなければ -1
int gfind(int p) {
  int i; int j; int k;
  i = 0;
  while (i < gcnt) {
    j = 0;
    k = 1;
    while (j < 16) {
      if (gnm[i * 16 + j] != inp[p + j]) { k = 0; j = 16; }
      else if (gnm[i * 16 + j] == 0) j = 16;
      else j = j + 1;
    }
    if (k) return i;
    i = i + 1;
  }
  return -1;
}

/// @brief 大域シンボルを登録する。
/// @param p .strtab 内の名前の位置 (inp 内の絶対位置)
/// @param ad 最終オフセット
/// @return 常に 0
/// @note 既に定義済みの名前が来たら多重定義エラー (3) とする。
int gadd(int p, int ad) {
  int i; int e;
  if (gfind(p) >= 0) exit(3);
  if (gcnt > 2047) exit(6);
  e = gcnt;
  gcnt = gcnt + 1;
  i = 0;
  while (i < 15) {
    gnm[e * 16 + i] = inp[p + i];
    if (inp[p + i] == 0) i = 15;
    else i = i + 1;
  }
  gnm[e * 16 + 15] = 0;
  gad[e] = ad;
  return 0;
}

/// @brief 名前 (リテラル) で大域シンボルを探す。
/// @param nm 探す名前
/// @return シンボル番号。見つからなければ -1
int gfindlit(char *nm) {
  int i; int j; int k;
  i = 0;
  while (i < gcnt) {
    j = 0;
    k = 1;
    while (j < 16) {
      if (gnm[i * 16 + j] != nm[j]) { k = 0; j = 16; }
      else if (nm[j] == 0) j = 16;
      else j = j + 1;
    }
    if (k) return i;
    i = i + 1;
  }
  return -1;
}

/// @brief リンカが提供する組込みシンボルを登録する。
/// @param nm 名前
/// @param ad 前置部内のオフセット
/// @return 常に 0
/// @note gadd は inp 内の名前を前提にするので，リテラル用に別途用意する。
int gaddlit(char *nm, int ad) {
  int i; int e;
  if (gcnt > 2047) exit(6);
  e = gcnt;
  gcnt = gcnt + 1;
  i = 0;
  while (i < 15) {
    gnm[e * 16 + i] = nm[i];
    if (nm[i] == 0) i = 15;
    else i = i + 1;
  }
  gnm[e * 16 + 15] = 0;
  gad[e] = ad;
  return 0;
}

// ---- 配置とシンボル解決 ----

/// @brief 各オブジェクトの .text と .bss の最終位置を決める。
/// @return 常に 0
/// @note .text は前置部の直後から入力順に詰め，その後ろに .bss を置く。
///       .bss は出力しないが，アドレスは決めなければ再配置できない。
int layout() {
  int i; int p;
  p = prosz;
  i = 0;
  while (i < nobj) {
    obtx[i] = p;
    p = p + shf(i, 1, 20);
    while (p & 3) p = p + 1;
    i = i + 1;
  }
  imgn = p;
  i = 0;
  while (i < nobj) {
    obbs[i] = p;
    p = p + shf(i, 2, 20);
    while (p & 3) p = p + 1;
    i = i + 1;
  }
  return 0;
}

/// @brief 全オブジェクトの定義済み大域シンボルを表に集める。
/// @return 常に 0
int collect() {
  int i; int n; int sy; int st; int ns; int k; int shn; int inf;
  i = 0;
  while (i < nobj) {
    sy = oof[i] + shf(i, 3, 16);        // .symtab の先頭
    ns = shf(i, 3, 20) / 16;            // シンボル数
    st = oof[i] + shf(i, 4, 16);        // .strtab の先頭
    n = shf(i, 3, 28);                  // sh_info = 最初の非ローカル
    while (n < ns) {
      shn = rd2(sy + n * 16 + 14);
      inf = rd1(sy + n * 16 + 12);
      if (shn != 0) {
        k = rd4(sy + n * 16 + 4);       // st_value
        if (shn == 1) gadd(st + rd4(sy + n * 16), obtx[i] + k);
        else gadd(st + rd4(sy + n * 16), obbs[i] + k);
      }
      n = n + 1;
    }
    i = i + 1;
  }
  return 0;
}

/// @brief オブジェクト o のシンボル番号 -> 最終オフセットの対応表を作る。
/// @param o オブジェクト番号
/// @return 常に 0
/// @note ローカル (文字列リテラル) は自オブジェクトの .text 内なので直に計算し，
///       未定義の大域は名前で大域表を引く。残っていればエラー 2。
int mksymad(int o) {
  int n; int sy; int st; int ns; int shn; int g;
  sy = oof[o] + shf(o, 3, 16);
  ns = shf(o, 3, 20) / 16;
  st = oof[o] + shf(o, 4, 16);
  if (ns > 8191) exit(6);
  n = 0;
  while (n < ns) {
    shn = rd2(sy + n * 16 + 14);
    if (shn == 1) symad[n] = obtx[o] + rd4(sy + n * 16 + 4);
    else if (shn == 2) symad[n] = obbs[o] + rd4(sy + n * 16 + 4);
    else {
      symad[n] = 0;
      if (n) {
        g = gfind(st + rd4(sy + n * 16));
        if (g < 0) exit(2);
        symad[n] = gad[g];
      }
    }
    n = n + 1;
  }
  return 0;
}

// ---- 出力と再配置 ----

/// @brief img へ 32 bit をリトルエンディアンで書く。
int iw4(int p, int w) {
  img[p] = w & 255;
  img[p + 1] = (w >> 8) & 255;
  img[p + 2] = (w >> 16) & 255;
  img[p + 3] = (w >> 24) & 255;
  return 0;
}
/// @brief img から 32 bit を読む。
int ir4(int p) {
  return img[p] | (img[p + 1] << 8) | (img[p + 2] << 16) | (img[p + 3] << 24);
}

/// @brief J 形式の即値を命令語のビット位置へ散らす。
int jenc(int rel) {
  return (((rel >> 20) & 1) << 31) | (((rel >> 1) & 1023) << 21)
       | (((rel >> 11) & 1) << 20) | (((rel >> 12) & 255) << 12);
}

/// @brief ランタイム前置部 (crt0 相当) を img の先頭へ置く。
/// @return 常に 0
/// @note occ まではコンパイラが出していたもので，内容は同一である。
///       レジスタ初期化・main 呼出し・終了処理・getc/putc/exit を含む。
///       main への jal は，配置が決まった後に patchmain で埋める。
int prologue() {
  int p;
  p = 0;
  iw4(p, 0x87f004b7); p = p + 4;     // lui x9, 0x87f00   データスタック
  iw4(p, 0x87800137); p = p + 4;     // lui x2, 0x87800   フレームスタック
  iw4(p, 0x100002b7); p = p + 4;     // lui x5, 0x10000   UART
  iw4(p, 0x00100337); p = p + 4;     // lui x6, 0x100     test finisher
  iw4(p, 0x00000000); p = p + 4;     // jal x1, main      (後で埋める)
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00448493); p = p + 4;
  iw4(p, 0x00050c63); p = p + 4;     // rt_exit
  iw4(p, 0x01051513); p = p + 4;
  iw4(p, 0x000035b7); p = p + 4;
  iw4(p, 0x33358593); p = p + 4;
  iw4(p, 0x00b56533); p = p + 4;
  iw4(p, 0x00c0006f); p = p + 4;
  iw4(p, 0x00005537); p = p + 4;
  iw4(p, 0x55550513); p = p + 4;
  iw4(p, 0x00a32023); p = p + 4;
  iw4(p, 0x0000006f); p = p + 4;
  iw4(p, 0x0052c503); p = p + 4;     // rt_getc  (offset 68)
  iw4(p, 0x00157513); p = p + 4;
  iw4(p, 0xfe050ce3); p = p + 4;
  iw4(p, 0x0002c503); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0x0052c583); p = p + 4;     // rt_putc  (offset 96)
  iw4(p, 0x0205f593); p = p + 4;
  iw4(p, 0xfe058ce3); p = p + 4;
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00a28023); p = p + 4;
  iw4(p, 0x0004a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xf99ff06f); p = p + 4;     // exit     (offset 124) rt_exit への跳び
  return 0;
}

/// @brief 前置部の main 呼出しを埋める。
/// @return 常に 0
int patchmain() {
  int g;
  g = gfindlit("main");
  if (g < 0) exit(5);
  iw4(16, jenc(gad[g] - 16) | 0xef);
  return 0;
}

/// @brief 各オブジェクトの .text を img へ複写する。
/// @return 常に 0
int copytext() {
  int i; int n; int sz; int src;
  i = 0;
  while (i < nobj) {
    src = oof[i] + shf(i, 1, 16);
    sz = shf(i, 1, 20);
    n = 0;
    while (n < sz) {
      img[obtx[i] + n] = inp[src + n];
      n = n + 1;
    }
    i = i + 1;
  }
  return 0;
}

/// @brief 再配置を 1 件適用する。
/// @param p 適用位置 (img 内オフセット)
/// @param ty 種別
/// @param s シンボルの最終オフセット
/// @param a 加数
/// @return 常に 0
/// @note S は絶対アドレスなので base を足す。HI20 の +0x800 は，
///       対になる LO12 の即値が符号拡張されることの補正である。
int applyrel(int p, int ty, int s, int a) {
  int v; int w; int rel;
  if (ty == 1) {                     // R_RISCV_32
    iw4(p, base + s + a);
    return 0;
  }
  if (ty == 17) {                    // R_RISCV_JAL
    rel = (s + a) - p;
    if (rel >= 1048576) exit(4);
    if (rel < (0 - 1048576)) exit(4);
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
  exit(1);
  return 0;
}

/// @brief 全オブジェクトの再配置を適用する。
/// @return 常に 0
int relocate() {
  int i; int n; int nr; int rp; int info;
  i = 0;
  while (i < nobj) {
    mksymad(i);
    rp = oof[i] + shf(i, 5, 16);
    nr = shf(i, 5, 20) / 12;
    n = 0;
    while (n < nr) {
      info = rd4(rp + n * 12 + 4);
      applyrel(obtx[i] + rd4(rp + n * 12),
               info & 255,
               symad[(info >> 8) & 16777215],
               rd4(rp + n * 12 + 8));
      n = n + 1;
    }
    i = i + 1;
  }
  return 0;
}

// ---- 駆動部 ----

/// @brief リンカ本体。標準入力からオブジェクト列を読み，実行像を書き出す。
/// @return 常に 0 (異常時は exit で終了コードを返して停止する)
int main() {
  int i;
  base = 0x80000000;
  prosz = 128;
  gcnt = 0;
  readobjs();
  layout();
  // 前置部が提供するシンボルを先に登録する。以降 cc からは
  // 普通の外部関数として見える
  gaddlit("getc", 68);
  gaddlit("putc", 96);
  gaddlit("exit", 124);
  collect();
  prologue();
  copytext();
  patchmain();
  relocate();
  i = 0;
  while (i < imgn) {
    putc(img[i]);
    i = i + 1;
  }
  return 0;
}
