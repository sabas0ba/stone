/// @file scc.sc
/// @brief sc コンパイラの sc 言語による再記述 (セルフホスト)。
///
/// 設計は docs/stage006-scc.md，入力言語の仕様は docs/stage005-sc.md 2 章。
/// 構成・コード生成テンプレート・エラーコードは Stage 5 実装 (sc.sol) と同一で，
/// 実装言語だけを sol から sc へ移したものである。
///
/// @section why セルフホストの意味
/// Stage 5 までは「処理対象の言語 (sc)」と「処理系を書いた言語 (sol)」が
/// 食い違っており，ビルドは常に下位の言語に依存していた。本 Stage で
/// sc コンパイラを sc 自身で書き直すことで，以降の改訂は sc 言語だけで
/// 完結する。正しくセルフホストできていることは固定点で機械的に示す:
///
///   B1 = C0(S)   下位チェーン (Stage 5 の sc) で bootstrap した scc
///   B2 = B1(S)   scc が自分自身をコンパイルしたもの
///   B3 = B2(S)   さらにもう一段
///
/// 完了条件は B2 == B3。本実装はコード生成が Stage 5 と同一なので
/// B1 == B2 も成立し，テストで併せて確認している。
///
/// @section codegen コード生成の方針
/// 単一パス。構文解析がそのまま命令を吐き，前方分岐と前方参照の呼出しは
/// 出力バッファ ob への後埋め (backpatch) で解決する。式の値はすべて
/// メモリ上のデータスタック (x9) を経由する固定テンプレートの展開であり，
/// レジスタ割付けは行わない (それは Stage 7 の occ.sc の役目)。
///
/// @section limits sc 言語側の制約に由来する書き方
/// - 構造体配列が書けないため，表は添字を共有する「並行配列」で表す
/// - 定数構文がないため，種別定数は大域変数として init() で設定する
/// - 「見つからない」は -1。0 は正当な添字なので不可を表せない

// ---- 領域: 入出力バッファと記号表 ----
// 実行時は生成コードの BSS (0x8010_0000 から) に確保される。
// 記号表はいずれも並行配列で，同じ添字 e が 1 エントリを指す。

char src[262144];         ///< 入力ソース全体 (EOT 0x04 まで読み込む)
char ob[524288];          ///< 生成バイナリ。後から書き戻すため一旦ここへ溜める

char gname[32768];        ///< 大域記号の名前 (16 バイト固定スロット x 2048)
int gkind[2048];          ///< 種別: 0 = 変数, 1 = 関数
int gty[2048];            ///< 型 (変数は自身の型，関数は返却型)
int gval[2048];           ///< 変数: 絶対アドレス / 関数: 定義済みならコード位置，未定義なら未解決呼出しリストの先頭
int gdef[2048];           ///< 1 = 定義済み。0 のまま入力が終われば未解決の前方参照 (エラー 2)
int garr[2048];           ///< 1 = 配列。式中では先頭要素へのポインタに退化する
int gna[2048];            ///< 引数個数。-1 = 未知 (前方参照で個数がまだ判らない)
int gcnt;                 ///< 大域記号の登録数

char lname[4096];         ///< ローカル記号の名前 (16 バイト x 256)。関数ごとに作り直す
int lty[256];             ///< 型
int loff[256];            ///< フレームポインタ x8 からのオフセット
int larr[256];            ///< 1 = 配列
int lcnt;                 ///< ローカル記号の登録数

char sname[4096];         ///< 構造体名 (16 バイト x 256)
int ssize[256];           ///< 構造体のサイズ (4 バイト境界へ切り上げ済み)
int scnt;                 ///< 構造体の登録数

char mname[32768];        ///< メンバ名 (16 バイト x 2048)。全構造体のメンバを 1 本の表に持つ
int msid[2048];           ///< 所属する構造体の番号。探索はこれで絞り込む
int mty[2048];            ///< メンバの型
int moff[2048];           ///< 構造体先頭からのオフセット
int marr[2048];           ///< 1 = 配列メンバ
int mcnt;                 ///< メンバの登録数

char tname[16];           ///< 直近に読んだ識別子。記号表の探索はすべてこれを鍵にする
char snam[16];            ///< struct 名の退避先 (tname は変数名で上書きされるため)
char sbuf[256];           ///< 文字列リテラルの組立てバッファ
int slen;                 ///< sbuf の有効長

int pos;                  ///< src 内の読取り位置
int tok;                  ///< 現在のトークン種別
int tval;                 ///< 現在のトークンの値 (数値・文字リテラル)
int outp;                 ///< ob への書込み位置 (= 生成コードのオフセット)
int bssp;                 ///< 大域変数の割付けポインタ (0x8010_0000 から上向き)

int ety;                  ///< 直前に解析した式の型 = (ポインタ深さ << 16) | 基底 (0=char, 1=int, 2+k=構造体 k)
int elv;                  ///< 1 = その式は左辺値 (値ではなくアドレスが手元にある)
int fsz;                  ///< 解析中の関数のフレームサイズ (エピローグで解放する量)
int cloff;                ///< 解析中の関数のローカル割付けポインタ
int cna;                  ///< 解析中の関数の引数個数
int cty;                  ///< 解析中の宣言の型
int mainok;               ///< 1 = main を定義済み
int mainoff;              ///< main のコード位置 (ランタイム前置部から呼ぶために保持)
int *wp;                  ///< char 配列 ob へ語単位で書くための作業ポインタ

// ---- トークン種別 ----
// sc に定数構文がないため大域変数として持ち，init() で値を入れる。
// t_* トークンの大分類, k_* 予約語, o_* 演算子・記号。
int eot;
int t_eof; int t_num; int t_str; int t_id;
int k_int; int k_char; int k_struct; int k_if; int k_else; int k_while; int k_return;
int o_asn; int o_lt; int o_gt; int o_add; int o_sub; int o_mul; int o_div; int o_mod;
int o_amp; int o_or; int o_xor; int o_not; int o_lp; int o_rp; int o_lb; int o_rb;
int o_lc; int o_rc; int o_semi; int o_comma; int o_dot;
int o_eq; int o_ne; int o_le; int o_ge; int o_shl; int o_shr; int o_aa; int o_oo; int o_arrow;

int init() {
  eot = 4;
  t_eof = 0; t_num = 1; t_str = 2; t_id = 3;
  k_int = 10; k_char = 11; k_struct = 12; k_if = 13; k_else = 14; k_while = 15; k_return = 16;
  o_asn = 30; o_lt = 31; o_gt = 32; o_add = 33; o_sub = 34; o_mul = 35; o_div = 36; o_mod = 37;
  o_amp = 38; o_or = 39; o_xor = 40; o_not = 41; o_lp = 42; o_rp = 43; o_lb = 44; o_rb = 45;
  o_lc = 46; o_rc = 47; o_semi = 48; o_comma = 49; o_dot = 50;
  o_eq = 51; o_ne = 52; o_le = 53; o_ge = 54; o_shl = 55; o_shr = 56; o_aa = 57; o_oo = 58; o_arrow = 59;
  return 0;
}

