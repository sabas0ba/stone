/// @file pp14.sc
/// @brief C プリプロセッサ (Stage 14 世代)。テキストを受け取り，前処理済みテキストを返す。
///
/// stage009/pp.sc を出発点に，**容量だけを広げた** (docs/stage014-external.md
/// 第 9 部)。外部ソース (zlib) は入力 256 KiB・識別子 31 バイトの器に
/// 収まらない (crc32.h の表 743 KiB・40 文字のマクロ名)。
///   - 入力・展開アリーナ: 256 KiB -> 1 MiB
///   - マクロ本体: 128 KiB -> 256 KiB / マクロ数: 512 -> 1024
///   - 識別子・マクロ名・メンバ名: 31 バイト -> 63 バイト (スロット 64 バイト)
/// 仕組み (フレームの積み・展開・指令) は pp と同一である。
///
/// 仕様は docs/stage009-pp.md，実装言語 sc の仕様は docs/stage005-sc.md 2 章。
/// cc.bin + ld.bin でビルドする (ld14 と同じ道具立て)。
///
/// @section model 中核となる考え方: 入力フレームの積み
/// pp の処理はすべて「文字を 1 個引き出す」操作の上に載っている。引き出し元は
/// 単一のソースではなく，フレームの積みである。底が翻訳単位で，#include は
/// ファイルのフレームを，マクロ展開は置換結果のフレームを積む。フレームが
/// 尽きれば自動的に一段下へ戻る。
///
///   [展開: BAR の置換結果]   <- 今ここから読んでいる
///   [展開: FOO の置換結果]      (FOO の中から BAR が現れた)
///   [ファイル: util.h    ]
///   [ファイル: main.c    ]   <- 底 (翻訳単位)
///
/// この形にすると，走査の本体 scan1() は「1 個の字句を処理する」だけの関数に
/// なる。置換結果の再走査も，入れ子の展開も，include からの復帰も，すべて
/// 「フレームを積む・降ろす」に還元される。マクロの再帰抑止も，積みの中に
/// 自分の印 (fmac) があるかを見るだけで済む。
///
/// @section arena 2 本のアリーナ
/// 置換結果のテキストを置く領域が要る。フレームの寿命は後入れ先出しなので，
/// スタック割付けで足りる (積むときに割付け位置を控え，降ろすときに戻す)。
/// アリーナを 2 本に分けているのは，実引数の事前展開の最中に
///   - 展開の出力先          (eb)
///   - 展開のために積むフレームのテキスト (xb)
/// が同時に伸びるためである。1 本だと互いの領域を跨いでしまう。
///
/// @section limits sc 言語の制約に由来する書き方
/// sc には for / do / switch / break / continue / ++ / += / sizeof / キャスト /
/// 初期化子 / 定数構文 / 列挙型が無い。したがって
///   - ループの途中脱出は完了フラグ (done) で表す
///   - 種別定数は大域変数として init() で設定する
///   - 表はすべて添字を共有する並行配列で表現する
///   - 再入する関数が使う作業領域はローカル配列に置く
///     (subst は実引数の事前展開を通じて自分自身に戻ってくる)

// ---- 入力と 2 本のアリーナ ----

char src[1048576];         ///< 標準入力の全体 (EOT 0x04 まで)
int srcn;                 ///< src の有効長

char xb[1048576];          ///< アリーナ 1: フレームが参照するテキスト
int xp;                   ///< xb の割付け位置 (スタック割付け)

char eb[65536];           ///< アリーナ 2: 事前展開・#if 式の展開結果
int epos;                 ///< eb の割付け位置 (同上)
int edep;                 ///< > 0 なら emit の行き先は eb

// ---- 束ね (bundle) のメンバ表 ----
// 最後のメンバが翻訳単位で，それ以外は #include の探索対象。

char mbname[4096];        ///< メンバ名 (32 バイトスロット x 64)
int mbof[64];             ///< src 内の開始位置
int mbln[64];             ///< 長さ
int mbcnt;                ///< メンバ数

// ---- マクロ表 (並行配列) ----
// 一度作ったスロットは #undef されても消さない (mcon を 0 にするだけ)。
// 同じ名前の再定義でスロットを使い回すため。

char mcname[65536];       ///< マクロ名 (32 バイトスロット x 512)
int mcfun[1024];           ///< 1 = 関数形式
int mcnp[1024];            ///< 仮引数の個数 (可変長なら __VA_ARGS__ を含む)
int mcvar[1024];           ///< 1 = 可変長 (最後の仮引数が __VA_ARGS__)
int mcbo[1024];            ///< 本体の位置 (mbody 内)
int mcbl[1024];            ///< 本体の長さ
int mcpo[1024];            ///< 仮引数名の位置 (mprm 内。64 バイトスロットが mcnp 個)
int mcon[1024];            ///< 1 = 現在定義されている
int mccnt;                ///< スロット数

char mbody[262144];       ///< マクロ本体の実体
int mbp;                  ///< mbody の有効長
char mprm[32768];         ///< 仮引数名の実体 (64 バイトスロット)
int mpp;                  ///< mprm の有効長

// ---- 入力フレームの積み ----

int fkind[64];            ///< 0 = ファイル (src を指す), 1 = 展開 (xb を指す)
int fpos[64];             ///< 次に読む位置
int fend[64];             ///< 終端位置
int fmac[64];             ///< 展開元のマクロ番号 (再帰抑止の印)。-1 = 無し
int fmark[64];            ///< 降ろすときに戻す xp の値 (展開フレームのみ)
int ffidx[64];            ///< ファイルフレームのメンバ番号 (__FILE__ / __LINE__ 用)
int fstop[64];            ///< 1 = 番兵。尽きたら -1 を返して読取りを止める
int fpb[64];              ///< 積む直前の押し戻しの床 (pbbase の退避先)
int ftop;                 ///< 積みの頂点。-1 = 空

// 押し戻しはフレームごとに区切る。識別子を読むときの 1 文字先読みは
// 「マクロ名の次の文字」であり，展開結果よりも後ろに位置する。フレームを
// 積んだ後もそれが先に読めてしまうと，展開結果と前後が入れ替わる
// (`return A;` が `return ;1` になる)。そこで積むときに床を上げ，
// 降ろすときに戻す。床より下の押し戻しは，そのフレームからは見えない。
int pbuf[16];             ///< 押し戻した文字
int pbff[16];             ///< 同上: その文字がファイル由来だったか
int pbn;                  ///< 押し戻しの個数
int pbbase;               ///< 現在のフレームから見える押し戻しの床

int fromfile;             ///< 直前に取り出した文字はファイル由来か
int atbol;                ///< 次の文字は行頭か (空白は行頭性を保つ)
int wasbol;               ///< 直前に取り出した文字は行頭にあったか

// ---- 条件コンパイル (#if の入れ子) ----

