// stage006/scc: sc コンパイラの sc 言語による再記述 (セルフホスト)
// docs/stage006-scc.md。言語仕様は docs/stage005-sc.md 2 章。
// 構成・コード生成テンプレート・エラーコードは Stage 5 実装 (sc.sol) と同一。

// ---- 領域 (並行配列。生成コードの BSS 0x8010_0000 から確保される) ----
char src[262144];         // 入力ソース
char ob[524288];          // 生成バイナリ (バックパッチ対象)
char gname[32768];        // 大域記号: 名前 16B x 2048
int gkind[2048];          // 0 = 変数, 1 = 関数
int gty[2048];
int gval[2048];           // 変数: 絶対アドレス / 関数: コードオフセット or 未解決リスト先頭
int gdef[2048];
int garr[2048];
int gna[2048];            // 引数個数 (-1 = 未知)
int gcnt;
char lname[4096];         // ローカル記号: 名前 16B x 256
int lty[256];
int loff[256];
int larr[256];
int lcnt;
char sname[4096];         // 構造体: 名前 16B x 256
int ssize[256];
int scnt;
char mname[32768];        // メンバ: 名前 16B x 2048
int msid[2048];
int mty[2048];
int moff[2048];
int marr[2048];
int mcnt;
char tname[16];           // 現トークンの識別子
char snam[16];            // struct 名の退避
char sbuf[256];           // 文字列リテラル
int slen;
int pos;
int tok;
int tval;
int outp;
int bssp;
int ety;                  // 式の型 = (ポインタ深さ << 16) | 基底 (0=char,1=int,2+k=構造体k)
int elv;                  // 1 = 左辺値 (アドレスが積まれロード遅延)
int fsz;
int cloff;
int cna;
int cty;
int mainok;
int mainoff;
int *wp;

// ---- トークン種別 (定数構文がないため init で設定) ----
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
int streq(char *a, char *b) {
  int i;
  i = 0;
  while (a[i] == b[i]) {
    if (a[i] == 0) return 1;
    i = i + 1;
  }
  return 0;
}
int copyn(char *d, char *s) {
  int i;
  i = 0;
  while (i < 16) { d[i] = s[i]; i = i + 1; }
  return 0;
}

// ---- 字句解析 ----
int getch() { return src[pos]; }
int adv() { pos = pos + 1; return 0; }

int isws(int c) { return c == 32 || c == 9 || c == 13 || c == 10; }
int isdig(int c) { return c >= '0' && c <= '9'; }
int isidh(int c) { return (c >= 'a' && c <= 'z') || c == '_'; }
int isidc(int c) { return isdig(c) || isidh(c); }
int ishex(int c) { return isdig(c) || (c >= 'a' && c <= 'f'); }
int hexv(int c) {
  if (c > '9') return c - 87;
  return c - '0';
}
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
int outw(int w) {
  wp = ob + outp;
  *wp = w;
  outp = outp + 4;
  return 0;
}
int outbyte(int b) {
  ob[outp] = b;
  outp = outp + 1;
  return 0;
}
int patw(int w, int off) {
  wp = ob + off;
  *wp = w;
  return 0;
}
int getw(int off) {
  wp = ob + off;
  return *wp;
}

// ---- J/B-type 即値の合成 ----
int jenc(int rel) {
  return (((rel >> 20) & 1) << 31) | (((rel >> 1) & 1023) << 21)
       | (((rel >> 11) & 1) << 20) | (((rel >> 12) & 255) << 12);
}
int benc(int rel) {
  return (((rel >> 12) & 1) << 31) | (((rel >> 5) & 63) << 25)
       | (((rel >> 1) & 15) << 8) | (((rel >> 11) & 1) << 7);
}