// ---- 名前操作 ----
// 記号表の名前は 16 バイト固定スロットに 0 詰めで格納する。可変長にすると
// 領域管理が要るのに対し，識別子は 15 バイト以下と仕様で決めてあるため。

/// @brief NUL 終端文字列の同値判定。
/// @param a 比較元
/// @param b 比較先
/// @return 1 = 一致, 0 = 不一致
int streq(char *a, char *b) {
  int i;
  i = 0;
  while (a[i] == b[i]) {
    if (a[i] == 0) return 1;
    i = i + 1;
  }
  return 0;
}
/// @brief 名前スロットの複写 (常に 16 バイト固定)。
/// @param d 複写先スロット
/// @param s 複写元スロット
/// @return 常に 0
int copyn(char *d, char *s) {
  int i;
  i = 0;
  while (i < 16) { d[i] = s[i]; i = i + 1; }
  return 0;
}

// ---- 字句解析 ----
// 1 文字先読みのみで済む単純な字句。トークンは tok / tval / tname に入る。

/// @brief 現在位置の 1 文字を返す (消費しない)。
int getch() { return src[pos]; }
/// @brief 読取り位置を 1 進める。
int adv() { pos = pos + 1; return 0; }

/// @brief 空白か (SP TAB CR LF)。
int isws(int c) { return c == 32 || c == 9 || c == 13 || c == 10; }
/// @brief 10 進数字か。
int isdig(int c) { return c >= '0' && c <= '9'; }
/// @brief 識別子の先頭に置ける文字か (英小文字と _)。大文字は仕様で不可。
int isidh(int c) { return (c >= 'a' && c <= 'z') || c == '_'; }
/// @brief 識別子の 2 文字目以降に置ける文字か。
int isidc(int c) { return isdig(c) || isidh(c); }
/// @brief 16 進数字か (小文字のみ)。
int ishex(int c) { return isdig(c) || (c >= 'a' && c <= 'f'); }

/// @brief 16 進数字を数値へ。
/// @param c '0'..'9' または 'a'..'f'
/// @return 0..15。'a' は 97 なので 87 を引くと 10 になる
int hexv(int c) {
  if (c > '9') return c - 87;
  return c - '0';
}

/// @brief エスケープ文字を値へ変換する。
/// @param c 逆斜線の次の 1 文字
/// @return 対応する文字コード。未対応の文字は構文エラー (終了コード 1) で停止する
int escv(int c) {
  if (c == 'n') return 10;
  if (c == 't') return 9;
  if (c == '0') return 0;
  if (c == 92) return 92;
  if (c == 39) return 39;
  if (c == 34) return 34;
  exit(1);
  return 0;
}

/// @brief 空白と行コメント (// 以降) を読み飛ばす。
/// @return 常に 0
/// @note コメント中に EOT が来た場合はそこで打ち切る。打ち切らないと
///       終端のないコメントで src の末尾を越えて走り続けてしまう。
int skipwc() {
  int c;
  c = getch();
  while (isws(c) || (c == '/' && src[pos + 1] == '/')) {
    if (isws(c)) adv();
    else {
      while (getch() != 10) {
        if (getch() == eot) return 0;
        adv();
      }
    }
    c = getch();
  }
  return 0;
}

/// @brief 整数リテラルを読み tval へ入れる (10 進 / 0x 16 進)。
/// @return 常に 0
int lexnum() {
  tval = 0;
  if (getch() == '0' && src[pos + 1] == 'x') {
    adv(); adv();
    if (!ishex(getch())) exit(1);
    while (ishex(getch())) { tval = tval * 16 + hexv(getch()); adv(); }
  } else {
    while (isdig(getch())) { tval = tval * 10 + getch() - '0'; adv(); }
  }
  tok = t_num;
  return 0;
}

/// @brief 識別子を読んで tname へ格納し，予約語ならその種別を tok に入れる。
/// @return 常に 0
/// @note 予約語は独立した表を持たず，読み終えた後に streq で突き合わせる。
///       語数が 7 個と少なく，表を引くより短く済むため。
int lexid() {
  int n;
  n = 0;
  while (isidc(getch())) {
    if (n == 15) exit(1);
    tname[n] = getch();
    n = n + 1;
    adv();
  }
  while (n < 16) { tname[n] = 0; n = n + 1; }
  if (streq(tname, "int")) { tok = k_int; return 0; }
  if (streq(tname, "char")) { tok = k_char; return 0; }
  if (streq(tname, "struct")) { tok = k_struct; return 0; }
  if (streq(tname, "if")) { tok = k_if; return 0; }
  if (streq(tname, "else")) { tok = k_else; return 0; }
  if (streq(tname, "while")) { tok = k_while; return 0; }
  if (streq(tname, "return")) { tok = k_return; return 0; }
  tok = t_id;
  return 0;
}

/// @brief 文字リテラルを読み，その文字コードを tval へ入れる。
/// @return 常に 0
int lexchr() {
  adv();
  if (getch() == eot) exit(1);
  if (getch() == 92) { adv(); tval = escv(getch()); }
  else tval = getch();
  adv();
  if (getch() != 39) exit(1);
  adv();
  tok = t_num;
  return 0;
}

/// @brief 文字列リテラルを読み sbuf / slen へ入れる。
/// @return 常に 0
/// @note 末尾に 0 を 4 個書くのは，後段が語単位で 4 バイト境界まで
///       切り上げて出力するため。境界埋めの分まで確実に 0 にしておく。
int lexstr() {
  int c;
  adv();
  slen = 0;
  while (getch() != 34) {
    if (getch() == eot) exit(1);
    if (slen == 255) exit(6);
    if (getch() == 92) { adv(); c = escv(getch()); }
    else c = getch();
    sbuf[slen] = c;
    adv();
    slen = slen + 1;
  }
  adv();
  sbuf[slen] = 0;
  sbuf[slen + 1] = 0;
  sbuf[slen + 2] = 0;
  sbuf[slen + 3] = 0;
  tok = t_str;
  return 0;
}