int cact[64];             ///< 1 = この段の現在の枝を採用している
int cany[64];             ///< 1 = この段で既に真の枝を通った
int cels[64];             ///< 1 = #else を通過済み
int cpen[64];             ///< 1 = この段に入った時点で外側がすべて真だった
int cdn;                  ///< 入れ子の深さ
int nfalse;               ///< 採用していない段の数。> 0 なら出力を抑止する

// ---- 指令行の作業域 ----

char dbuf[8192];          ///< 指令行 (# の次から行末まで。コメントは空白へ)
int dn;                   ///< dbuf の有効長
int dp;                   ///< dbuf の解析位置

char idn[64];             ///< 走査中に読んだ識別子
int idl;                  ///< その長さ
char nbuf[64];            ///< 名前の作業域 (指令の解析用)
char wbuf[64];            ///< 指令名

// ---- #if 式の評価器 ----

int evp;                  ///< eb 内の読取り位置
int eve;                  ///< eb 内の終端
int etok;                 ///< 現在の (まだ消費していない) トークン種別
int enum_;                ///< そのトークンが数のときの値

int eot;
int te_end; int te_num; int te_lp; int te_rp;
int te_add; int te_sub; int te_mul; int te_div; int te_mod;
int te_shl; int te_shr; int te_lt; int te_gt; int te_le; int te_ge;
int te_eq; int te_ne; int te_and; int te_xor; int te_or;
int te_aa; int te_oo; int te_not; int te_tld; int te_que; int te_col;

/// @brief 種別定数を設定する (sc に定数構文が無いため)。
int init() {
  eot = 4;
  te_end = 0; te_num = 1; te_lp = 2; te_rp = 3;
  te_add = 4; te_sub = 5; te_mul = 6; te_div = 7; te_mod = 8;
  te_shl = 9; te_shr = 10; te_lt = 11; te_gt = 12; te_le = 13; te_ge = 14;
  te_eq = 15; te_ne = 16; te_and = 17; te_xor = 18; te_or = 19;
  te_aa = 20; te_oo = 21; te_not = 22; te_tld = 23; te_que = 24; te_col = 25;
  return 0;
}

// ---- 文字種と文字列 ----

/// @brief 空白か (SP TAB CR LF)。
int isws(int c) { return c == 32 || c == 9 || c == 13 || c == 10; }
/// @brief 10 進数字か。
int isdig(int c) { return c >= '0' && c <= '9'; }
/// @brief 識別子の先頭に置ける文字か。
/// @note sc の識別子は英小文字のみだが，マクロ名は大文字が慣習であり，
///       取り込む外部ヘッダも大文字を使う。pp は入力言語より広く受ける。
int isidh(int c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  return c == '_';
}
/// @brief 識別子の 2 文字目以降に置ける文字か。
int isidc(int c) { return isidh(c) || isdig(c); }

/// @brief NUL 終端文字列の同値判定。
int streq(char *a, char *b) {
  int i;
  i = 0;
  while (a[i] == b[i]) {
    if (a[i] == 0) return 1;
    i = i + 1;
  }
  return 0;
}

/// @brief 名前スロット (32 バイト) へ NUL 終端で複写する。
/// @return 常に 0。31 バイトを超える名前は容量超過 (6)
int nput(char *d, char *s) {
  int i;
  i = 0;
  while (s[i]) {
    if (i >= 63) exit(6);
    d[i] = s[i];
    i = i + 1;
  }
  d[i] = 0;
  return 0;
}

// ---- 入出力 ----

/// @brief 標準入力を EOT まで src へ読み込む。
int rdall() {
  int c;
  srcn = 0;
  c = getc();
  while (c != eot) {
    if (srcn >= 1048576) exit(6);
    src[srcn] = c;
    srcn = srcn + 1;
    c = getc();
  }
  return 0;
}

/// @brief 前処理結果の 1 文字を出力する。
/// @details 行き先は 3 通り (docs/stage009-pp.md 4.3)。
///          事前展開中は eb，抑止区間では捨て，それ以外は標準出力。
int emit(int c) {
  if (edep > 0) {
    if (epos >= 65536) exit(6);
    eb[epos] = c;
    epos = epos + 1;
    return 0;
  }
  if (nfalse > 0) return 0;
  putc(c);
  return 0;
}

/// @brief アリーナ xb へ 1 文字積む。
int xput(int c) {
  if (xp >= 1048576) exit(6);
  xb[xp] = c;
  xp = xp + 1;
  return 0;
}

// ---- 束ねの解析 ----

/// @brief 入力が束ねのマジックで始まるか調べる。
/// @return マジックの長さ (15)。束ねでなければ 0
int hasmagic() {
  char *m;
  int i;
  m = "#!stone-bundle\n";
  i = 0;
  while (m[i]) {
    if (i >= srcn) return 0;
    if (src[i] != m[i]) return 0;
    i = i + 1;
  }
  return i;
}

/// @brief 束ねをメンバ表へ分解する。
/// @details マジックが無ければ入力全体を名前 `-` の 1 メンバとみなす。
///          この場合 #include は必ず失敗する (エラー 2)。
int bundle() {
  int p;
  int k;
  int sz;
  p = hasmagic();
  if (p == 0) {
    nput(mbname, "-");
    mbof[0] = 0;
    mbln[0] = srcn;
    mbcnt = 1;
    return 0;
  }
  mbcnt = 0;
  while (p < srcn) {
    if (src[p] != '@') exit(1);
    p = p + 1;
    if (mbcnt >= 64) exit(6);
    k = 0;
    while (p < srcn && src[p] != 32) {
      if (k >= 63) exit(6);
      mbname[mbcnt * 64 + k] = src[p];
      k = k + 1;
      p = p + 1;
    }
    mbname[mbcnt * 64 + k] = 0;
    if (k == 0 || p >= srcn) exit(1);
    p = p + 1;
    if (!isdig(src[p])) exit(1);
    sz = 0;
    while (p < srcn && isdig(src[p])) {
      sz = sz * 10 + src[p] - '0';
      p = p + 1;
    }
    if (p >= srcn || src[p] != 10) exit(1);
    p = p + 1;
    if (p + sz > srcn) exit(1);
    mbof[mbcnt] = p;
    mbln[mbcnt] = sz;
    mbcnt = mbcnt + 1;
    p = p + sz;
  }
  if (mbcnt == 0) exit(1);
  return 0;
}

// ---- 入力フレームの操作 ----

/// @brief フレームを 1 段降ろす。展開フレームならアリーナも巻き戻す。
int popf() {
  if (fkind[ftop] == 1) xp = fmark[ftop];
  if (pbn > pbbase) pbn = pbbase;
  pbbase = fpb[ftop];
  ftop = ftop - 1;
  return 0;
}