// ---- コード生成テンプレート (Stage 5 実装と同一) ----
int epop() { outw(0x0004a503); outw(0x00448493); return 0; }
int epush() { outw(0xffc48493); outw(0x00a4a023); return 0; }
int elit(int v) {
  outw(((v + 2048) & 0xfffff000) | 0x537);
  outw((v << 20) | 0x50513);
  return epush();
}
int eswp() { outw(0x0004a503); outw(0x0044a583); outw(0x00a4a223); outw(0x00b4a023); return 0; }
int ebin(int w) {
  outw(0x0004a503); outw(0x0044a583);
  outw(w);
  outw(0x00448493); outw(0x00b4a023);
  return 0;
}
int ebin2(int w1, int w2) {
  outw(0x0004a503); outw(0x0044a583);
  outw(w1); outw(w2);
  outw(0x00448493); outw(0x00b4a023);
  return 0;
}
int tsize(int t) {
  if ((t >> 16) != 0) return 4;
  if (t == 0) return 1;
  if (t == 1) return 4;
  return ssize[t - 2];
}
int bytesz(int t) {
  if ((t >> 16) != 0) return 4;
  if (t == 0) return 1;
  return 4;
}
int eload(int t) {
  outw(0x0004a503);
  if (bytesz(t) == 1) outw(0x00054503); else outw(0x00052503);
  outw(0x00a4a023);
  return 0;
}
int estore(int t) {
  outw(0x0004a503);
  outw(0x0044a583);
  if (bytesz(t) == 1) outw(0x00a58023); else outw(0x00a5a023);
  outw(0x00a4a223);
  outw(0x00448493);
  return 0;
}
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
int eladdr(int off) {
  outw((off << 20) | 0x40513);
  return epush();
}
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
int swx8(int off) {
  return ((off >> 5) << 25) | ((off & 31) << 7) | 0x00a42023;
}
int eepilog() {
  outw(0x00012083);
  outw(0x00412403);
  outw((fsz << 20) | 0x10113);
  outw(0x00008067);
  return 0;
}

// ---- 記号表 (未発見は -1) ----
int gfind() {
  int i;
  i = 0;
  while (i < gcnt) {
    if (streq(gname + i * 16, tname)) return i;
    i = i + 1;
  }
  return -1;
}
int gnew() {
  int e;
  if (gcnt > 2047) exit(6);
  e = gcnt;
  gcnt = gcnt + 1;
  copyn(gname + e * 16, tname);
  return e;
}
int lfind() {
  int i;
  i = 0;
  while (i < lcnt) {
    if (streq(lname + i * 16, tname)) return i;
    i = i + 1;
  }
  return -1;
}
int lnew() {
  int e;
  if (lcnt > 255) exit(6);
  e = lcnt;
  lcnt = lcnt + 1;
  copyn(lname + e * 16, tname);
  return e;
}
int sfind() {
  int i;
  i = 0;
  while (i < scnt) {
    if (streq(sname + i * 16, tname)) return i;
    i = i + 1;
  }
  return -1;
}
int sfind2() {
  int i;
  i = 0;
  while (i < scnt) {
    if (streq(sname + i * 16, snam)) return i;
    i = i + 1;
  }
  return -1;
}
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
int patchcalls(int d, int h) {
  int nx;
  while (h) {
    nx = getw(h);
    patw(jenc(d - h) | 0xef, h);
    h = nx;
  }
  return 0;
}
int ecall(int e) {
  int h;
  if (gdef[e]) { outw(jenc(gval[e] - outp) | 0xef); return 0; }
  h = gval[e];
  gval[e] = outp;
  outw(h);
  return 0;
}

// ---- 型の解析 ----
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
int pstars(int b) {
  while (tok == o_mul) { b = b + 65536; next(); }
  return b;
}

// ---- 式 ----
int rv() {
  if (elv) { eload(ety); elv = 0; }
  return 0;
}

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

int eband() {
  eeq();
  while (tok == o_amp) {
    rv(); next(); eeq(); rv();
    ebin(0x00a5f5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

int exor() {
  eband();
  while (tok == o_xor) {
    rv(); next(); eband(); rv();
    ebin(0x00a5c5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

int ebor() {
  exor();
  while (tok == o_or) {
    rv(); next(); exor(); rv();
    ebin(0x00a5e5b3);
    ety = 1; elv = 0;
  }
  return 0;
}

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
int bireg() {
  biadd("getc", 68, 0);
  biadd("putc", 96, 1);
  biadd("exit", 124, 1);
  return 0;
}

// ---- 駆動部 ----
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