/// @brief 演算子・区切り記号を読んで tok に入れる。
/// @return 常に 0
/// @note 2 文字演算子 (== != <= >= << >> && ||) は，1 文字目を消費した後に
///       次の文字を覗いて分岐する。先に長い方を試すのが要点で，例えば
///       '<' を見た時点で o_lt を確定してしまうと "<=" が壊れる。
int lexop() {
  int c;
  c = getch();
  adv();
  if (c == '=') {
    if (getch() == '=') { adv(); tok = o_eq; } else tok = o_asn;
    return 0;
  }
  if (c == '!') {
    if (getch() == '=') { adv(); tok = o_ne; } else tok = o_not;
    return 0;
  }
  if (c == '<') {
    if (getch() == '=') { adv(); tok = o_le; return 0; }
    if (getch() == '<') { adv(); tok = o_shl; return 0; }
    tok = o_lt;
    return 0;
  }
  if (c == '>') {
    if (getch() == '=') { adv(); tok = o_ge; return 0; }
    if (getch() == '>') { adv(); tok = o_shr; return 0; }
    tok = o_gt;
    return 0;
  }
  if (c == '&') {
    if (getch() == '&') { adv(); tok = o_aa; return 0; }
    tok = o_amp;
    return 0;
  }
  if (c == '|') {
    if (getch() == '|') { adv(); tok = o_oo; return 0; }
    tok = o_or;
    return 0;
  }
  if (c == '-') {
    if (getch() == '>') { adv(); tok = o_arrow; return 0; }
    tok = o_sub;
    return 0;
  }
  if (c == '+') { tok = o_add; return 0; }
  if (c == '*') { tok = o_mul; return 0; }
  if (c == '/') { tok = o_div; return 0; }
  if (c == '%') { tok = o_mod; return 0; }
  if (c == '^') { tok = o_xor; return 0; }
  if (c == '(') { tok = o_lp; return 0; }
  if (c == ')') { tok = o_rp; return 0; }
  if (c == '[') { tok = o_lb; return 0; }
  if (c == ']') { tok = o_rb; return 0; }
  if (c == '{') { tok = o_lc; return 0; }
  if (c == '}') { tok = o_rc; return 0; }
  if (c == ';') { tok = o_semi; return 0; }
  if (c == ',') { tok = o_comma; return 0; }
  if (c == '.') { tok = o_dot; return 0; }
  exit(1);
  return 0;
}

/// @brief トークンを 1 個読み進める。結果は tok / tval / tname に入る。
/// @return 常に 0
int next() {
  int c;
  skipwc();
  c = getch();
  if (c == eot) { tok = t_eof; return 0; }
  if (isdig(c)) return lexnum();
  if (isidh(c)) return lexid();
  if (c == 39) return lexchr();
  if (c == 34) return lexstr();
  return lexop();
}

// ---- 出力バッファ ----
// 生成コードは UART へ直接流さず ob へ溜める。前方分岐や前方参照の呼出しを
// 後から書き戻す (backpatch) 必要があり，一度流したものは直せないため。
// sc には語単位で char 配列を触る構文がないので，int ポインタ wp を経由する。

/// @brief 現在位置へ 1 語書き，位置を 4 進める。
/// @param w 書き込む命令語
/// @return 常に 0
int outw(int w) {
  wp = ob + outp;
  *wp = w;
  outp = outp + 4;
  return 0;
}
/// @brief 現在位置へ 1 バイト書く (文字列リテラルの実体出力に使う)。
/// @param b 書き込むバイト
/// @return 常に 0
int outbyte(int b) {
  ob[outp] = b;
  outp = outp + 1;
  return 0;
}
/// @brief 既に書いた位置へ語を上書きする (後埋め)。
/// @param w 新しい語
/// @param off 上書きする出力オフセット
/// @return 常に 0
int patw(int w, int off) {
  wp = ob + off;
  *wp = w;
  return 0;
}
/// @brief 既に書いた語を読み出す。
/// @param off 読み出す出力オフセット
/// @return その位置の語
int getw(int off) {
  wp = ob + off;
  return *wp;
}

// ---- J/B-type 即値の合成 ----
/// @brief J 形式の即値を命令語のビット位置へ散らす。
/// @param rel 相対変位 (バイト単位, 偶数)
/// @return base と OR して使う即値部分
/// @note 配置は imm[20] -> bit31, imm[10:1] -> bit30:21, imm[11] -> bit20,
///       imm[19:12] -> bit19:12。bit0 は常に 0 なので符号化されない。
int jenc(int rel) {
  return (((rel >> 20) & 1) << 31) | (((rel >> 1) & 1023) << 21)
       | (((rel >> 11) & 1) << 20) | (((rel >> 12) & 255) << 12);
}
/// @brief B 形式の即値を命令語のビット位置へ散らす。
/// @param rel 相対変位 (バイト単位, 偶数)
/// @return base と OR して使う即値部分
/// @warning 表現範囲は ±4KiB しかないが，本実装は距離検査をしない。
///          本体が 4KiB を超える if / while は誤ったアドレスへ飛ぶコードに
///          なる (Stage 7 で発見・修正した制限。docs/stage007-occ.md 3 章)。
int benc(int rel) {
  return (((rel >> 12) & 1) << 31) | (((rel >> 5) & 63) << 25)
       | (((rel >> 1) & 15) << 8) | (((rel >> 11) & 1) << 7);
}