/// @brief ファイル (束ねのメンバ) のフレームを積む。
int pushfile(int idx) {
  if (ftop >= 63) exit(6);
  ftop = ftop + 1;
  fkind[ftop] = 0;
  fpos[ftop] = mbof[idx];
  fend[ftop] = mbof[idx] + mbln[idx];
  fmac[ftop] = 0 - 1;
  fmark[ftop] = xp;
  ffidx[ftop] = idx;
  fstop[ftop] = 0;
  fpb[ftop] = pbbase;
  pbbase = pbn;
  return 0;
}

/// @brief 展開テキストのフレームを積む。
/// @param off  xb 内の開始位置
/// @param len  長さ
/// @param m    再帰抑止の印にするマクロ番号 (-1 = 印なし)
/// @param mark 降ろすときに戻す xp の値
/// @param stop 1 = 番兵。尽きたら -1 を返して読取りを止める
int pushx(int off, int len, int m, int mark, int stop) {
  if (ftop >= 63) exit(6);
  ftop = ftop + 1;
  fkind[ftop] = 1;
  fpos[ftop] = off;
  fend[ftop] = off + len;
  fmac[ftop] = m;
  fmark[ftop] = mark;
  ffidx[ftop] = 0 - 1;
  fstop[ftop] = stop;
  fpb[ftop] = pbbase;
  pbbase = pbn;
  return 0;
}

/// @brief 行頭性を更新する。
/// @details 空白と水平タブは行頭性を保つ (`  # define` を指令として認めるため)。
///          wasbol には「この文字自身が行頭にあったか」を残す。
int setbol(int c) {
  wasbol = atbol;
  if (c == 10) {
    if (fromfile) atbol = 1;
    return c;
  }
  if (c != 32 && c != 9 && c != 13) atbol = 0;
  return c;
}

/// @brief フレームの積みから 1 文字取り出す。
/// @return 文字，または -1 (入力終端・番兵フレームの終端)
/// @note ファイルフレームが尽きたときは改行を 1 個返す。include 元の行が
///       途中で終わっていても，戻った先が行頭になることを保証するため。
int rawc() {
  int c;
  int done;
  done = 0;
  while (done == 0) {
    // フレームを降ろすと一段下の押し戻しが再び見えるようになる。
    // 降ろした後は必ずここへ戻って押し戻しを先に調べる
    if (pbn > pbbase) {
      pbn = pbn - 1;
      c = pbuf[pbn];
      fromfile = pbff[pbn];
      if (c < 0) return c;
      return setbol(c);
    }
    if (ftop < 0) {
      fromfile = 0;
      return 0 - 1;
    }
    if (fpos[ftop] < fend[ftop]) {
      if (fkind[ftop] == 0) {
        c = src[fpos[ftop]];
        fromfile = 1;
      } else {
        c = xb[fpos[ftop]];
        fromfile = 0;
      }
      fpos[ftop] = fpos[ftop] + 1;
      return setbol(c);
    }
    // 番兵は自分では降りない。expandto が読み終えた後に降ろす。
    // ここで降ろしてしまうと，直後の ungc(-1) が下の段へ積まれてしまう
    if (fstop[ftop]) {
      fromfile = 0;
      return 0 - 1;
    }
    if (fkind[ftop] == 0) {
      popf();
      fromfile = 1;
      return setbol(10);
    }
    popf();
  }
  return 0 - 1;
}

/// @brief 文字を 1 個押し戻す。
int ungc(int c) {
  if (pbn >= 16) exit(6);
  pbuf[pbn] = c;
  pbff[pbn] = fromfile;
  pbn = pbn + 1;
  return 0;
}

/// @brief 行連結 (`\` + 改行) を取り除きながら 1 文字取り出す。
/// @note 文字列やコメントの内側でも働く (translation phase 2 に相当)。
int gc() {
  int c;
  int d;
  c = rawc();
  while (c == 92) {
    d = rawc();
    if (d != 10) {
      ungc(d);
      return c;
    }
    c = rawc();
  }
  return c;
}

/// @brief コメントを空白 1 個に置き換えながら 1 文字取り出す。
/// @return 文字，または -1
/// @note 文字列・文字リテラルの内側では呼んではならない (そこは gc を使う)。
///       ブロックコメントを飲んだときは行頭性を巻き戻す。コメントは空白
///       1 個と等価であり，`/* */ #define` の # は行頭にあるとみなすため。
int gcnc() {
  int c;
  int d;
  int b0;
  b0 = atbol;
  c = gc();
  if (c != '/') return c;
  d = gc();
  if (d == '/') {
    c = gc();
    while (c >= 0 && c != 10) c = gc();
    return c;
  }
  if (d == '*') {
    c = gc();
    while (c >= 0) {
      if (c == '*') {
        d = gc();
        if (d == '/') {
          atbol = b0;
          wasbol = b0;
          return 32;
        }
        c = d;
      } else {
        c = gc();
      }
    }
    exit(1);
  }
  ungc(d);
  wasbol = b0;
  atbol = 0;
  return c;
}

// ---- マクロ表 ----

/// @brief 定義されているマクロを名前で探す。
/// @return スロット番号。無ければ -1
int mfind(char *nm) {
  int i;
  i = 0;
  while (i < mccnt) {
    if (mcon[i] && streq(mcname + i * 64, nm)) return i;
    i = i + 1;
  }
  return 0 - 1;
}

/// @brief 名前に対応するスロットを得る (無ければ作る)。
/// @note #undef されたスロットも名前で見つかる。再定義で使い回すため。
int mslot(char *nm) {
  int i;
  i = 0;
  while (i < mccnt) {
    if (streq(mcname + i * 64, nm)) return i;
    i = i + 1;
  }
  if (mccnt >= 1024) exit(6);
  i = mccnt;
  mccnt = mccnt + 1;
  nput(mcname + i * 64, nm);
  mcon[i] = 0;
  return i;
}

/// @brief マクロ m が現在展開の途中か (再帰抑止の判定)。
int active(int m) {
  int i;
  i = ftop;
  while (i >= 0) {
    if (fmac[i] == m) return 1;
    i = i - 1;
  }
  return 0;
}

/// @brief マクロ m の仮引数のうち名前が nm のものを探す。
/// @return 仮引数の番号。無ければ -1
int pfind(int m, char *nm) {
  int i;
  i = 0;
  while (i < mcnp[m]) {
    if (streq(mprm + mcpo[m] + i * 64, nm)) return i;
    i = i + 1;
  }
  return 0 - 1;
}

// ---- __FILE__ / __LINE__ ----
// 表を引かずに直接処理する。__LINE__ は使われた時点でメンバ先頭からの
// 改行を数える。頻度が低いので走査で足り，行番号の逐次管理が要らない。

/// @brief 現在読んでいるファイルフレームを探す。
/// @return フレーム番号。無ければ -1
int curfil() {
  int i;
  i = ftop;
  while (i >= 0) {
    if (fkind[i] == 0) return i;
    i = i - 1;
  }
  return 0 - 1;
}

