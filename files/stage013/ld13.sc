/// @file ld13.sc
/// @brief 実行像を作るリンカ (Stage 13 世代)。出力形式を 3 つ持つ。
///
/// stage012/ld12.sc を出発点に，'E' 前置部へ **汎用 syscall スタブ
/// sys_ecall** を足したものである (docs/stage013-tools.md 3.1)。
/// 'F' / 'K' の出力は ld12 と 1 バイトも変わらない。再配置とシンボル
/// 解決の仕組みは stage008 から変えていない。
///
/// @section fmt 出力形式は入力の先頭 1 バイトで選ぶ
///   'F'  フラット (従来と同一の前置部。ベアメタル)
///   'K'  カーネル: フラット + カーネル前置部 (mtvec・trap 入口・urun)
///   'E'  ELF 実行形式 (ET_EXEC + プログラムヘッダ 1 本) + ユーザ前置部
///
/// 'E' が提供する syscall スタブは生の名前 (sys_read / sys_write /
/// sys_openat / sys_close / sys_brk) で，戻り値は -errno のままである。
/// POSIX の名前 (read / open / sbrk など) は libc の環境部が errno へ
/// 写して提供する (docs/stage012-os.md 6.3)。加えて汎用の
/// sys_ecall(n, a, b, c) を持ち，以降の syscall の増設は libc がこれを
/// 包むだけで済む (前置部は二度と変えない)。
///
///   { printf 'K'; cat kernel.o; printf '\0'; } | ld13 > kernel.bin
///
/// @section addr アドレスの扱い
/// stage008 と同じく「像の先頭からのオフセット」で計算し，ロードアドレスは
/// 再配置の値を作る瞬間にだけ足す。'E' では像の先頭に ELF ヘッダが載るので，
/// オフセット 0 がそのままロードアドレス (0x8600_0000) に対応する。
///
/// @section priv 特権命令は前置部が持つ
/// カーネルが要る mtvec 設定・レジスタ退避・mret は C では書けないので，
/// 前置部が機械語で持つ (docs/stage012-os.md 5.2)。cc には手を入れない。
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

int base;                 ///< ロードアドレス ('F'/'K' は 0x8000_0000, 'E' は 0x8600_0000)
int prosz;                ///< 前置部の大きさ (バイト。'E' では ELF ヘッダを含む)
int fmt;                  ///< 出力形式 ('F' / 'K' / 'E')
int ebss;                 ///< .bss の終端オフセット (memsz の算出に使う)
int ehsz;                 ///< 'E' の ELF ヘッダ + プログラムヘッダの大きさ

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
  // 先頭 1 バイトが出力形式。続けてオブジェクト列を読む
  fmt = getc();
  if (fmt != 'F' && fmt != 'K' && fmt != 'E') exit(7);
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
  ebss = p;
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

/// @brief 'F' 前置部 (crt0 相当) を img の先頭へ置く。
/// @return 常に 0
/// @note stage008 から一字一句変えていない。レジスタ初期化・main 呼出し・
///       終了処理・getc/putc/exit を含む。main への jal は patchmain が埋める。
int prologuef() {
  int p;
  p = 0;
  iw4(p, 0x87f004b7); p = p + 4;
  iw4(p, 0x87800137); p = p + 4;
  iw4(p, 0x100002b7); p = p + 4;
  iw4(p, 0x00100337); p = p + 4;
  iw4(p, 0x00000000); p = p + 4;
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00448493); p = p + 4;
  iw4(p, 0x00050c63); p = p + 4;
  iw4(p, 0x01051513); p = p + 4;
  iw4(p, 0x000035b7); p = p + 4;
  iw4(p, 0x33358593); p = p + 4;
  iw4(p, 0x00b56533); p = p + 4;
  iw4(p, 0x00c0006f); p = p + 4;
  iw4(p, 0x00005537); p = p + 4;
  iw4(p, 0x55550513); p = p + 4;
  iw4(p, 0x00a32023); p = p + 4;
  iw4(p, 0x0000006f); p = p + 4;
  iw4(p, 0x0052c503); p = p + 4;
  iw4(p, 0x00157513); p = p + 4;
  iw4(p, 0xfe050ce3); p = p + 4;
  iw4(p, 0x0002c503); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0x0052c583); p = p + 4;
  iw4(p, 0x0205f593); p = p + 4;
  iw4(p, 0xfe058ce3); p = p + 4;
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00a28023); p = p + 4;
  iw4(p, 0x0004a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xf99ff06f); p = p + 4;
  return 0;
}