// ---- コード生成テンプレート (Stage 5 実装と同一) ----
/// @brief データスタックの先頭を x10 へ取り出して捨てる (2 語)。
int epop() { outw(0x0004a503); outw(0x00448493); return 0; }
/// @brief x10 の値をデータスタックへ積む (2 語)。
int epush() { outw(0xffc48493); outw(0x00a4a023); return 0; }
/// @brief 定数をデータスタックへ積む (lui + addi + push の 4 語)。
/// @param v 積む値
/// @return 常に 0
/// @note addi の即値は符号拡張されるため，下位 12 bit の最上位が立つ値では
///       上位側が 1 足りなくなる。lui へ渡す前に +2048 して相殺している。
int elit(int v) {
  outw(((v + 2048) & 0xfffff000) | 0x537);
  outw((v << 20) | 0x50513);
  return epush();
}
/// @brief データスタック上位 2 個を入れ替える (4 語)。
int eswp() { outw(0x0004a503); outw(0x0044a583); outw(0x00a4a223); outw(0x00b4a023); return 0; }
/// @brief 二項演算を展開する (上位 2 個を取り出し，演算し，結果を積む)。
/// @param w 演算そのものを行う 1 語 (x10, x11 に対して働く)
/// @return 常に 0
int ebin(int w) {
  outw(0x0004a503); outw(0x0044a583);
  outw(w);
  outw(0x00448493); outw(0x00b4a023);
  return 0;
}
/// @brief 2 語を要する二項演算を展開する (比較演算の補正付きなど)。
/// @param w1 主たる演算の語
/// @param w2 補正の語
/// @return 常に 0
int ebin2(int w1, int w2) {
  outw(0x0004a503); outw(0x0044a583);
  outw(w1); outw(w2);
  outw(0x00448493); outw(0x00b4a023);
  return 0;
}
/// @brief 型が指す実体 1 個の大きさ (バイト)。ポインタ演算のスケール係数になる。
/// @param t 型
/// @return バイト数
int tsize(int t) {
  if ((t >> 16) != 0) return 4;
  if (t == 0) return 1;
  if (t == 1) return 4;
  return ssize[t - 2];
}
/// @brief その型の記憶域へアクセスする幅 (バイト)。1 なら lbu/sb, 4 なら lw/sw。
/// @param t 型
/// @return 1 または 4
/// @note tsize と違い構造体でも 4 を返す。構造体そのものを 1 命令で読み書き
///       することはなく，この関数はスカラのロード/ストア幅の選択にしか使わない。
int bytesz(int t) {
  if ((t >> 16) != 0) return 4;
  if (t == 0) return 1;
  return 4;
}
/// @brief スタック先頭のアドレスを，その指す値で置き換える。
/// @param t 指し先の型 (アクセス幅の決定に使う)
/// @return 常に 0
int eload(int t) {
  outw(0x0004a503);
  if (bytesz(t) == 1) outw(0x00054503); else outw(0x00052503);
  outw(0x00a4a023);
  return 0;
}
/// @brief ( アドレス, 値 -- 値 ) の格納を展開する。
/// @param t 格納先の型 (アクセス幅の決定に使う)
/// @return 常に 0
/// @note 代入式の値は「格納した値」なので，格納後もスタックに残す。
int estore(int t) {
  outw(0x0004a503);
  outw(0x0044a583);
  if (bytesz(t) == 1) outw(0x00a58023); else outw(0x00a5a023);
  outw(0x00a4a223);
  outw(0x00448493);
  return 0;
}
/// @brief スタック先頭を sz 倍する (ポインタ演算のスケーリング)。
/// @param sz 要素サイズ (バイト)
/// @return 常に 0
/// @note 1 なら何も出さず，4 なら 2 bit シフト，それ以外は乗算に落とす。
int escale(int sz) {
  if (sz == 1) return 0;
  if (sz == 4) { outw(0x0004a503); outw(0x00251513); outw(0x00a4a023); return 0; }
  outw(0x0004a503);
  outw(((sz + 2048) & 0xfffff000) | 0x5b7);
  outw((sz << 20) | 0x58593);
  outw(0x02b50533);
  outw(0x00a4a023);
  return 0;
}
/// @brief スタック先頭を sz で割る (ポインタ同士の差から要素数を得る)。
/// @param sz 要素サイズ (バイト)
/// @return 常に 0
int ediv(int sz) {
  if (sz == 1) return 0;
  if (sz == 4) { outw(0x0004a503); outw(0x00255513); outw(0x00a4a023); return 0; }
  outw(0x0004a503);
  outw(((sz + 2048) & 0xfffff000) | 0x5b7);
  outw((sz << 20) | 0x58593);
  outw(0x02b54533);
  outw(0x00a4a023);
  return 0;
}
/// @brief ローカル変数のアドレスをスタックへ積む。
/// @param off フレームポインタ x8 からのオフセット
/// @return 常に 0
int eladdr(int off) {
  outw((off << 20) | 0x40513);
  return epush();
}
/// @brief スタック先頭のアドレスに定数を加算する (メンバのオフセット加算)。
/// @param off 加算量
/// @return 常に 0
/// @note 0 なら何も出さない。2047 を超える場合は即値に収まらないので
///       lui + addi で作ってから加算する。
int eoffs(int off) {
  if (off == 0) return 0;
  if (off > 2047) {
    outw(0x0004a503);
    outw(((off + 2048) & 0xfffff000) | 0x5b7);
    outw((off << 20) | 0x58593);
    outw(0x00b50533);
    outw(0x00a4a023);
    return 0;
  }
  outw(0x0004a503);
  outw((off << 20) | 0x50513);
  outw(0x00a4a023);
  return 0;
}
/// @brief sw x10, off(x8) の命令語を組み立てる。
/// @param off フレームポインタからのオフセット
/// @return 命令語
/// @note S 形式は 12 bit の変位が上位 7 bit と下位 5 bit に分かれて入る。
int swx8(int off) {
  return ((off >> 5) << 25) | ((off & 31) << 7) | 0x00a42023;
}
/// @brief エピローグ (戻り先とフレームポインタの復元・フレーム解放・復帰)。
/// @return 常に 0
/// @note 返却値はデータスタックに積んだ状態で来る。return のたびに出力する
///       ので 1 つの関数に複数回現れうる。
int eepilog() {
  outw(0x00012083);
  outw(0x00412403);
  outw((fsz << 20) | 0x10113);
  outw(0x00008067);
  return 0;
}

// ---- 記号表 (未発見は -1) ----
/// @brief tname と一致する大域記号を探す。
/// @return エントリ添字。見つからなければ -1
int gfind() {
  int i;
  i = 0;
  while (i < gcnt) {
    if (streq(gname + i * 16, tname)) return i;
    i = i + 1;
  }
  return -1;
}
/// @brief tname で大域記号を新規登録する (名前のみ設定。属性は呼び手が埋める)。
/// @return 新しいエントリ添字。表が満杯なら終了コード 6 で停止する
int gnew() {
  int e;
  if (gcnt > 2047) exit(6);
  e = gcnt;
  gcnt = gcnt + 1;
  copyn(gname + e * 16, tname);
  return e;
}
/// @brief tname と一致するローカル記号 (引数・ローカル変数) を探す。
/// @return エントリ添字。見つからなければ -1
/// @note ローカルを先に引き，無ければ大域を引く。これが名前の遮蔽になる。
int lfind() {
  int i;
  i = 0;
  while (i < lcnt) {
    if (streq(lname + i * 16, tname)) return i;
    i = i + 1;
  }
  return -1;
}
/// @brief tname でローカル記号を新規登録する。
/// @return 新しいエントリ添字。表が満杯なら終了コード 6 で停止する
int lnew() {
  int e;
  if (lcnt > 255) exit(6);
  e = lcnt;
  lcnt = lcnt + 1;
  copyn(lname + e * 16, tname);
  return e;
}
/// @brief tname と一致する構造体を探す。
/// @return 構造体番号。見つからなければ -1
int sfind() {
  int i;
  i = 0;
  while (i < scnt) {
    if (streq(sname + i * 16, tname)) return i;
    i = i + 1;
  }
  return -1;
}
/// @brief snam (退避した struct 名) と一致する構造体を探す。
/// @return 構造体番号。見つからなければ -1
/// @note sfind と同じ処理だが鍵が違う。"struct foo bar;" の解析では
///       tname が既に変数名 bar で上書きされているため，型名は snam から引く。
int sfind2() {
  int i;
  i = 0;
  while (i < scnt) {
    if (streq(sname + i * 16, snam)) return i;
    i = i + 1;
  }
  return -1;
}
/// @brief 構造体 k のメンバのうち tname と一致するものを探す。
/// @param k 構造体番号
/// @return メンバ表の添字。見つからなければ -1
/// @note メンバは全構造体で 1 本の表に並べ，msid で所属を絞る。
int mfind(int k) {
  int i;
  i = 0;
  while (i < mcnt) {
    if (msid[i] == k) {
      if (streq(mname + i * 16, tname)) return i;
    }
    i = i + 1;
  }
  return -1;
}

// ---- 呼出しの解決 (前方参照はプレースホルダ内の連結リスト) ----
/// @brief 未解決だった前方参照の呼出しを，確定した関数アドレスで埋める。
/// @param d 関数の実アドレス (出力オフセット)
/// @param h 未解決リストの先頭 (0 = なし)
/// @return 常に 0
/// @note 未解決の呼出しは，まだ書けない jal の語そのものに「1 つ前の
///       未解決位置」を書き込んで数珠つなぎにしてある。別途リスト用の
///       配列を持たずに済ませるための手である。
int patchcalls(int d, int h) {
  int nx;
  while (h) {
    nx = getw(h);
    patw(jenc(d - h) | 0xef, h);
    h = nx;
  }
  return 0;
}
/// @brief 関数呼出しの jal を出力する。未定義なら未解決リストへ繋ぐ。
/// @param e 呼び出す関数の大域記号番号
/// @return 常に 0
int ecall(int e) {
  int h;
  if (gdef[e]) { outw(jenc(gval[e] - outp) | 0xef); return 0; }
  h = gval[e];
  gval[e] = outp;
  outw(h);
  return 0;
}