/// @brief __FILE__ を「メンバ名の文字列リテラル」として出力する。
int emitfile() {
  int k;
  int i;
  char *s;
  k = curfil();
  emit(34);
  if (k >= 0) {
    s = mbname + ffidx[k] * 64;
    i = 0;
    while (s[i]) {
      emit(s[i]);
      i = i + 1;
    }
  }
  emit(34);
  return 0;
}

/// @brief __LINE__ を 10 進で出力する。
int emitline() {
  int k;
  int i;
  int n;
  int d;
  char t[16];
  k = curfil();
  n = 1;
  if (k >= 0) {
    i = mbof[ffidx[k]];
    while (i < fpos[k]) {
      if (src[i] == 10) n = n + 1;
      i = i + 1;
    }
  }
  d = 0;
  while (n > 0) {
    t[d] = '0' + n % 10;
    d = d + 1;
    n = n / 10;
  }
  if (d == 0) emit('0');
  while (d > 0) {
    d = d - 1;
    emit(t[d]);
  }
  return 0;
}

// ---- 走査 (前方宣言のかわりに定義順は自由。sc は前方参照できる) ----

/// @brief 直前に読んだ識別子 idn を出力する。
int emitid() {
  int i;
  i = 0;
  while (i < idl) {
    emit(idn[i]);
    i = i + 1;
  }
  return 0;
}

/// @brief 識別子を idn へ読み取る (先頭の 1 文字は読み終えている)。
int readid(int c) {
  idl = 0;
  while (isidc(c)) {
    if (idl >= 63) exit(6);
    idn[idl] = c;
    idl = idl + 1;
    c = gc();
  }
  ungc(c);
  idn[idl] = 0;
  return 0;
}

/// @brief pp-number を逐語で出力する (先頭の数字は読み終えている)。
/// @note 途中の `e+` `e-` も 1 個の字句に含める。これを分けると
///       `1e+X` の X がマクロとして展開されてしまう。
int emitnum(int c) {
  int isexp;
  emit(c);
  c = gc();
  while (isidc(c) || c == '.') {
    emit(c);
    isexp = 0;
    if (c == 'e' || c == 'E' || c == 'p' || c == 'P') isexp = 1;
    c = gc();
    if (isexp && (c == '+' || c == '-')) {
      emit(c);
      c = gc();
    }
  }
  ungc(c);
  return 0;
}

/// @brief 文字列・文字リテラルを読み取る。
/// @param q 引用符 (34 または 39)。開き引用符は読み終えている
/// @param e 1 = 逐語で出力する, 0 = 捨てる
/// @note 抑止区間では未終端のリテラルがありうる (`don't` を含むコメント風の
///       記述など)。その場合は改行で打ち切って押し戻す。
int dolit(int q, int e) {
  int c;
  if (e) emit(q);
  c = gc();
  while (c >= 0 && c != q && c != 10) {
    if (e) emit(c);
    if (c == 92) {
      c = gc();
      if (c < 0) exit(1);
      if (e) emit(c);
    }
    c = gc();
  }
  if (c == q) {
    if (e) emit(c);
    return 0;
  }
  if (c == 10) {
    if (e) exit(1);
    ungc(c);
    return 0;
  }
  exit(1);
  return 0;
}

// ---- 実引数の収集と置換 ----

/// @brief xb 内の [st, xp) の末尾の空白を落とす。
/// @return 残った長さ
int trimx(int st) {
  while (xp > st && xb[xp - 1] == 32) xp = xp - 1;
  return xp - st;
}

/// @brief 関数形式マクロの実引数を集める (開き括弧は読み終えている)。
/// @param ofs 出力: 各実引数の xb 内の開始位置
/// @param lns 出力: 各実引数の長さ
/// @param lim 可変長マクロの分割上限。lim 個を集めた後のカンマは分割しない。
///            -1 = 上限なし
/// @return 実引数の個数
/// @details 実引数は展開せずに生テキストのまま蓄える。事前展開は subst が
///          必要になった時点で行う (# や ## の作用対象は生のまま使うため)。
///          空白の連なりは空白 1 個へ畳む (# の文字列化の規定に合わせる)。
int collect(int *ofs, int *lns, int lim) {
  int n;
  int d;
  int c;
  int st;
  int q;
  int done;
  n = 0;
  d = 1;
  done = 0;
  c = gcnc();
  while (isws(c)) c = gcnc();
  if (c == ')') return 0;
  st = xp;
  while (done == 0) {
    if (c < 0) exit(3);
    if (c == '(') {
      d = d + 1;
      xput(c);
    } else if (c == ')') {
      d = d - 1;
      if (d == 0) {
        if (n >= 31) exit(6);
        ofs[n] = st;
        lns[n] = trimx(st);
        n = n + 1;
        done = 1;
      } else {
        xput(c);
      }
    } else if (c == ',' && d == 1 && (lim < 0 || n < lim)) {
      if (n >= 31) exit(6);
      ofs[n] = st;
      lns[n] = trimx(st);
      n = n + 1;
      st = xp;
    } else if (c == 34 || c == 39) {
      q = c;
      xput(c);
      c = gc();
      while (c >= 0 && c != q) {
        xput(c);
        if (c == 92) {
          c = gc();
          if (c < 0) exit(3);
          xput(c);
        }
        c = gc();
      }
      if (c < 0) exit(3);
      xput(c);
    } else if (isws(c)) {
      if (xp > st && xb[xp - 1] != 32) xput(32);
    } else {
      xput(c);
    }
    if (done == 0) c = gcnc();
  }
  return n;
}

/// @brief xb 内の [off, off+len) を文字列リテラルにして xb の末尾へ積む (`#`)。
int strize(int off, int len) {
  int i;
  int c;
  xput(34);
  i = 0;
  while (i < len) {
    c = xb[off + i];
    if (c == 34 || c == 92) xput(92);
    xput(c);
    i = i + 1;
  }
  xput(34);
  return 0;
}

/// @brief テキストを展開しきって eb へ積む (実引数の事前展開，#if の式)。
/// @param off xb 内の開始位置
/// @param len 長さ
/// @return eb 内の開始位置 (長さは呼び手が epos との差で求める)
/// @details 番兵フレームを積み，それが尽きるまで通常の走査を回す。
///          番兵があるので，展開の途中で下のフレームまで読み進むことはない。
int expandto(int off, int len) {
  int st;
  int mark;
  st = epos;
  mark = xp;
  edep = edep + 1;
  pushx(off, len, 0 - 1, mark, 1);
  while (scan1()) { }
  popf();
  edep = edep - 1;
  return st;
}