/// @brief 'K' カーネル前置部を img の先頭へ置く。
/// @return 常に 0
/// @note 'F' との違いは 3 つ。(1) 2 本のスタックをカーネル用のアドレスへ置く，
///       (2) mscratch にトラップフレーム (0x8370_0000)，mtvec に trap 入口を
///       設定する，(3) trap 入口と urun (U モードへの mret) を持つ。
///       getc/putc/exit の実体は 'F' と同一のものを引き写している。
///       trap 入口は全レジスタをフレームへ退避し，カーネルのスタックへ
///       移してから ktrap を呼び，そのまま urun へ落ちる。
int prologuek() {
  int p; int v;
  p = 0;
  iw4(p, 0x83f004b7); p = p + 4;
  iw4(p, 0x83800137); p = p + 4;
  iw4(p, 0x100002b7); p = p + 4;
  iw4(p, 0x00100337); p = p + 4;
  iw4(p, 0xfff00f13); p = p + 4;
  iw4(p, 0x3b0f1073); p = p + 4;
  iw4(p, 0x01f00f13); p = p + 4;
  iw4(p, 0x3a0f1073); p = p + 4;
  iw4(p, 0x83700fb7); p = p + 4;
  iw4(p, 0x000f8f93); p = p + 4;
  iw4(p, 0x340f9073); p = p + 4;
  iw4(p, 0x80000f37); p = p + 4;
  iw4(p, 0x0a8f0f13); p = p + 4;
  iw4(p, 0x305f1073); p = p + 4;
  iw4(p, 0x00000000); p = p + 4;
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00448493); p = p + 4;
  iw4(p, 0x00050c63); p = p + 4;
  iw4(p, 0x01051513); p = p + 4;
  iw4(p, 0x000035b7); p = p + 4;
  iw4(p, 0x33358593); p = p + 4;
  iw4(p, 0x00b56533); p = p + 4;
  iw4(p, 0x00c0006f); p = p + 4;
  iw4(p, 0x00005537); p = p + 4;
  iw4(p, 0x55550513); p = p + 4;
  iw4(p, 0x00a32023); p = p + 4;
  iw4(p, 0x0000006f); p = p + 4;
  iw4(p, 0x0052c503); p = p + 4;
  iw4(p, 0x00157513); p = p + 4;
  iw4(p, 0xfe050ce3); p = p + 4;
  iw4(p, 0x0002c503); p = p + 4;
  iw4(p, 0xffc48493); p = p + 4;
  iw4(p, 0x00a4a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0x0052c583); p = p + 4;
  iw4(p, 0x0205f593); p = p + 4;
  iw4(p, 0xfe058ce3); p = p + 4;
  iw4(p, 0x0004a503); p = p + 4;
  iw4(p, 0x00a28023); p = p + 4;
  iw4(p, 0x0004a023); p = p + 4;
  iw4(p, 0x00008067); p = p + 4;
  iw4(p, 0xf99ff06f); p = p + 4;
  iw4(p, 0x340f9ff3); p = p + 4;
  iw4(p, 0x001fa023); p = p + 4;
  iw4(p, 0x002fa223); p = p + 4;
  iw4(p, 0x003fa423); p = p + 4;
  iw4(p, 0x004fa623); p = p + 4;
  iw4(p, 0x005fa823); p = p + 4;
  iw4(p, 0x006faa23); p = p + 4;
  iw4(p, 0x007fac23); p = p + 4;
  iw4(p, 0x008fae23); p = p + 4;
  iw4(p, 0x029fa023); p = p + 4;
  iw4(p, 0x02afa223); p = p + 4;
  iw4(p, 0x02bfa423); p = p + 4;
  iw4(p, 0x02cfa623); p = p + 4;
  iw4(p, 0x02dfa823); p = p + 4;
  iw4(p, 0x02efaa23); p = p + 4;
  iw4(p, 0x02ffac23); p = p + 4;
  iw4(p, 0x030fae23); p = p + 4;
  iw4(p, 0x051fa023); p = p + 4;
  iw4(p, 0x052fa223); p = p + 4;
  iw4(p, 0x053fa423); p = p + 4;
  iw4(p, 0x054fa623); p = p + 4;
  iw4(p, 0x055fa823); p = p + 4;
  iw4(p, 0x056faa23); p = p + 4;
  iw4(p, 0x057fac23); p = p + 4;
  iw4(p, 0x058fae23); p = p + 4;
  iw4(p, 0x079fa023); p = p + 4;
  iw4(p, 0x07afa223); p = p + 4;
  iw4(p, 0x07bfa423); p = p + 4;
  iw4(p, 0x07cfa623); p = p + 4;
  iw4(p, 0x07dfa823); p = p + 4;
  iw4(p, 0x07efaa23); p = p + 4;
  iw4(p, 0x340f9f73); p = p + 4;
  iw4(p, 0x07efac23); p = p + 4;
  iw4(p, 0x34102f73); p = p + 4;
  iw4(p, 0x07efae23); p = p + 4;
  iw4(p, 0x34202f73); p = p + 4;
  iw4(p, 0x09efa023); p = p + 4;
  iw4(p, 0x83f004b7); p = p + 4;
  iw4(p, 0x83800137); p = p + 4;
  iw4(p, 0x100002b7); p = p + 4;
  iw4(p, 0x00100337); p = p + 4;
  iw4(p, 0x00000000); p = p + 4;
  iw4(p, 0x83700fb7); p = p + 4;
  iw4(p, 0x000f8f93); p = p + 4;
  iw4(p, 0x07cfaf03); p = p + 4;
  iw4(p, 0x341f1073); p = p + 4;
  iw4(p, 0x00002f37); p = p + 4;
  iw4(p, 0x800f0f13); p = p + 4;
  iw4(p, 0x300f3073); p = p + 4;
  iw4(p, 0x000fa083); p = p + 4;
  iw4(p, 0x004fa103); p = p + 4;
  iw4(p, 0x008fa183); p = p + 4;
  iw4(p, 0x00cfa203); p = p + 4;
  iw4(p, 0x010fa283); p = p + 4;
  iw4(p, 0x014fa303); p = p + 4;
  iw4(p, 0x018fa383); p = p + 4;
  iw4(p, 0x01cfa403); p = p + 4;
  iw4(p, 0x020fa483); p = p + 4;
  iw4(p, 0x024fa503); p = p + 4;
  iw4(p, 0x028fa583); p = p + 4;
  iw4(p, 0x02cfa603); p = p + 4;
  iw4(p, 0x030fa683); p = p + 4;
  iw4(p, 0x034fa703); p = p + 4;
  iw4(p, 0x038fa783); p = p + 4;
  iw4(p, 0x03cfa803); p = p + 4;
  iw4(p, 0x040fa883); p = p + 4;
  iw4(p, 0x044fa903); p = p + 4;
  iw4(p, 0x048fa983); p = p + 4;
  iw4(p, 0x04cfaa03); p = p + 4;
  iw4(p, 0x050faa83); p = p + 4;
  iw4(p, 0x054fab03); p = p + 4;
  iw4(p, 0x058fab83); p = p + 4;
  iw4(p, 0x05cfac03); p = p + 4;
  iw4(p, 0x060fac83); p = p + 4;
  iw4(p, 0x064fad03); p = p + 4;
  iw4(p, 0x068fad83); p = p + 4;
  iw4(p, 0x06cfae03); p = p + 4;
  iw4(p, 0x070fae83); p = p + 4;
  iw4(p, 0x074faf03); p = p + 4;
  iw4(p, 0x078faf83); p = p + 4;
  iw4(p, 0x30200073); p = p + 4;
  // mtvec に trap 入口の絶対アドレスを埋める (lui + addi の 2 語。語 11, 12)
  v = base + 168;
  iw4(44, (((v + 2048) >> 12) << 12) | (30 << 7) | 55);
  iw4(48, ((v & 4095) << 20) | (30 << 15) | (30 << 7) | 19);
  return 0;
}