// ---- 型の解析 ----
//
// 型は 1 個の int に詰める:  ty = (ポインタ深さ << 16) | 基底
//   基底 0 = char, 1 = int, 2+k = 構造体 k
// この表現なら「* を 1 つ被せる/剥がす」が 65536 の加減算になり，
// ポインタかどうかの判定が (ty >> 16) で済む。型表を別に持たなくてよい。

/// @brief 基底型 (int / char / struct 名) を読む。
/// @return 基底型の番号。型として不正なら終了コード 1，未知の構造体なら 2 で停止する
int ptype() {
  int k;
  if (tok == k_int) { next(); return 1; }
  if (tok == k_char) { next(); return 0; }
  if (tok == k_struct) {
    next();
    if (tok != t_id) exit(1);
    k = sfind();
    if (k < 0) exit(2);
    next();
    return k + 2;
  }
  exit(1);
  return 0;
}
/// @brief 基底型に続く '*' を読み，ポインタ深さを足し込む。
/// @param b 基底型
/// @return 完全な型
int pstars(int b) {
  while (tok == o_mul) { b = b + 65536; next(); }
  return b;
}

// ---- 式 ----
//
// 各関数は解析した式のコードを出力し，結果はデータスタックの先頭に残る。
// 補助的な状態を 2 つの大域で伝える:
//   ety … その式の型
//   elv … 1 なら「左辺値」= スタックにあるのは値ではなくアドレス
//
// 変数を読んだ直後は elv = 1 (アドレスだけ積んだ状態) にしておき，
// 実際に値が要る場面で rv() を通してロードを出す。こうすると
// 代入の左辺や & の対象では余計なロードを出さずに済む。
// 「値が要る」側の演算子はすべて自分でオペランドを rv() に通す責任を持つ。

/// @brief 左辺値なら値へ変換する (必要ならロードを出力する)。
/// @return 常に 0
int rv() {
  if (elv) { eload(ety); elv = 0; }
  return 0;
}

/// @brief 文字列リテラルの実体をコード中に埋め込み，先頭アドレスを積む。
/// @return 常に 0
/// @note 実体は命令列の途中に置くので，実行が流れ込まないよう手前に
///       跳び越しの jal を出す。長さは 4 バイト境界へ切り上げ，
///       後続の命令が境界を保つようにする。
int estr2() {
  int p; int a; int i;
  p = (slen + 4) & 0xfffffffc;
  outw(jenc(p + 4) | 0x6f);
  a = outp + 0x80000000;
  i = 0;
  while (i < p) { outbyte(sbuf[i]); i = i + 1; }
  elit(a);
  ety = 65536;
  elv = 0;
  next();
  return 0;
}

/// @brief メンバ参照 (. および ->) を解析し，メンバのアドレスを積む。
/// @param k 構造体番号
/// @return 常に 0
/// @note 呼び出し時点でトークンは '.' か '->' を指している。
///       配列メンバは先頭要素へのポインタに退化させるので elv = 0 とし，
///       スカラメンバは左辺値 (elv = 1) のままにしてロードを遅延させる。
int emember(int k) {
  int m;
  next();
  if (tok != t_id) exit(1);
  m = mfind(k);
  if (m < 0) exit(5);
  next();
  eoffs(moff[m]);
  if (marr[m]) { ety = mty[m] + 65536; elv = 0; }
  else { ety = mty[m]; elv = 1; }
  return 0;
}

/// @brief 関数呼出しの実引数並びを解析し，呼出しを出力する。
/// @param e 呼び出す関数の大域記号番号
/// @return 常に 0
/// @note 実引数は左から順に評価してデータスタックへ積む。gna が -1 の
///       ときは前方参照でまだ個数が判らないので個数検査を省く。
int ecallseq(int e) {
  int n;
  next();
  n = 0;
  if (tok != o_rp) {
    expr(); rv(); n = 1;
    while (tok == o_comma) { next(); expr(); rv(); n = n + 1; }
  }
  if (tok != o_rp) exit(1);
  next();
  if (gna[e] >= 0 && gna[e] != n) exit(5);
  ety = gty[e];
  elv = 0;
  return ecall(e);
}

/// @brief 識別子を解決する (ローカル -> 大域 -> 未知なら前方参照の関数呼出し)。
/// @return 常に 0
/// @note 未知の名前は「これから定義される関数の呼出し」としてのみ許す。
///       直後が '(' でなければ未定義識別子 (エラー 2)。仮登録した記号は
///       gdef = 0 のままなので，最後まで定義されなければ main が検出する。
int eident() {
  int e;
  e = lfind();
  if (e >= 0) {
    eladdr(loff[e]);
    if (larr[e]) { ety = lty[e] + 65536; elv = 0; }
    else { ety = lty[e]; elv = 1; }
    next();
    return 0;
  }
  e = gfind();
  if (e >= 0) {
    if (gkind[e] == 1) {
      next();
      if (tok != o_lp) exit(1);
      return ecallseq(e);
    }
    elit(gval[e]);
    if (garr[e]) { ety = gty[e] + 65536; elv = 0; }
    else { ety = gty[e]; elv = 1; }
    next();
    return 0;
  }
  next();
  if (tok != o_lp) exit(2);
  e = gnew();
  gkind[e] = 1; gty[e] = 1; gval[e] = 0; gdef[e] = 0; garr[e] = 0; gna[e] = -1;
  return ecallseq(e);
}

/// @brief 一次式 (リテラル・識別子・括弧) を解析する。
/// @return 常に 0
int eprim() {
  if (tok == t_num) { elit(tval); elv = 0; ety = 1; next(); return 0; }
  if (tok == t_str) return estr2();
  if (tok == o_lp) {
    next();
    expr();
    if (tok != o_rp) exit(1);
    next();
    return 0;
  }
  if (tok == t_id) return eident();
  exit(1);
  return 0;
}

/// @brief 後置演算 (添字 [] ・メンバ . ・アロー ->) を左から畳み込む。
/// @return 常に 0
/// @note a[i] は *(a + i) と同義に展開する。添字は指し先の大きさで
///       スケールし，結果は左辺値 (elv = 1) として返すので，
///       代入の左辺にも読み出しにも使える。
int epost() {
  int pt; int k;
  eprim();
  while (tok == o_lb || tok == o_dot || tok == o_arrow) {
    if (tok == o_lb) {
      rv();
      if ((ety >> 16) == 0) exit(5);
      pt = ety;
      next();
      expr(); rv();
      if (tok != o_rb) exit(1);
      next();
      escale(tsize(pt - 65536));
      ebin(0x00a585b3);
      ety = pt - 65536;
      elv = 1;
    } else if (tok == o_dot) {
      if (elv == 0) exit(5);
      if ((ety >> 16) != 0) exit(5);
      if (ety < 2) exit(5);
      emember(ety - 2);
    } else {
      rv();
      if ((ety >> 16) != 1) exit(5);
      k = ety & 65535;
      if (k < 2) exit(5);
      emember(k - 2);
    }
  }
  return 0;
}