/// @brief マクロ本体を置換して xb の末尾へ積む。
/// @param m   マクロ番号
/// @param ofs 実引数 (生テキスト) の xb 内の開始位置
/// @param lns 同上: 長さ
/// @param xof 事前展開の結果の eb 内の開始位置。-1 = まだ展開していない
/// @param xln 同上: 長さ
/// @param n   実引数の個数
/// @details 仮引数の置換は 2 通りある。# や ## の作用対象なら生の実引数を，
///          そうでなければ展開しきった実引数を使う (C89 の規定)。
///          事前展開は必要になった時点で 1 度だけ行い xof に控える。
/// @note この関数は expandto を通じて自分自身へ再入する。作業領域を
///       大域に置いてはならない (nm と pastenext がローカルなのはこのため)。
int subst(int m, int *ofs, int *lns, int *xof, int *xln, int n) {
  int i;
  int e;
  int c;
  int k;
  int j;
  int p;
  int ts;
  int q;
  int raw;
  int pastenext;
  int pn;
  char nm[64];
  ts = xp;
  i = mcbo[m];
  e = i + mcbl[m];
  pastenext = 0;
  while (i < e) {
    pn = pastenext;
    pastenext = 0;
    c = mbody[i];
    if (c == 34 || c == 39) {
      // 本体中のリテラルは逐語で写す
      q = c;
      xput(c);
      i = i + 1;
      while (i < e && mbody[i] != q) {
        xput(mbody[i]);
        if (mbody[i] == 92 && i + 1 < e) {
          i = i + 1;
          xput(mbody[i]);
        }
        i = i + 1;
      }
      if (i >= e) exit(1);
      xput(mbody[i]);
      i = i + 1;
    } else if (c == '#' && i + 1 < e && mbody[i + 1] == '#') {
      // 連結: 直前に積んだ空白を削り，次の字句を隙間なく続ける
      while (xp > ts && xb[xp - 1] == 32) xp = xp - 1;
      i = i + 2;
      while (i < e && (mbody[i] == 32 || mbody[i] == 9)) i = i + 1;
      pastenext = 1;
    } else if (c == '#' && mcfun[m]) {
      // 文字列化: 直後が仮引数のときだけ働く
      j = i + 1;
      while (j < e && (mbody[j] == 32 || mbody[j] == 9)) j = j + 1;
      k = 0;
      while (j < e && isidc(mbody[j])) {
        if (k >= 63) exit(6);
        nm[k] = mbody[j];
        k = k + 1;
        j = j + 1;
      }
      nm[k] = 0;
      p = 0 - 1;
      if (k > 0) p = pfind(m, nm);
      if (p >= 0 && p < n) {
        strize(ofs[p], lns[p]);
        i = j;
      } else {
        xput(c);
        i = i + 1;
      }
    } else if (isidh(c)) {
      k = 0;
      while (i < e && isidc(mbody[i])) {
        if (k >= 63) exit(6);
        nm[k] = mbody[i];
        k = k + 1;
        i = i + 1;
      }
      nm[k] = 0;
      p = pfind(m, nm);
      if (p < 0 || p >= n) {
        k = 0;
        while (nm[k]) {
          xput(nm[k]);
          k = k + 1;
        }
      } else {
        // ## が前後どちらかに隣接していれば生の実引数を使う
        raw = pn;
        j = i;
        while (j < e && (mbody[j] == 32 || mbody[j] == 9)) j = j + 1;
        if (j + 1 < e && mbody[j] == '#' && mbody[j + 1] == '#') raw = 1;
        if (raw) {
          k = 0;
          while (k < lns[p]) {
            xput(xb[ofs[p] + k]);
            k = k + 1;
          }
        } else {
          if (xof[p] < 0) {
            xof[p] = expandto(ofs[p], lns[p]);
            xln[p] = epos - xof[p];
            // 展開結果の末尾には補いの空白が付いている。実引数として
            // 埋め込むときは落とす (`(3 )` のような見た目を避ける)
            while (xln[p] > 0 && eb[xof[p] + xln[p] - 1] == 32) xln[p] = xln[p] - 1;
          }
          k = 0;
          while (k < xln[p]) {
            xput(eb[xof[p] + k]);
            k = k + 1;
          }
        }
      }
    } else {
      xput(c);
      i = i + 1;
    }
  }
  return xp - ts;
}

/// @brief オブジェクト形式マクロを展開してフレームを積む。
int expobj(int m) {
  int mark;
  int ts;
  mark = xp;
  ts = xp;
  subst(m, 0, 0, 0, 0, 0);
  xput(32);
  pushx(ts, xp - ts, m, mark, 0);
  return 0;
}

/// @brief 関数形式マクロを展開してフレームを積む (開き括弧は読み終えている)。
/// @details xb の使い方: [mark, ts) に実引数の生テキスト，[ts, xp) に置換結果。
///          フレームを降ろすと mark まで一括で解放される。
int expfun(int m) {
  int agof[32];
  int aglen[32];
  int axof[32];
  int axlen[32];
  int mark;
  int ts;
  int es;
  int n;
  int i;
  int lim;
  mark = xp;
  es = epos;
  lim = 0 - 1;
  if (mcvar[m]) lim = mcnp[m] - 1;
  n = collect(agof, aglen, lim);
  // F() は「空の実引数 1 個」でもある。1 引数のマクロならそう解釈する
  if (n == 0 && mcnp[m] == 1) {
    agof[0] = xp;
    aglen[0] = 0;
    n = 1;
  }
  // 可変長で実引数が足りないときは __VA_ARGS__ を空にする
  if (mcvar[m] && n == mcnp[m] - 1) {
    agof[n] = xp;
    aglen[n] = 0;
    n = n + 1;
  }
  if (n != mcnp[m]) exit(3);
  i = 0;
  while (i < n) {
    axof[i] = 0 - 1;
    axlen[i] = 0;
    i = i + 1;
  }
  ts = xp;
  subst(m, agof, aglen, axof, axlen, n);
  xput(32);
  epos = es;
  pushx(ts, xp - ts, m, mark, 0);
  return 0;
}

/// @brief 直前に読んだ識別子 idn がマクロなら展開する。
int domacro() {
  int m;
  int c;
  int sp;
  if (streq(idn, "__FILE__")) return emitfile();
  if (streq(idn, "__LINE__")) return emitline();
  m = mfind(idn);
  if (m < 0) return emitid();
  if (active(m)) return emitid();
  if (mcfun[m] == 0) return expobj(m);
  // 関数形式は開き括弧が続くときだけ展開する。空白・改行は跨いでよい
  sp = 0;
  c = gcnc();
  while (isws(c)) {
    sp = 1;
    c = gcnc();
  }
  if (c != '(') {
    emitid();
    if (sp) emit(32);
    ungc(c);
    return 0;
  }
  return expfun(m);
}

// ---- 指令行の読取りと解析 ----

/// @brief dbuf へ 1 文字積む。
int dput(int c) {
  if (dn >= 8190) exit(6);
  dbuf[dn] = c;
  dn = dn + 1;
  return 0;
}