/// @brief 'E' ユーザ前置部を img へ置く (ELF ヘッダの直後から)。
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

/// @brief 前置部の main 呼出しを埋める。
/// @return 常に 0
int patchmain() {
  int g; int at;
  g = gfindlit("main");
  if (g < 0) exit(5);
  if (fmt == 'F') at = 16;
  else if (fmt == 'K') at = 56;
  else at = ehsz + 32;
  iw4(at, jenc(gad[g] - at) | 0xef);
  // 'K' は trap 入口から呼ぶ ktrap も埋める (カーネルが定義する)
  if (fmt == 'K') {
    g = gfindlit("ktrap");
    if (g < 0) exit(5);
    at = 332;
    iw4(at, jenc(gad[g] - at) | 0xef);
  }
  return 0;
}

/// @brief 'E' のデータスタック上端を前置部へ埋める。
/// @return 常に 0
/// @note .bss の後ろに 256 KiB を予約し，その末尾を x9 の初期値とする。
int patchdstk() {
  int v;
  v = base + ebss + 262144;
  iw4(ehsz, ((v + 2048) >> 12 << 12) | (9 << 7) | 55);
  iw4(ehsz + 4, ((v & 4095) << 20) | (9 << 15) | (9 << 7) | 19);
  return 0;
}