/// @brief 単項演算 (- ! * &) を解析する。
/// @return 常に 0
/// @note * と & は elv の付け外しだけで済む。* は「値として得たアドレス」を
///       左辺値に変える操作 (elv = 1)，& は「左辺値のアドレス」をそのまま
///       値に変える操作 (elv = 0) であり，どちらも命令を生まない。
int euna() {
  if (tok == o_sub) {
    next(); euna(); rv();
    outw(0x0004a503); outw(0x40a00533); outw(0x00a4a023);
    ety = 1;
    return 0;
  }
  if (tok == o_not) {
    next(); euna(); rv();
    outw(0x0004a503); outw(0x00153513); outw(0x00a4a023);
    ety = 1;
    return 0;
  }
  if (tok == o_mul) {
    next(); euna(); rv();
    if ((ety >> 16) == 0) exit(5);
    ety = ety - 65536;
    elv = 1;
    return 0;
  }
  if (tok == o_amp) {
    next(); euna();
    if (elv == 0) exit(5);
    elv = 0;
    ety = ety + 65536;
    return 0;
  }
  return epost();
}

// 以降の二項演算子は優先順位ごとに 1 関数を割り当てた再帰下降で，
// 低い優先順位の関数が高い方を呼ぶ。いずれも「同じ優先順位が続く限り
// while で左から畳み込む」形なので，自然に左結合になる。