/// @brief 指令行 (# の次から行末まで) を dbuf へ読み取る。
/// @details コメントは空白へ畳むが，文字列・文字リテラルは逐語で残す
///          (`#define S "a//b"` の // をコメントにしないため)。
///          行末の改行は消費する。
int readline() {
  int c;
  int q;
  dn = 0;
  dp = 0;
  c = gcnc();
  while (c >= 0 && c != 10) {
    dput(c);
    if (c == 34 || c == 39) {
      q = c;
      c = gc();
      while (c >= 0 && c != 10 && c != q) {
        dput(c);
        if (c == 92) {
          c = gc();
          if (c < 0) exit(1);
          dput(c);
        }
        c = gc();
      }
      if (c == q) dput(c);
      else if (c >= 0) ungc(c);
    }
    c = gcnc();
  }
  dbuf[dn] = 0;
  return 0;
}

/// @brief dbuf の空白を読み飛ばす。
int dskip() {
  while (dp < dn && (dbuf[dp] == 32 || dbuf[dp] == 9 || dbuf[dp] == 13)) dp = dp + 1;
  return 0;
}

/// @brief dbuf から識別子を読んで o へ入れる。無ければ空文字列。
int dname(char *o) {
  int k;
  k = 0;
  if (dp < dn && isidh(dbuf[dp])) {
    while (dp < dn && isidc(dbuf[dp])) {
      if (k >= 63) exit(6);
      o[k] = dbuf[dp];
      k = k + 1;
      dp = dp + 1;
    }
  }
  o[k] = 0;
  return 0;
}

// ---- #if の定数式 ----

/// @brief eb から次のトークンを取り出し etok / enum_ へ入れる。
/// @note 識別子は「値 0 の数」として扱う (C89 の規定)。
int enext() {
  int c;
  int d;
  int done;
  while (evp < eve && isws(eb[evp])) evp = evp + 1;
  if (evp >= eve) {
    etok = te_end;
    return 0;
  }
  c = eb[evp];
  if (isidh(c)) {
    while (evp < eve && isidc(eb[evp])) evp = evp + 1;
    enum_ = 0;
    etok = te_num;
    return 0;
  }
  if (isdig(c)) {
    enum_ = 0;
    if (c == '0' && evp + 1 < eve && (eb[evp + 1] == 'x' || eb[evp + 1] == 'X')) {
      evp = evp + 2;
      done = 0;
      while (evp < eve && done == 0) {
        d = eb[evp];
        if (isdig(d)) { enum_ = enum_ * 16 + d - '0'; evp = evp + 1; }
        else if (d >= 'a' && d <= 'f') { enum_ = enum_ * 16 + d - 87; evp = evp + 1; }
        else if (d >= 'A' && d <= 'F') { enum_ = enum_ * 16 + d - 55; evp = evp + 1; }
        else done = 1;
      }
    } else {
      while (evp < eve && isdig(eb[evp])) {
        enum_ = enum_ * 10 + eb[evp] - '0';
        evp = evp + 1;
      }
    }
    // 接尾辞 u / l は読み飛ばす
    while (evp < eve && (eb[evp] == 'u' || eb[evp] == 'U' || eb[evp] == 'l' || eb[evp] == 'L')) {
      evp = evp + 1;
    }
    etok = te_num;
    return 0;
  }
  if (c == 39) {
    evp = evp + 1;
    if (evp >= eve) exit(4);
    d = eb[evp];
    if (d == 92) {
      evp = evp + 1;
      if (evp >= eve) exit(4);
      d = eb[evp];
      if (d == 'n') d = 10;
      else if (d == 't') d = 9;
      else if (d == 'r') d = 13;
      else if (d == '0') d = 0;
    }
    evp = evp + 1;
    if (evp >= eve || eb[evp] != 39) exit(4);
    evp = evp + 1;
    enum_ = d;
    etok = te_num;
    return 0;
  }
  evp = evp + 1;
  d = 0;
  if (evp < eve) d = eb[evp];
  if (c == '<' && d == '<') { evp = evp + 1; etok = te_shl; return 0; }
  if (c == '>' && d == '>') { evp = evp + 1; etok = te_shr; return 0; }
  if (c == '<' && d == '=') { evp = evp + 1; etok = te_le; return 0; }
  if (c == '>' && d == '=') { evp = evp + 1; etok = te_ge; return 0; }
  if (c == '=' && d == '=') { evp = evp + 1; etok = te_eq; return 0; }
  if (c == '!' && d == '=') { evp = evp + 1; etok = te_ne; return 0; }
  if (c == '&' && d == '&') { evp = evp + 1; etok = te_aa; return 0; }
  if (c == '|' && d == '|') { evp = evp + 1; etok = te_oo; return 0; }
  if (c == '(') { etok = te_lp; return 0; }
  if (c == ')') { etok = te_rp; return 0; }
  if (c == '+') { etok = te_add; return 0; }
  if (c == '-') { etok = te_sub; return 0; }
  if (c == '*') { etok = te_mul; return 0; }
  if (c == '/') { etok = te_div; return 0; }
  if (c == '%') { etok = te_mod; return 0; }
  if (c == '<') { etok = te_lt; return 0; }
  if (c == '>') { etok = te_gt; return 0; }
  if (c == '&') { etok = te_and; return 0; }
  if (c == '^') { etok = te_xor; return 0; }
  if (c == '|') { etok = te_or; return 0; }
  if (c == '!') { etok = te_not; return 0; }
  if (c == 126) { etok = te_tld; return 0; }
  if (c == '?') { etok = te_que; return 0; }
  if (c == ':') { etok = te_col; return 0; }
  exit(4);
  return 0;
}

/// @brief 一次式: 数・識別子 (0)・括弧。
int e_prim() {
  int v;
  if (etok == te_num) {
    v = enum_;
    enext();
    return v;
  }
  if (etok == te_lp) {
    enext();
    v = e_cond();
    if (etok != te_rp) exit(4);
    enext();
    return v;
  }
  exit(4);
  return 0;
}

/// @brief 単項 + - ! ~。
int e_un() {
  if (etok == te_sub) { enext(); return 0 - e_un(); }
  if (etok == te_add) { enext(); return e_un(); }
  if (etok == te_not) { enext(); if (e_un() == 0) return 1; return 0; }
  if (etok == te_tld) { enext(); return 0 - e_un() - 1; }
  return e_prim();
}

/// @brief 乗除。
/// @note 0 除算は 0 とする。&& / || が短絡しないため，
///       `defined(N) && 100 / N` のような式で停止しないようにするための規定
///       (docs/stage009-pp.md 3.4)。
int e_mul() {
  int v;
  int r;
  int op;
  v = e_un();
  while (etok == te_mul || etok == te_div || etok == te_mod) {
    op = etok;
    enext();
    r = e_un();
    if (op == te_mul) v = v * r;
    else if (r == 0) v = 0;
    else if (op == te_div) v = v / r;
    else v = v % r;
  }
  return v;
}

/// @brief 加減。
int e_add() {
  int v;
  int r;
  int op;
  v = e_mul();
  while (etok == te_add || etok == te_sub) {
    op = etok;
    enext();
    r = e_mul();
    if (op == te_add) v = v + r;
    else v = v - r;
  }
  return v;
}