/// @brief img へ 16 bit をリトルエンディアンで書く。
int iw2(int p, int w) {
  img[p] = w & 255;
  img[p + 1] = (w >> 8) & 255;
  return 0;
}

/// @brief ELF 実行形式のヘッダとプログラムヘッダを img の先頭へ置く。
/// @return 常に 0
/// @note セクションヘッダは付けない (実行に不要)。PT_LOAD 1 本で表し，
///       .bss は p_memsz > p_filesz として表現する (カーネルが 0 で埋める)。
///       データスタックのぶんも memsz に含める。
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
  iw4(72, ebss + 262144);
  iw4(76, 7);
  iw4(80, 4096);
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
  gcnt = 0;
  ehsz = 84;
  readobjs();
  // 形式ごとにロードアドレスと前置部の大きさを決めてから配置する
  if (fmt == 'E') { base = 0x86000000; prosz = ehsz + 504; }
  else if (fmt == 'K') { base = 0x80000000; prosz = 492; }
  else { base = 0x80000000; prosz = 128; }
  layout();
  // 前置部が提供するシンボルを先に登録する。以降 cc からは
  // 普通の外部関数として見える
  if (fmt == 'E') {
    gaddlit("getc", ehsz + 324);
    gaddlit("putc", ehsz + 396);
    gaddlit("exit", ehsz + 36);
    gaddlit("sys_read", ehsz + 56);
    gaddlit("sys_write", ehsz + 112);
    gaddlit("sys_openat", ehsz + 168);
    gaddlit("sys_close", ehsz + 228);
    gaddlit("sys_brk", ehsz + 276);
    gaddlit("sys_ecall", ehsz + 448);
  } else if (fmt == 'K') {
    gaddlit("getc", 108);
    gaddlit("putc", 136);
    gaddlit("exit", 164);
    gaddlit("urun", 336);
  } else {
    gaddlit("getc", 68);
    gaddlit("putc", 96);
    gaddlit("exit", 124);
  }
  collect();
  if (fmt == 'E') prologuee();
  else if (fmt == 'K') prologuek();
  else prologuef();
  copytext();
  patchmain();
  if (fmt == 'E') { patchdstk(); elfhdr(); }
  relocate();
  i = 0;
  while (i < imgn) {
    putc(img[i]);
    i = i + 1;
  }
  return 0;
}