/// @brief 乗除算 (* / %) を解析する。
/// @return 常に 0
int emul() {
  int op;
  euna();
  while (tok == o_mul || tok == o_div || tok == o_mod) {
    op = tok;
    rv(); next(); euna(); rv();
    if (op == o_mul) ebin(0x02a585b3);
    else if (op == o_div) ebin(0x02a5c5b3);
    else ebin(0x02a5e5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief 加減算の型処理 (ポインタ演算のスケーリング)。
/// @param op 演算子 (o_add / o_sub)
/// @param lt 左辺の型
/// @return 常に 0
/// @note C と同じ規則を実装する:
///       ポインタ ± 整数 -> 整数側を要素サイズ倍し，型はポインタのまま
///       整数 + ポインタ -> 可換なので入れ替えて同様に扱う
///       ポインタ - ポインタ -> バイト差を要素サイズで割り，型は int
int edoadd(int op, int lt) {
  int rt;
  rt = ety;
  if (op == o_add) {
    if ((lt >> 16) != 0 && (rt >> 16) == 0) {
      escale(tsize(lt - 65536));
      ebin(0x00a585b3);
      ety = lt;
      return 0;
    }
    if ((lt >> 16) == 0 && (rt >> 16) != 0) {
      eswp();
      escale(tsize(rt - 65536));
      eswp();
      ebin(0x00a585b3);
      return 0;
    }
    ebin(0x00a585b3);
    ety = lt;
    return 0;
  }
  if ((lt >> 16) != 0 && (rt >> 16) == 0) {
    escale(tsize(lt - 65536));
    ebin(0x40a585b3);
    ety = lt;
    return 0;
  }
  if ((lt >> 16) != 0 && (rt >> 16) != 0) {
    ebin(0x40a585b3);
    ediv(tsize(lt - 65536));
    ety = 1;
    return 0;
  }
  ebin(0x40a585b3);
  ety = lt;
  return 0;
}

/// @brief 加減算 (+ -) を解析する。型処理は edoadd に委ねる。
/// @return 常に 0
int eadd() {
  int op; int lt;
  emul();
  while (tok == o_add || tok == o_sub) {
    op = tok;
    rv();
    lt = ety;
    next(); emul(); rv();
    edoadd(op, lt);
    elv = 0;
  }
  return 0;
}

/// @brief シフト (<< >>) を解析する。>> は論理右シフト。
/// @return 常に 0
int eshift() {
  int op;
  eadd();
  while (tok == o_shl || tok == o_shr) {
    op = tok;
    rv(); next(); eadd(); rv();
    if (op == o_shl) ebin(0x00a595b3);
    else ebin(0x00a5d5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief 大小比較 (< > <= >=) を解析する。結果は 1 / 0。
/// @return 常に 0
/// @note RV32I には slt しかないので，> は左右を入れ替え，
///       <= と >= は結果を xor 1 で反転して作る。
int erel() {
  int op;
  eshift();
  while (tok == o_lt || tok == o_gt || tok == o_le || tok == o_ge) {
    op = tok;
    rv(); next(); eshift(); rv();
    if (op == o_lt) ebin(0x00a5a5b3);
    else if (op == o_gt) ebin(0x00b525b3);
    else if (op == o_le) ebin2(0x00b525b3, 0x0015c593);
    else ebin2(0x00a5a5b3, 0x0015c593);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief 等値比較 (== !=) を解析する。結果は 1 / 0。
/// @return 常に 0
/// @note 差を取ってから，== は sltiu ..,1 (差が 0 なら 1)，
///       != は sltu x0,.. (差が 0 以外なら 1) で 1/0 に落とす。
int eeq() {
  int op;
  erel();
  while (tok == o_eq || tok == o_ne) {
    op = tok;
    rv(); next(); erel(); rv();
    if (op == o_eq) ebin2(0x40a585b3, 0x0015b593);
    else ebin2(0x40a585b3, 0x00b035b3);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief ビット AND (&) を解析する。
/// @return 常に 0
int eband() {
  eeq();
  while (tok == o_amp) {
    rv(); next(); eeq(); rv();
    ebin(0x00a5f5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief ビット XOR (^) を解析する。
/// @return 常に 0
int exor() {
  eband();
  while (tok == o_xor) {
    rv(); next(); eband(); rv();
    ebin(0x00a5c5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief ビット OR (|) を解析する。
/// @return 常に 0
int ebor() {
  exor();
  while (tok == o_or) {
    rv(); next(); exor(); rv();
    ebin(0x00a5e5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief 論理 AND (&&) を解析する。左辺が偽なら右辺を評価しない (短絡)。
/// @return 常に 0
/// @note 左辺が 0 なら右辺を飛ばして 0 を積み，そうでなければ右辺を
///       評価して 1/0 に正規化する。飛び先は前方なので，分岐を先に
///       出しておき，行き先が確定した時点で後埋めする。
int ecand() {
  int b; int j;
  ebor();
  while (tok == o_aa) {
    rv();
    epop();
    b = outp;
    outw(0x00050063);
    next(); ebor(); rv();
    epop();
    outw(0x00a03533);
    epush();
    j = outp;
    outw(0x0000006f);
    patw(benc(outp - b) | 0x00050063, b);
    outw(0xffc48493);
    outw(0x0004a023);
    patw(jenc(outp - j) | 0x6f, j);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief 論理 OR (||) を解析する。左辺が真なら右辺を評価しない (短絡)。
/// @return 常に 0
/// @note 構造は ecand と対称で，飛び先で積む定数が 1 になる。
int ecor() {
  int b; int j;
  ecand();
  while (tok == o_oo) {
    rv();
    epop();
    b = outp;
    outw(0x00051063);
    next(); ecand(); rv();
    epop();
    outw(0x00a03533);
    epush();
    j = outp;
    outw(0x0000006f);
    patw(benc(outp - b) | 0x00051063, b);
    outw(0x00100513);
    epush();
    patw(jenc(outp - j) | 0x6f, j);
    ety = 1; elv = 0;
  }
  return 0;
}

/// @brief 式を解析する (代入を含む最上位)。
/// @return 常に 0
/// @note 代入だけは右結合なので while ではなく自分自身を再帰呼出しする。
///       左辺は左辺値でなければならず (elv = 1)，そうでなければ型エラー (5)。
int expr() {
  int t;
  ecor();
  if (tok == o_asn) {
    if (elv == 0) exit(5);
    t = ety;
    elv = 0;
    next();
    expr();
    rv();
    estore(t);
    ety = t;
  }
  return 0;
}

// ---- 文 ----

/// @brief 文を 1 個解析してコードを出力する。
/// @return 常に 0
/// @note 前方への分岐は，行き先が決まる前に命令を出してしまい，
///       位置を覚えておいて後から書き戻す (backpatch)。while の
///       後方分岐は行き先が既知なのでその場で確定する。
///       式文の値は捨てる必要があるので，末尾でスタックを 1 つ縮める。
int stmt() {
  int b; int j; int s;
  if (tok == o_lc) {
    next();
    while (tok != o_rc) stmt();
    next();
    return 0;
  }
  if (tok == k_if) {
    next();
    if (tok != o_lp) exit(1);
    next();
    expr(); rv();
    if (tok != o_rp) exit(1);
    next();
    epop();
    b = outp;
    outw(0x00050063);
    stmt();
    if (tok == k_else) {
      next();
      j = outp;
      outw(0x0000006f);
      patw(benc(outp - b) | 0x00050063, b);
      stmt();
      patw(jenc(outp - j) | 0x6f, j);
    } else patw(benc(outp - b) | 0x00050063, b);
    return 0;
  }
  if (tok == k_while) {
    next();
    if (tok != o_lp) exit(1);
    next();
    s = outp;
    expr(); rv();
    if (tok != o_rp) exit(1);
    next();
    epop();
    b = outp;
    outw(0x00050063);
    stmt();
    outw(jenc(s - outp) | 0x6f);
    patw(benc(outp - b) | 0x00050063, b);
    return 0;
  }
  if (tok == k_return) {
    next();
    if (tok == o_semi) elit(0);
    else {
      expr(); rv();
      if (tok != o_semi) exit(1);
    }
    next();
    eepilog();
    return 0;
  }
  if (tok == o_semi) { next(); return 0; }
  expr();
  if (tok != o_semi) exit(1);
  next();
  outw(0x00448493);
  return 0;
}

// ---- 宣言 ----

/// @brief 構造体メンバを 1 個解析して登録する。
/// @param se 所属する構造体番号
/// @param t メンバの型
/// @return 常に 0
/// @note オフセットは宣言順に積む。char 配列だけは詰めて置き，
///       それ以外は 4 バイト境界へ揃える。ワード単位のロード/ストアが
///       境界を跨がないようにするためである。
int memb(int se, int t) {
  int m;
  if ((t >> 16) == 0 && (t & 65535) > 1) exit(5);
  if (tok != t_id) exit(1);
  if (mfind(se) >= 0) exit(4);
  if (mcnt > 2047) exit(6);
  m = mcnt;
  mcnt = mcnt + 1;
  msid[m] = se;
  copyn(mname + m * 16, tname);
  mty[m] = t;
  next();
  if (tok == o_lb) {
    next();
    if (tok != t_num) exit(1);
    marr[m] = 1;
    if (mty[m] == 0) {
      moff[m] = ssize[se];
      ssize[se] = ssize[se] + tval;
    } else {
      moff[m] = (ssize[se] + 3) & 0xfffffffc;
      ssize[se] = moff[m] + tval * 4;
    }
    next();
    if (tok != o_rb) exit(1);
    next();
  } else {
    marr[m] = 0;
    moff[m] = (ssize[se] + 3) & 0xfffffffc;
    ssize[se] = moff[m] + 4;
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

/// @brief 構造体定義 (struct 名 { ... };) を解析して登録する。
/// @return 常に 0
/// @note 先に空の構造体として登録してからメンバを読む。こうすると
///       メンバに自分自身へのポインタ (連結リストの next など) を書ける。
int structdef() {
  int se;
  copyn(tname, snam);
  if (sfind() >= 0) exit(4);
  if (scnt > 255) exit(6);
  se = scnt;
  scnt = scnt + 1;
  copyn(sname + se * 16, tname);
  ssize[se] = 0;
  next();
  while (tok != o_rc) memb(se, pstars(ptype()));
  next();
  if (tok != o_semi) exit(1);
  next();
  ssize[se] = (ssize[se] + 3) & 0xfffffffc;
  return 0;
}

/// @brief 仮引数を 1 個解析してローカル記号に登録する。
/// @return 常に 0
/// @note 引数はフレームの [8] から順に置く。[0] は戻り先，[4] は旧 x8。
int pparam() {
  int b; int i;
  b = pstars(ptype());
  if (tok != t_id) exit(1);
  if (lfind() >= 0) exit(4);
  i = lnew();
  lty[i] = b;
  loff[i] = 8 + cna * 4;
  larr[i] = 0;
  cna = cna + 1;
  next();
  return 0;
}

/// @brief ローカル変数宣言を 1 個解析して登録する。
/// @return 常に 0
/// @note 宣言は関数本体の先頭にまとめる仕様なので，走査の途中で
///       フレームサイズが後戻りすることがない。プロローグを本体より
///       先に出せるのはこの制約のおかげである。
int plocal() {
  int b; int i;
  b = pstars(ptype());
  if ((b >> 16) == 0 && (b & 65535) > 1) exit(5);
  if (tok != t_id) exit(1);
  if (lfind() >= 0) exit(4);
  i = lnew();
  lty[i] = b;
  loff[i] = cloff;
  next();
  if (tok == o_lb) {
    next();
    if (tok != t_num) exit(1);
    larr[i] = 1;
    if (lty[i] == 0) cloff = cloff + ((tval + 3) & 0xfffffffc);
    else cloff = cloff + tval * 4;
    next();
    if (tok != o_rb) exit(1);
    next();
  } else {
    larr[i] = 0;
    cloff = cloff + 4;
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

/// @brief 関数定義を解析し，プロローグ・本体・エピローグを出力する。
/// @return 常に 0
/// @note 既に記号がある場合は前方参照で仮登録されたものなので，
///       関数であること・未定義であることを確かめてから引き継ぎ，
///       溜まっていた未解決の呼出しをこの時点で解決する。
///       引数はデータスタックに積まれて来るので，プロローグで逆順に
///       取り出してフレームへ移す。以降は普通のローカル変数として扱える。
///       本体末尾には無条件に「return 0」相当を足す。return を通らずに
///       関数の終わりへ到達した場合の返却値を仕様どおり 0 にするため。
int funcdef() {
  int e; int i; int h;
  e = gfind();
  if (e >= 0) {
    if (gkind[e] != 1) exit(4);
    if (gdef[e]) exit(4);
  } else {
    e = gnew();
    gkind[e] = 1;
    gval[e] = 0;
    garr[e] = 0;
  }
  gty[e] = cty;
  h = gval[e];
  gval[e] = outp;
  gdef[e] = 1;
  patchcalls(outp, h);
  if (streq(gname + e * 16, "main")) { mainoff = outp; mainok = 1; }
  lcnt = 0;
  cna = 0;
  next();
  if (tok != o_rp) {
    pparam();
    while (tok == o_comma) { next(); pparam(); }
  }
  if (tok != o_rp) exit(1);
  next();
  gna[e] = cna;
  if (tok != o_lc) exit(1);
  next();
  cloff = 8 + cna * 4;
  while (tok == k_int || tok == k_char || tok == k_struct) plocal();
  if (cloff > 2040) exit(6);
  fsz = cloff;
  outw((((0 - fsz) & 4095) << 20) | 0x10113);
  outw(0x00112023);
  outw(0x00812223);
  outw(0x00010413);
  i = cna;
  while (i) {
    i = i - 1;
    epop();
    outw(swx8(8 + i * 4));
  }
  while (tok != o_rc) stmt();
  next();
  elit(0);
  eepilog();
  return 0;
}

/// @brief 型を読んだ後の宣言を処理する (関数定義か大域変数かをここで分ける)。
/// @param b 基底型
/// @return 常に 0
/// @note 名前の次が '(' なら関数，そうでなければ大域変数。
///       大域変数には bssp から順にアドレスを与える。実体はバイナリに
///       含めず，実行時の BSS 領域を指すだけである。
int dcont(int b) {
  int e;
  cty = pstars(b);
  if (tok != t_id) exit(1);
  next();
  if (tok == o_lp) return funcdef();
  if (gfind() >= 0) exit(4);
  e = gnew();
  gkind[e] = 0;
  gty[e] = cty;
  gval[e] = bssp;
  gdef[e] = 1;
  gna[e] = 0;
  if (tok == o_lb) {
    next();
    if (tok != t_num) exit(1);
    garr[e] = 1;
    if (cty == 0) bssp = bssp + ((tval + 3) & 0xfffffffc);
    else bssp = bssp + tval * 4;
    next();
    if (tok != o_rb) exit(1);
    next();
  } else {
    garr[e] = 0;
    bssp = bssp + ((tsize(cty) + 3) & 0xfffffffc);
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

/// @brief トップレベルの宣言を 1 個処理する。
/// @return 常に 0
/// @note "struct 名 {" なら定義，"struct 名 名前" なら既存の構造体型を
///       使った宣言。1 トークン先読みするために型名を snam へ退避する。
int topdecl() {
  int k;
  if (tok == k_struct) {
    next();
    if (tok != t_id) exit(1);
    copyn(snam, tname);
    next();
    if (tok == o_lc) return structdef();
    k = sfind2();
    if (k < 0) exit(2);
    return dcont(k + 2);
  }
  return dcont(ptype());
}

// ---- 組込み関数の登録 ----

/// @brief 組込み関数を定義済みの大域記号として登録する。
/// @param nm 名前
/// @param v ランタイム前置部の中のアドレス (出力オフセット)
/// @param na 引数個数
/// @return 常に 0
int biadd(char *nm, int v, int na) {
  int e; int i;
  i = 0;
  while (nm[i]) { tname[i] = nm[i]; i = i + 1; }
  while (i < 16) { tname[i] = 0; i = i + 1; }
  e = gnew();
  gkind[e] = 1;
  gty[e] = 1;
  gval[e] = v;
  gdef[e] = 1;
  garr[e] = 0;
  gna[e] = na;
  return 0;
}
/// @brief getc / putc / exit を登録する。
/// @return 常に 0
/// @note アドレスはランタイム前置部の中の固定位置で，普通の関数と同じく
///       jal で呼べる。前置部は必ず出力の先頭に置くので位置は不変である。
int bireg() {
  biadd("getc", 68, 0);
  biadd("putc", 96, 1);
  biadd("exit", 124, 1);
  return 0;
}

// ---- 駆動部 ----

/// @brief コンパイラ本体。標準入力からソースを読み，標準出力へバイナリを書く。
/// @return 常に 0 (異常時は exit で終了コードを返して停止する)
/// @note 段取り:
///       1. EOT (0x04) までソースを読み切る。UART には EOF がないため
///          明示的な終端文字を使う
///       2. ランタイム前置部 32 語を出力する。レジスタ初期化・main 呼出し・
///          getc/putc/exit を含む。main のアドレスはこの時点で未定なので
///          5 語目は 0 で埋め，全体を読み終えてから後埋めする
///       3. トップレベル宣言を順に処理する
///       4. main の存在と，前方参照のまま定義されなかった関数がないことを検査
///       5. 溜めたバイナリを 1 バイトずつ書き出す
int main() {
  int c; int i;
  init();
  pos = 0;
  c = getc();
  while (c != eot) {
    src[pos] = c;
    pos = pos + 1;
    c = getc();
  }
  src[pos] = c;
  pos = 0;
  outp = 0; gcnt = 0; scnt = 0; mcnt = 0; lcnt = 0; mainok = 0;
  bssp = 0x80100000;
  // ランタイム前置部 (32 語。語 4 = jal x1 main は後で patch)
  outw(0x87f004b7);
  outw(0x87800137);
  outw(0x100002b7);
  outw(0x00100337);
  outw(0);
  outw(0x0004a503);
  outw(0x00448493);
  outw(0x00050c63);
  outw(0x01051513);
  outw(0x000035b7);
  outw(0x33358593);
  outw(0x00b56533);
  outw(0x00c0006f);
  outw(0x00005537);
  outw(0x55550513);
  outw(0x00a32023);
  outw(0x0000006f);
  outw(0x0052c503);
  outw(0x00157513);
  outw(0xfe050ce3);
  outw(0x0002c503);
  outw(0xffc48493);
  outw(0x00a4a023);
  outw(0x00008067);
  outw(0x0052c583);
  outw(0x0205f593);
  outw(0xfe058ce3);
  outw(0x0004a503);
  outw(0x00a28023);
  outw(0x0004a023);
  outw(0x00008067);
  outw(0xf99ff06f);
  bireg();
  next();
  while (tok != t_eof) topdecl();
  if (mainok == 0) exit(3);
  patw(jenc(mainoff - 16) | 0xef, 16);
  i = 0;
  while (i < gcnt) {
    if (gkind[i] == 1 && gdef[i] == 0) exit(2);
    i = i + 1;
  }
  i = 0;
  while (i < outp) {
    putc(ob[i]);
    i = i + 1;
  }
  return 0;
}