/// @brief シフト。
int e_shift() {
  int v;
  int r;
  int op;
  v = e_add();
  while (etok == te_shl || etok == te_shr) {
    op = etok;
    enext();
    r = e_add();
    if (op == te_shl) v = v << r;
    else v = v >> r;
  }
  return v;
}

/// @brief 大小比較。
int e_rel() {
  int v;
  int r;
  int op;
  v = e_shift();
  while (etok == te_lt || etok == te_gt || etok == te_le || etok == te_ge) {
    op = etok;
    enext();
    r = e_shift();
    if (op == te_lt) v = v < r;
    else if (op == te_gt) v = v > r;
    else if (op == te_le) v = v <= r;
    else v = v >= r;
  }
  return v;
}

/// @brief 等価比較。
int e_eq() {
  int v;
  int r;
  int op;
  v = e_rel();
  while (etok == te_eq || etok == te_ne) {
    op = etok;
    enext();
    r = e_rel();
    if (op == te_eq) v = v == r;
    else v = v != r;
  }
  return v;
}

/// @brief ビット AND。
int e_band() {
  int v;
  v = e_eq();
  while (etok == te_and) {
    enext();
    v = v & e_eq();
  }
  return v;
}

/// @brief ビット XOR。
int e_bxor() {
  int v;
  v = e_band();
  while (etok == te_xor) {
    enext();
    v = v ^ e_band();
  }
  return v;
}

/// @brief ビット OR。
int e_bor() {
  int v;
  v = e_bxor();
  while (etok == te_or) {
    enext();
    v = v | e_bxor();
  }
  return v;
}

/// @brief 論理 AND (短絡しない)。
int e_land() {
  int v;
  int r;
  v = e_bor();
  while (etok == te_aa) {
    enext();
    r = e_bor();
    if (v != 0 && r != 0) v = 1;
    else v = 0;
  }
  return v;
}

/// @brief 論理 OR (短絡しない)。
int e_lor() {
  int v;
  int r;
  v = e_land();
  while (etok == te_oo) {
    enext();
    r = e_land();
    if (v != 0 || r != 0) v = 1;
    else v = 0;
  }
  return v;
}

/// @brief 条件演算子 (両辺を評価する)。
int e_cond() {
  int c;
  int a;
  int b;
  c = e_lor();
  if (etok != te_que) return c;
  enext();
  a = e_cond();
  if (etok != te_col) exit(4);
  enext();
  b = e_cond();
  if (c) return a;
  return b;
}

/// @brief dbuf の残りを xb へ写しつつ `defined` を 1 / 0 へ置き換える。
/// @details マクロ展開より先に行う。展開してしまうと `defined` の対象が
///          消えてしまうため (C89 の規定)。
int dodefined() {
  int c;
  int m;
  int par;
  int k;
  int q;
  while (dp < dn) {
    c = dbuf[dp];
    if (c == 34 || c == 39) {
      q = c;
      xput(c);
      dp = dp + 1;
      while (dp < dn && dbuf[dp] != q) {
        xput(dbuf[dp]);
        if (dbuf[dp] == 92 && dp + 1 < dn) {
          dp = dp + 1;
          xput(dbuf[dp]);
        }
        dp = dp + 1;
      }
      if (dp < dn) {
        xput(dbuf[dp]);
        dp = dp + 1;
      }
    } else if (isidh(c)) {
      dname(nbuf);
      if (streq(nbuf, "defined")) {
        dskip();
        par = 0;
        if (dp < dn && dbuf[dp] == '(') {
          par = 1;
          dp = dp + 1;
          dskip();
        }
        dname(nbuf);
        if (nbuf[0] == 0) exit(4);
        if (par) {
          dskip();
          if (dp >= dn || dbuf[dp] != ')') exit(4);
          dp = dp + 1;
        }
        m = mfind(nbuf);
        if (m >= 0) xput('1');
        else xput('0');
      } else {
        k = 0;
        while (nbuf[k]) {
          xput(nbuf[k]);
          k = k + 1;
        }
      }
    } else {
      xput(c);
      dp = dp + 1;
    }
  }
  return 0;
}

/// @brief dbuf の残りを #if の定数式として評価する。
/// @return 0 または 1
int evalif() {
  int mark;
  int st;
  int len;
  int es;
  int v;
  mark = xp;
  st = xp;
  dodefined();
  len = xp - st;
  es = expandto(st, len);
  evp = es;
  eve = epos;
  enext();
  if (etok == te_end) exit(4);
  v = e_cond();
  if (etok != te_end) exit(4);
  epos = es;
  xp = mark;
  if (v) return 1;
  return 0;
}

// ---- 指令 ----

/// @brief #define。
int d_define() {
  int m;
  int i;
  int k;
  int st;
  dskip();
  dname(nbuf);
  if (nbuf[0] == 0) exit(1);
  m = mslot(nbuf);
  mcon[m] = 1;
  mcfun[m] = 0;
  mcvar[m] = 0;
  mcnp[m] = 0;
  mcpo[m] = mpp;
  if (dp < dn && dbuf[dp] == '(') {
    mcfun[m] = 1;
    dp = dp + 1;
    dskip();
    if (dp < dn && dbuf[dp] == ')') {
      dp = dp + 1;
    } else {
      k = 0;
      while (k == 0) {
        dskip();
        if (mpp + 64 > 32768) exit(6);
        if (dp + 2 < dn && dbuf[dp] == '.' && dbuf[dp + 1] == '.' && dbuf[dp + 2] == '.') {
          dp = dp + 3;
          mcvar[m] = 1;
          nput(mprm + mpp, "__VA_ARGS__");
        } else {
          dname(nbuf);
          if (nbuf[0] == 0) exit(1);
          nput(mprm + mpp, nbuf);
        }
        mpp = mpp + 64;
        mcnp[m] = mcnp[m] + 1;
        if (mcnp[m] > 31) exit(6);
        dskip();
        if (dp < dn && dbuf[dp] == ',') dp = dp + 1;
        else if (dp < dn && dbuf[dp] == ')') { dp = dp + 1; k = 1; }
        else exit(1);
      }
    }
  }
  dskip();
  st = mbp;
  i = dp;
  k = dn;
  while (k > i && (dbuf[k - 1] == 32 || dbuf[k - 1] == 9 || dbuf[k - 1] == 13)) k = k - 1;
  while (i < k) {
    if (mbp >= 262144) exit(6);
    mbody[mbp] = dbuf[i];
    mbp = mbp + 1;
    i = i + 1;
  }
  mcbo[m] = st;
  mcbl[m] = mbp - st;
  return 0;
}

/// @brief #undef。
int d_undef() {
  int m;
  dskip();
  dname(nbuf);
  if (nbuf[0] == 0) exit(1);
  m = mfind(nbuf);
  if (m >= 0) mcon[m] = 0;
  return 0;
}

/// @brief #include。"..." と <...> はどちらも束ねのメンバを探す。
int d_include() {
  int q;
  int k;
  int i;
  dskip();
  if (dp >= dn) exit(1);
  q = dbuf[dp];
  if (q == 34) q = 34;
  else if (q == '<') q = '>';
  else exit(1);
  dp = dp + 1;
  k = 0;
  while (dp < dn && dbuf[dp] != q) {
    if (k >= 63) exit(6);
    nbuf[k] = dbuf[dp];
    k = k + 1;
    dp = dp + 1;
  }
  nbuf[k] = 0;
  if (dp >= dn || k == 0) exit(1);
  i = 0;
  while (i < mbcnt) {
    if (streq(mbname + i * 64, nbuf)) {
      pushfile(i);
      return 0;
    }
    i = i + 1;
  }
  exit(2);
  return 0;
}

/// @brief #if / #ifdef / #ifndef が段を 1 つ積む。
/// @param v この段の最初の枝を採用するか
int cpush(int v) {
  if (cdn >= 63) exit(6);
  cpen[cdn] = 0;
  if (nfalse == 0) cpen[cdn] = 1;
  if (cpen[cdn] == 0) v = 0;
  cact[cdn] = v;
  cany[cdn] = v;
  cels[cdn] = 0;
  if (v == 0) nfalse = nfalse + 1;
  cdn = cdn + 1;
  return 0;
}

/// @brief #elif / #else が枝を切り替える。
/// @param v 新しい枝を採用するか (#else は 1)
int cnext(int v) {
  int k;
  k = cdn - 1;
  if (k < 0) exit(4);
  if (cact[k]) {
    cact[k] = 0;
    nfalse = nfalse + 1;
    return 0;
  }
  if (cany[k] == 0 && cpen[k] && v) {
    cact[k] = 1;
    cany[k] = 1;
    nfalse = nfalse - 1;
  }
  return 0;
}

/// @brief 指令行を処理する (行頭の # は読み終えている)。
/// @details 指令の処理を終えた時点では，必ず次の行の行頭にいる。
///          #if の式を評価すると，展開フレームからの読取りが行頭性を
///          落としてしまうため，ここで明示的に立て直す
///          (これを怠ると，直後の行の指令が本文として素通しされる)。
int dodir() {
  dodir1();
  atbol = 1;
  return 0;
}

/// @brief 指令行の本処理。
int dodir1() {
  int m;
  int v;
  readline();
  dp = 0;
  dskip();
  dname(wbuf);
  // 条件指令は抑止区間でも処理する (入れ子を数えるため)
  if (streq(wbuf, "if")) {
    v = 0;
    if (nfalse == 0) v = evalif();
    cpush(v);
    emit(10);
    return 0;
  }
  if (streq(wbuf, "ifdef") || streq(wbuf, "ifndef")) {
    v = 0;
    if (nfalse == 0) {
      dskip();
      dname(nbuf);
      if (nbuf[0] == 0) exit(4);
      m = mfind(nbuf);
      if (streq(wbuf, "ifdef")) { if (m >= 0) v = 1; }
      else { if (m < 0) v = 1; }
    }
    cpush(v);
    emit(10);
    return 0;
  }
  if (streq(wbuf, "elif")) {
    v = 0;
    // 外側が真で，まだどの枝も採っていないときだけ式を評価する
    if (cdn > 0 && cpen[cdn - 1] && cany[cdn - 1] == 0 && cact[cdn - 1] == 0) v = evalif();
    cnext(v);
    emit(10);
    return 0;
  }
  if (streq(wbuf, "else")) {
    if (cdn == 0) exit(4);
    if (cels[cdn - 1]) exit(4);
    cels[cdn - 1] = 1;
    cnext(1);
    emit(10);
    return 0;
  }
  if (streq(wbuf, "endif")) {
    if (cdn == 0) exit(4);
    if (cact[cdn - 1] == 0) nfalse = nfalse - 1;
    cdn = cdn - 1;
    emit(10);
    return 0;
  }
  if (nfalse > 0) return 0;
  if (wbuf[0] == 0) { emit(10); return 0; }
  if (streq(wbuf, "define")) d_define();
  else if (streq(wbuf, "undef")) d_undef();
  else if (streq(wbuf, "include")) d_include();
  else if (streq(wbuf, "error")) exit(5);
  else if (streq(wbuf, "warning")) { }
  else if (streq(wbuf, "pragma")) { }
  else if (streq(wbuf, "line")) { }
  else exit(1);
  emit(10);
  return 0;
}

// ---- 走査の本体 ----

/// @brief 字句を 1 個処理する。
/// @return 1 = まだ続く, 0 = 入力 (または番兵フレーム) が尽きた
/// @details 展開結果の再走査もこの関数が行う。フレームの積みから読むので，
///          「今どこを読んでいるか」を意識する必要がない。
int scan1() {
  int c;
  c = gcnc();
  if (c < 0) return 0;
  // 指令はファイル由来の行頭の # だけ。マクロ展開の中の # は指令ではない
  if (c == '#' && wasbol && fromfile) {
    dodir();
    return 1;
  }
  // 抑止区間。リテラルは読み飛ばす (中の # を指令と誤らないため)。
  // edep > 0 のときは #elif の式を評価している最中であり，抑止区間の
  // 内側であっても式は通常どおり展開しなければならない
  if (nfalse > 0 && edep == 0) {
    if (c == 34 || c == 39) dolit(c, 0);
    return 1;
  }
  if (c == 34 || c == 39) {
    dolit(c, 1);
    return 1;
  }
  if (isdig(c)) {
    emitnum(c);
    return 1;
  }
  if (isidh(c)) {
    readid(c);
    domacro();
    return 1;
  }
  emit(c);
  return 1;
}

/// @brief 組込みのマクロ定義を登録する。
int predef() {
  int m;
  m = mslot("__STDC__");
  mcon[m] = 1; mcfun[m] = 0; mcvar[m] = 0; mcnp[m] = 0; mcpo[m] = mpp;
  mcbo[m] = mbp; mbody[mbp] = '1'; mbp = mbp + 1; mcbl[m] = 1;
  m = mslot("__STONE__");
  mcon[m] = 1; mcfun[m] = 0; mcvar[m] = 0; mcnp[m] = 0; mcpo[m] = mpp;
  mcbo[m] = mbp; mbody[mbp] = '1'; mbp = mbp + 1; mcbl[m] = 1;
  return 0;
}

int main() {
  init();
  rdall();
  bundle();
  xp = 0;
  epos = 0;
  edep = 0;
  pbn = 0;
  pbbase = 0;
  ftop = 0 - 1;
  cdn = 0;
  nfalse = 0;
  atbol = 1;
  wasbol = 1;
  fromfile = 1;
  mccnt = 0;
  mbp = 0;
  mpp = 0;
  predef();
  pushfile(mbcnt - 1);
  while (scan1()) { }
  if (cdn != 0) exit(4);
  putc(eot);
  return 0;
}
