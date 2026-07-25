// stage007/occ: 現代化した sc コンパイラ (IR + 最適化 + レジスタ割付)
// docs/stage007-occ.md。言語仕様は docs/stage005-sc.md 2 章 (変更なし)。
// 字句・構文解析・記号表は Stage 6 実装 (scc.sc) を継承し，
// コード生成を IR 構築 -> fold -> dce -> 線形走査割付け -> 出力に置き換える。

// ---- 領域 ----
char src[262144];
char ob[524288];
char gname[32768];
int gkind[2048];
int gty[2048];
int gval[2048];
int gdef[2048];
int garr[2048];
int gna[2048];
int gcnt;
char lname[4096];
int lty[256];
int loff[256];
int larr[256];
int lcnt;
char sname[4096];
int ssize[256];
int scnt;
char mname[32768];
int msid[2048];
int mty[2048];
int moff[2048];
int marr[2048];
int mcnt;
char tname[16];
char snam[16];
char sbuf[256];
int slen;
int pos;
int tok;
int tval;
int outp;
int bssp;
int ety;
int elv;
int cloff;
int cna;
int cty;
int mainok;
int mainoff;
int *wp;

// ---- IR (関数単位) ----
int iop[8192];            // 命令種別
int ia[8192];             // 第 1 オペランド
int ib[8192];             // 第 2 オペランド
int icnt;
int lastu[8192];          // 値の最終使用位置 (-1 = 未使用)
int vreg[8192];           // 割付け: >= 0 レジスタ番号 / -1 未割付 / -2-n スピルスロット n
int live[8192];           // dce: 1 = 生存
int labpos[1024];         // ラベル -> 出力オフセット (-1 = 未確定)
int labcnt;
int lfix[2048];           // 分岐の後埋め: 出力位置
int lflab[2048];          //               対象ラベル
int lfixn;
char spool[8192];         // 文字列プール (関数単位)
int spcnt;
int spfix[256];           // GSTR の後埋め: 出力位置 (lui の位置)
int spofs[256];           //               プール内オフセット
int spfn;
int hcnt;                 // 隠しスロット (&& / || の結果) の数
int nspill;               // スピルスロット数
int rheld[32];            // 割付け中: レジスタ -> 値 (-1 = 空き)
int rused[32];            // この関数で使ったレジスタ (退避対象)
int fnf;                  // フレーム総サイズ (プロローグ/エピローグ用)
int spbase;               // スピル領域の先頭オフセット
int svbase;               // レジスタ退避領域の先頭オフセット

// ---- トークン種別・IR 命令種別 (init で設定) ----
int eot;
int t_eof; int t_num; int t_str; int t_id;
int k_int; int k_char; int k_struct; int k_if; int k_else; int k_while; int k_return;
int o_asn; int o_lt; int o_gt; int o_add; int o_sub; int o_mul; int o_div; int o_mod;
int o_amp; int o_or; int o_xor; int o_not; int o_lp; int o_rp; int o_lb; int o_rb;
int o_lc; int o_rc; int o_semi; int o_comma; int o_dot;
int o_eq; int o_ne; int o_le; int o_ge; int o_shl; int o_shr; int o_aa; int o_oo; int o_arrow;
int c_const; int c_laddr; int c_gaddr; int c_gstr;
int c_loadw; int c_loadb; int c_stw; int c_stb;
int c_neg; int c_not; int c_arg; int c_call; int c_ret;
int c_lab; int c_jmp; int c_bz; int c_bnz; int c_bin;
int b_add; int b_sub; int b_mul; int b_div; int b_rem;
int b_and; int b_or; int b_xor; int b_sll; int b_srl;
int b_slt; int b_sgt; int b_sle; int b_sge; int b_seq; int b_sne;

int init() {
  eot = 4;
  t_eof = 0; t_num = 1; t_str = 2; t_id = 3;
  k_int = 10; k_char = 11; k_struct = 12; k_if = 13; k_else = 14; k_while = 15; k_return = 16;
  o_asn = 30; o_lt = 31; o_gt = 32; o_add = 33; o_sub = 34; o_mul = 35; o_div = 36; o_mod = 37;
  o_amp = 38; o_or = 39; o_xor = 40; o_not = 41; o_lp = 42; o_rp = 43; o_lb = 44; o_rb = 45;
  o_lc = 46; o_rc = 47; o_semi = 48; o_comma = 49; o_dot = 50;
  o_eq = 51; o_ne = 52; o_le = 53; o_ge = 54; o_shl = 55; o_shr = 56; o_aa = 57; o_oo = 58; o_arrow = 59;
  c_const = 1; c_laddr = 2; c_gaddr = 3; c_gstr = 4;
  c_loadw = 5; c_loadb = 6; c_stw = 7; c_stb = 8;
  c_neg = 9; c_not = 10; c_arg = 11; c_call = 12; c_ret = 13;
  c_lab = 14; c_jmp = 15; c_bz = 16; c_bnz = 17; c_bin = 18;
  b_add = 0; b_sub = 1; b_mul = 2; b_div = 3; b_rem = 4;
  b_and = 5; b_or = 6; b_xor = 7; b_sll = 8; b_srl = 9;
  b_slt = 10; b_sgt = 11; b_sle = 12; b_sge = 13; b_seq = 14; b_sne = 15;
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

// ---- 字句解析 (scc と同一) ----
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

// ---- 命令語の組立て ----
int jenc(int rel) {
  return (((rel >> 20) & 1) << 31) | (((rel >> 1) & 1023) << 21)
       | (((rel >> 11) & 1) << 20) | (((rel >> 12) & 255) << 12);
}
int benc(int rel) {
  return (((rel >> 12) & 1) << 31) | (((rel >> 5) & 63) << 25)
       | (((rel >> 1) & 15) << 8) | (((rel >> 11) & 1) << 7);
}
int rw3(int base, int rd, int rs1, int rs2) {
  return base | (rd << 7) | (rs1 << 15) | (rs2 << 20);
}
int iw3(int base, int rd, int rs1, int imm) {
  return base | (rd << 7) | (rs1 << 15) | (imm << 20);
}
int sw3(int base, int rs1, int rs2, int imm) {
  return base | (((imm >> 5) & 127) << 25) | ((imm & 31) << 7) | (rs1 << 15) | (rs2 << 20);
}
int liw(int rd, int v) {              // li rd, v (2 語)
  outw((((v + 2048) & 0xfffff000) | 0x37) | (rd << 7));
  outw(iw3(0x13, rd, rd, v & 4095));
  return 0;
}

// ---- 記号表 (scc と同一) ----
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

int patchcalls(int d, int h) {
  int nx;
  while (h) {
    nx = getw(h);
    patw(jenc(d - h) | 0xef, h);
    h = nx;
  }
  return 0;
}

// ---- 型の解析 (scc と同一) ----
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

// ---- IR 構築 ----
int emit(int op, int a, int b) {
  if (icnt > 8191) exit(6);
  iop[icnt] = op;
  ia[icnt] = a;
  ib[icnt] = b;
  icnt = icnt + 1;
  return icnt - 1;
}
int newlab() {
  if (labcnt > 1023) exit(6);
  labcnt = labcnt + 1;
  return labcnt - 1;
}
int hslot() {                        // 隠しスロット (フレームオフセットを返す)
  hcnt = hcnt + 1;
  return cloff + (hcnt - 1) * 4;
}

// ---- 式 (値 id を返す) ----
int rv(int v) {
  if (elv) {
    if (bytesz(ety) == 1) v = emit(c_loadb, v, 0);
    else v = emit(c_loadw, v, 0);
    elv = 0;
  }
  return v;
}

int estr2() {
  int p; int a; int i;
  p = (slen + 4) & 0xfffffffc;
  if (spcnt + p > 8191) exit(6);
  a = spcnt;
  i = 0;
  while (i < p) { spool[spcnt] = sbuf[i]; spcnt = spcnt + 1; i = i + 1; }
  ety = 65536;
  elv = 0;
  next();
  return emit(c_gstr, a, 0);
}

int emember(int k, int v) {
  int m; int c;
  next();
  if (tok != t_id) exit(1);
  m = mfind(k);
  if (m < 0) exit(5);
  next();
  if (moff[m] != 0) {
    c = emit(c_const, moff[m], 0);
    v = emit(c_bin + b_add, v, c);
  }
  if (marr[m]) { ety = mty[m] + 65536; elv = 0; }
  else { ety = mty[m]; elv = 1; }
  return v;
}

int ecallseq(int e) {
  int n; int v;
  next();
  n = 0;
  if (tok != o_rp) {
    v = rv(expr());
    emit(c_arg, v, 0);
    n = 1;
    while (tok == o_comma) {
      next();
      v = rv(expr());
      emit(c_arg, v, 0);
      n = n + 1;
    }
  }
  if (tok != o_rp) exit(1);
  next();
  if (gna[e] >= 0 && gna[e] != n) exit(5);
  ety = gty[e];
  elv = 0;
  return emit(c_call, e, n);
}

int eident() {
  int e;
  e = lfind();
  if (e >= 0) {
    if (larr[e]) { ety = lty[e] + 65536; elv = 0; }
    else { ety = lty[e]; elv = 1; }
    next();
    return emit(c_laddr, loff[e], 0);
  }
  e = gfind();
  if (e >= 0) {
    if (gkind[e] == 1) {
      next();
      if (tok != o_lp) exit(1);
      return ecallseq(e);
    }
    if (garr[e]) { ety = gty[e] + 65536; elv = 0; }
    else { ety = gty[e]; elv = 1; }
    next();
    return emit(c_gaddr, gval[e], 0);
  }
  next();
  if (tok != o_lp) exit(2);
  e = gnew();
  gkind[e] = 1; gty[e] = 1; gval[e] = 0; gdef[e] = 0; garr[e] = 0; gna[e] = -1;
  return ecallseq(e);
}

int eprim() {
  int v;
  if (tok == t_num) {
    v = emit(c_const, tval, 0);
    elv = 0; ety = 1;
    next();
    return v;
  }
  if (tok == t_str) return estr2();
  if (tok == o_lp) {
    next();
    v = expr();
    if (tok != o_rp) exit(1);
    next();
    return v;
  }
  if (tok == t_id) return eident();
  exit(1);
  return 0;
}

int epost() {
  int v; int i; int pt; int k; int sz; int c;
  v = eprim();
  while (tok == o_lb || tok == o_dot || tok == o_arrow) {
    if (tok == o_lb) {
      v = rv(v);
      if ((ety >> 16) == 0) exit(5);
      pt = ety;
      next();
      i = rv(expr());
      if (tok != o_rb) exit(1);
      next();
      sz = tsize(pt - 65536);
      if (sz != 1) {
        c = emit(c_const, sz, 0);
        i = emit(c_bin + b_mul, i, c);
      }
      v = emit(c_bin + b_add, v, i);
      ety = pt - 65536;
      elv = 1;
    } else if (tok == o_dot) {
      if (elv == 0) exit(5);
      if ((ety >> 16) != 0) exit(5);
      if (ety < 2) exit(5);
      v = emember(ety - 2, v);
    } else {
      v = rv(v);
      if ((ety >> 16) != 1) exit(5);
      k = ety & 65535;
      if (k < 2) exit(5);
      v = emember(k - 2, v);
    }
  }
  return v;
}

int euna() {
  int v;
  if (tok == o_sub) {
    next();
    v = rv(euna());
    ety = 1;
    return emit(c_neg, v, 0);
  }
  if (tok == o_not) {
    next();
    v = rv(euna());
    ety = 1;
    return emit(c_not, v, 0);
  }
  if (tok == o_mul) {
    next();
    v = rv(euna());
    if ((ety >> 16) == 0) exit(5);
    ety = ety - 65536;
    elv = 1;
    return v;
  }
  if (tok == o_amp) {
    next();
    v = euna();
    if (elv == 0) exit(5);
    elv = 0;
    ety = ety + 65536;
    return v;
  }
  return epost();
}

int emul() {
  int v; int r; int op;
  v = euna();
  while (tok == o_mul || tok == o_div || tok == o_mod) {
    op = tok;
    v = rv(v);
    next();
    r = rv(euna());
    if (op == o_mul) v = emit(c_bin + b_mul, v, r);
    else if (op == o_div) v = emit(c_bin + b_div, v, r);
    else v = emit(c_bin + b_rem, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int escale2(int v, int sz) {          // v * sz (ポインタ演算)
  int c;
  if (sz == 1) return v;
  c = emit(c_const, sz, 0);
  return emit(c_bin + b_mul, v, c);
}

int eadd() {
  int v; int r; int op; int lt;
  v = emul();
  while (tok == o_add || tok == o_sub) {
    op = tok;
    v = rv(v);
    lt = ety;
    next();
    r = rv(emul());
    if (op == o_add) {
      if ((lt >> 16) != 0 && (ety >> 16) == 0) {
        r = escale2(r, tsize(lt - 65536));
        v = emit(c_bin + b_add, v, r);
        ety = lt;
      } else if ((lt >> 16) == 0 && (ety >> 16) != 0) {
        v = escale2(v, tsize(ety - 65536));
        v = emit(c_bin + b_add, v, r);
      } else {
        v = emit(c_bin + b_add, v, r);
        ety = lt;
      }
    } else {
      if ((lt >> 16) != 0 && (ety >> 16) == 0) {
        r = escale2(r, tsize(lt - 65536));
        v = emit(c_bin + b_sub, v, r);
        ety = lt;
      } else if ((lt >> 16) != 0 && (ety >> 16) != 0) {
        v = emit(c_bin + b_sub, v, r);
        r = emit(c_const, tsize(lt - 65536), 0);
        if (tsize(lt - 65536) != 1) v = emit(c_bin + b_div, v, r);
        ety = 1;
      } else {
        v = emit(c_bin + b_sub, v, r);
        ety = lt;
      }
    }
    elv = 0;
  }
  return v;
}

int eshift() {
  int v; int r; int op;
  v = eadd();
  while (tok == o_shl || tok == o_shr) {
    op = tok;
    v = rv(v);
    next();
    r = rv(eadd());
    if (op == o_shl) v = emit(c_bin + b_sll, v, r);
    else v = emit(c_bin + b_srl, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int erel() {
  int v; int r; int op;
  v = eshift();
  while (tok == o_lt || tok == o_gt || tok == o_le || tok == o_ge) {
    op = tok;
    v = rv(v);
    next();
    r = rv(eshift());
    if (op == o_lt) v = emit(c_bin + b_slt, v, r);
    else if (op == o_gt) v = emit(c_bin + b_sgt, v, r);
    else if (op == o_le) v = emit(c_bin + b_sle, v, r);
    else v = emit(c_bin + b_sge, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int eeq() {
  int v; int r; int op;
  v = erel();
  while (tok == o_eq || tok == o_ne) {
    op = tok;
    v = rv(v);
    next();
    r = rv(erel());
    if (op == o_eq) v = emit(c_bin + b_seq, v, r);
    else v = emit(c_bin + b_sne, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int eband() {
  int v; int r;
  v = eeq();
  while (tok == o_amp) {
    v = rv(v);
    next();
    r = rv(eeq());
    v = emit(c_bin + b_and, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int exor() {
  int v; int r;
  v = eband();
  while (tok == o_xor) {
    v = rv(v);
    next();
    r = rv(eband());
    v = emit(c_bin + b_xor, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int ebor() {
  int v; int r;
  v = exor();
  while (tok == o_or) {
    v = rv(v);
    next();
    r = rv(exor());
    v = emit(c_bin + b_or, v, r);
    ety = 1; elv = 0;
  }
  return v;
}

int ecand() {
  int v; int r; int off; int l1; int l2; int t;
  v = ebor();
  while (tok == o_aa) {
    v = rv(v);
    off = hslot();
    l1 = newlab();
    l2 = newlab();
    emit(c_bz, v, l1);
    next();
    r = rv(ebor());
    t = emit(c_not, r, 0);
    t = emit(c_not, t, 0);
    emit(c_stw, emit(c_laddr, off, 0), t);
    emit(c_jmp, l2, 0);
    emit(c_lab, l1, 0);
    emit(c_stw, emit(c_laddr, off, 0), emit(c_const, 0, 0));
    emit(c_lab, l2, 0);
    v = emit(c_loadw, emit(c_laddr, off, 0), 0);
    ety = 1; elv = 0;
  }
  return v;
}

int ecor() {
  int v; int r; int off; int l1; int l2; int t;
  v = ecand();
  while (tok == o_oo) {
    v = rv(v);
    off = hslot();
    l1 = newlab();
    l2 = newlab();
    emit(c_bnz, v, l1);
    next();
    r = rv(ecand());
    t = emit(c_not, r, 0);
    t = emit(c_not, t, 0);
    emit(c_stw, emit(c_laddr, off, 0), t);
    emit(c_jmp, l2, 0);
    emit(c_lab, l1, 0);
    emit(c_stw, emit(c_laddr, off, 0), emit(c_const, 1, 0));
    emit(c_lab, l2, 0);
    v = emit(c_loadw, emit(c_laddr, off, 0), 0);
    ety = 1; elv = 0;
  }
  return v;
}

int expr() {
  int v; int r; int t;
  v = ecor();
  if (tok == o_asn) {
    if (elv == 0) exit(5);
    t = ety;
    elv = 0;
    next();
    r = rv(expr());
    if (bytesz(t) == 1) emit(c_stb, v, r);
    else emit(c_stw, v, r);
    ety = t;
    return r;
  }
  return v;
}

// ---- 文 ----
int stmt() {
  int c; int l1; int l2; int l0;
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
    c = rv(expr());
    if (tok != o_rp) exit(1);
    next();
    l1 = newlab();
    emit(c_bz, c, l1);
    stmt();
    if (tok == k_else) {
      next();
      l2 = newlab();
      emit(c_jmp, l2, 0);
      emit(c_lab, l1, 0);
      stmt();
      emit(c_lab, l2, 0);
    } else emit(c_lab, l1, 0);
    return 0;
  }
  if (tok == k_while) {
    next();
    if (tok != o_lp) exit(1);
    next();
    l0 = newlab();
    l1 = newlab();
    emit(c_lab, l0, 0);
    c = rv(expr());
    if (tok != o_rp) exit(1);
    next();
    emit(c_bz, c, l1);
    stmt();
    emit(c_jmp, l0, 0);
    emit(c_lab, l1, 0);
    return 0;
  }
  if (tok == k_return) {
    next();
    if (tok == o_semi) emit(c_ret, emit(c_const, 0, 0), 0);
    else {
      c = rv(expr());
      if (tok != o_semi) exit(1);
      emit(c_ret, c, 0);
    }
    next();
    return 0;
  }
  if (tok == o_semi) { next(); return 0; }
  expr();
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

// ---- 最適化パス ----
int isbin(int i) { return iop[i] >= c_bin; }
int ispure(int i) {
  if (isbin(i)) return 1;
  if (iop[i] == c_const || iop[i] == c_laddr || iop[i] == c_gaddr || iop[i] == c_gstr) return 1;
  if (iop[i] == c_loadw || iop[i] == c_loadb) return 1;
  if (iop[i] == c_neg || iop[i] == c_not) return 1;
  return 0;
}

int foldbin(int k, int x, int y) {
  if (k == b_add) return x + y;
  if (k == b_sub) return x - y;
  if (k == b_mul) return x * y;
  if (k == b_div) return x / y;
  if (k == b_rem) return x % y;
  if (k == b_and) return x & y;
  if (k == b_or) return x | y;
  if (k == b_xor) return x ^ y;
  if (k == b_sll) return x << y;
  if (k == b_srl) return x >> y;
  if (k == b_slt) return x < y;
  if (k == b_sgt) return x > y;
  if (k == b_sle) return x <= y;
  if (k == b_sge) return x >= y;
  if (k == b_seq) return x == y;
  return x != y;
}

int foldins(int i) {
  int k;
  if (isbin(i)) {
    if (iop[ia[i]] == c_const && iop[ib[i]] == c_const) {
      k = iop[i] - c_bin;
      if (!((k == b_div || k == b_rem) && ia[ib[i]] == 0)) {
        ia[i] = foldbin(k, ia[ia[i]], ia[ib[i]]);
        iop[i] = c_const;
        ib[i] = 0;
      }
    }
    return 0;
  }
  if (iop[i] == c_neg) {
    if (iop[ia[i]] == c_const) { ia[i] = 0 - ia[ia[i]]; iop[i] = c_const; }
    return 0;
  }
  if (iop[i] == c_not) {
    if (iop[ia[i]] == c_const) { ia[i] = ia[ia[i]] == 0; iop[i] = c_const; }
    return 0;
  }
  if (iop[i] == c_bz) {
    if (iop[ia[i]] == c_const) {
      if (ia[ia[i]] == 0) { iop[i] = c_jmp; ia[i] = ib[i]; ib[i] = 0; }
      else { iop[i] = c_const; ia[i] = 0; ib[i] = 0; }
    }
    return 0;
  }
  if (iop[i] == c_bnz) {
    if (iop[ia[i]] == c_const) {
      if (ia[ia[i]] != 0) { iop[i] = c_jmp; ia[i] = ib[i]; ib[i] = 0; }
      else { iop[i] = c_const; ia[i] = 0; ib[i] = 0; }
    }
    return 0;
  }
  return 0;
}

int fold() {
  int i;
  i = 0;
  while (i < icnt) {
    foldins(i);
    i = i + 1;
  }
  return 0;
}

int markv(int v) {
  if (live[v]) return 0;
  live[v] = 1;
  if (isbin(v)) { markv(ia[v]); markv(ib[v]); return 0; }
  if (iop[v] == c_loadw || iop[v] == c_loadb || iop[v] == c_neg || iop[v] == c_not) {
    markv(ia[v]);
    return 0;
  }
  return 0;
}

int dce() {
  int i;
  i = 0;
  while (i < icnt) { live[i] = 0; i = i + 1; }
  i = 0;
  while (i < icnt) {
    if (!ispure(i)) {
      live[i] = 1;
      if (iop[i] == c_stw || iop[i] == c_stb) { markv(ia[i]); markv(ib[i]); }
      else if (iop[i] == c_arg || iop[i] == c_ret) markv(ia[i]);
      else if (iop[i] == c_bz || iop[i] == c_bnz) markv(ia[i]);
    }
    i = i + 1;
  }
  return 0;
}

// ---- レジスタ割付け (線形走査。割付対象 x13..x27，スピルは新規値をスロットへ) ----
int usemark(int v, int i) {
  if (lastu[v] < i) lastu[v] = i;
  return 0;
}
int regalloc() {
  int i; int r; int f;
  i = 0;
  while (i < icnt) { lastu[i] = -1; vreg[i] = -1; i = i + 1; }
  i = 0;
  while (i < icnt) {
    if (live[i]) {
      if (isbin(i)) { usemark(ia[i], i); usemark(ib[i], i); }
      else if (iop[i] == c_loadw || iop[i] == c_loadb || iop[i] == c_neg || iop[i] == c_not) usemark(ia[i], i);
      else if (iop[i] == c_stw || iop[i] == c_stb) { usemark(ia[i], i); usemark(ib[i], i); }
      else if (iop[i] == c_arg || iop[i] == c_ret) usemark(ia[i], i);
      else if (iop[i] == c_bz || iop[i] == c_bnz) usemark(ia[i], i);
    }
    i = i + 1;
  }
  r = 13;
  while (r < 28) { rheld[r] = -1; rused[r] = 0; r = r + 1; }
  nspill = 0;
  i = 0;
  while (i < icnt) {
    // この位置が最終使用の値を解放する
    r = 13;
    while (r < 28) {
      if (rheld[r] >= 0) {
        if (lastu[rheld[r]] <= i) rheld[r] = -1;
      }
      r = r + 1;
    }
    // 値を定義する命令に割り付ける
    if (live[i] && lastu[i] > i) {
      if (ispure(i) || iop[i] == c_call) {
        f = -1;
        r = 13;
        while (r < 28) {
          if (f < 0 && rheld[r] < 0) f = r;
          r = r + 1;
        }
        if (f >= 0) {
          vreg[i] = f;
          rheld[f] = i;
          rused[f] = 1;
        } else {
          vreg[i] = -2 - nspill;
          nspill = nspill + 1;
        }
      }
    }
    i = i + 1;
  }
  return 0;
}

// ---- コード出力 ----
int oreg(int v, int sc2) {           // 値 v の読出しレジスタ (スピルなら sc2 へロード)
  if (vreg[v] >= 0) return vreg[v];
  outw(iw3(0x2003, sc2, 8, spbase + (0 - 2 - vreg[v]) * 4));
  return sc2;
}
int dreg(int v) {                    // 値 v の書込みレジスタ (スピルなら x10)
  if (vreg[v] >= 0) return vreg[v];
  return 10;
}
int dstore(int v) {                  // スピル値の書戻し
  if (vreg[v] >= 0) return 0;
  if (vreg[v] == -1) return 0;
  outw(sw3(0x2023, 8, 10, spbase + (0 - 2 - vreg[v]) * 4));
  return 0;
}
int lrefj(int l) {                   // ラベルへのジャンプ (前方は後埋め。jal で距離制限なし)
  if (labpos[l] >= 0) outw(0x6f | jenc(labpos[l] - outp));
  else {
    if (lfixn > 2047) exit(6);
    lfix[lfixn] = outp;
    lflab[lfixn] = l;
    lfixn = lfixn + 1;
    outw(0x6f);
  }
  return 0;
}

int binbase(int k) {
  if (k == b_add) return 0x33;
  if (k == b_sub) return 0x40000033;
  if (k == b_mul) return 0x02000033;
  if (k == b_div) return 0x02004033;
  if (k == b_rem) return 0x02006033;
  if (k == b_and) return 0x7033;
  if (k == b_or) return 0x6033;
  if (k == b_xor) return 0x4033;
  if (k == b_sll) return 0x1033;
  if (k == b_srl) return 0x5033;
  return 0x2033;                     // slt (比較系の基本)
}

int emitins(int i) {
  int k; int d; int a; int b;
  k = iop[i];
  if (k == c_const) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    d = dreg(i);
    if (ia[i] >= (0 - 2048) && ia[i] < 2048) outw(iw3(0x13, d, 0, ia[i] & 4095));
    else liw(d, ia[i]);
    return dstore(i);
  }
  if (k == c_laddr) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    d = dreg(i);
    outw(iw3(0x13, d, 8, ia[i]));
    return dstore(i);
  }
  if (k == c_gaddr) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    d = dreg(i);
    liw(d, ia[i]);
    return dstore(i);
  }
  if (k == c_gstr) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    d = dreg(i);
    if (spfn > 255) exit(6);
    spfix[spfn] = outp;
    spofs[spfn] = ia[i];
    spfn = spfn + 1;
    liw(d, 0);
    return dstore(i);
  }
  if (k == c_loadw || k == c_loadb) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    a = oreg(ia[i], 10);
    d = dreg(i);
    if (k == c_loadb) outw(iw3(0x4003, d, a, 0));
    else outw(iw3(0x2003, d, a, 0));
    return dstore(i);
  }
  if (k == c_stw || k == c_stb) {
    a = oreg(ia[i], 10);
    b = oreg(ib[i], 11);
    if (k == c_stb) outw(sw3(0x23, a, b, 0));
    else outw(sw3(0x2023, a, b, 0));
    return 0;
  }
  if (k == c_neg) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    a = oreg(ia[i], 10);
    d = dreg(i);
    outw(rw3(0x40000033, d, 0, a));
    return dstore(i);
  }
  if (k == c_not) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    a = oreg(ia[i], 10);
    d = dreg(i);
    outw(iw3(0x3013, d, a, 1));
    return dstore(i);
  }
  if (k == c_arg) {
    a = oreg(ia[i], 10);
    outw(0xffc48493);
    outw(sw3(0x2023, 9, a, 0));
    return 0;
  }
  if (k == c_call) {
    b = ia[i];                       // 記号番号
    if (gdef[b]) outw(jenc(gval[b] - outp) | 0xef);
    else {
      a = gval[b];
      gval[b] = outp;
      outw(a);
    }
    d = 10;
    if (live[i] && vreg[i] != -1) d = dreg(i);
    outw(iw3(0x2003, d, 9, 0));
    outw(0x00448493);
    if (live[i] && vreg[i] != -1) return dstore(i);
    return 0;
  }
  if (k == c_ret) {
    a = oreg(ia[i], 10);
    outw(0xffc48493);
    outw(sw3(0x2023, 9, a, 0));
    return 1;                        // エピローグを出す印
  }
  if (k == c_lab) {
    labpos[ia[i]] = outp;
    return 0;
  }
  if (k == c_jmp) return lrefj(ia[i]);
  if (k == c_bz) {
    // 逆条件で 1 語跳び越え + jal (B-type の ±4KiB 制限を受けない)
    a = oreg(ia[i], 10);
    outw(0x1463 | (a << 15));
    return lrefj(ib[i]);
  }
  if (k == c_bnz) {
    a = oreg(ia[i], 10);
    outw(0x463 | (a << 15));
    return lrefj(ib[i]);
  }
  // 二項演算
  if (!live[i]) return 0;
  if (vreg[i] == -1) return 0;
  a = oreg(ia[i], 10);
  b = oreg(ib[i], 11);
  d = dreg(i);
  k = iop[i] - c_bin;
  if (k == b_sgt || k == b_sle) outw(rw3(0x2033, d, b, a));
  else if (k == b_seq || k == b_sne) outw(rw3(0x40000033, d, a, b));
  else outw(rw3(binbase(k), d, a, b));
  if (k == b_sle || k == b_sge) outw(iw3(0x4013, d, d, 1));
  else if (k == b_seq) outw(iw3(0x3013, d, d, 1));
  else if (k == b_sne) outw(rw3(0x3033, d, 0, d));
  return dstore(i);
}

int epilog2() {                      // 使用レジスタの復元 + フレーム解放
  int r; int o;
  o = svbase;
  r = 13;
  while (r < 28) {
    if (rused[r]) { outw(iw3(0x2003, r, 8, o)); o = o + 4; }
    r = r + 1;
  }
  outw(0x00012083);
  outw(0x00412403);
  outw(iw3(0x13, 2, 2, fnf));
  outw(0x00008067);
  return 0;
}

int emitfn(int e) {
  int i; int r; int o; int h; int nsv;
  // 割付けとフレームレイアウト
  fold();
  dce();
  regalloc();
  nsv = 0;
  r = 13;
  while (r < 28) { if (rused[r]) nsv = nsv + 1; r = r + 1; }
  spbase = cloff + hcnt * 4;
  svbase = spbase + nspill * 4;
  fnf = svbase + nsv * 4;
  if (fnf > 2040) exit(6);
  // 関数アドレスの確定と前方参照の解決
  h = gval[e];
  gval[e] = outp;
  gdef[e] = 1;
  patchcalls(outp, h);
  if (streq(gname + e * 16, "main")) { mainoff = outp; mainok = 1; }
  // プロローグ
  outw((((0 - fnf) & 4095) << 20) | 0x10113);
  outw(0x00112023);
  outw(0x00812223);
  outw(0x00010413);
  o = svbase;
  r = 13;
  while (r < 28) {
    if (rused[r]) { outw(sw3(0x2023, 8, r, o)); o = o + 4; }
    r = r + 1;
  }
  i = cna;
  while (i) {
    i = i - 1;
    outw(iw3(0x2003, 10, 9, 0));
    outw(0x00448493);
    outw(sw3(0x2023, 8, 10, 8 + i * 4));
  }
  // 本体
  i = 0;
  while (i < labcnt) { labpos[i] = -1; i = i + 1; }
  lfixn = 0;
  spfn = 0;
  i = 0;
  while (i < icnt) {
    if (emitins(i)) epilog2();
    i = i + 1;
  }
  // ラベルの後埋め (すべて jal)
  i = 0;
  while (i < lfixn) {
    patw(getw(lfix[i]) | jenc(labpos[lflab[i]] - lfix[i]), lfix[i]);
    i = i + 1;
  }
  // 文字列プールの出力と参照の後埋め
  o = outp;
  i = 0;
  while (i < spcnt) { outbyte(spool[i]); i = i + 1; }
  i = 0;
  while (i < spfn) {
    r = 0x80000000 + o + spofs[i];
    patw((getw(spfix[i]) & 4095) | ((r + 2048) & 0xfffff000), spfix[i]);
    patw((getw(spfix[i] + 4) & 0xfffff) | ((r & 4095) << 20), spfix[i] + 4);
    i = i + 1;
  }
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
  int e;
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
  // 本体を IR として構築し，最適化・割付けの後に出力する
  icnt = 0;
  labcnt = 0;
  spcnt = 0;
  hcnt = 0;
  while (tok != o_rc) stmt();
  next();
  emit(c_ret, emit(c_const, 0, 0), 0);
  return emitfn(e);
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
  // ランタイム前置部 (32 語。scc と同一。語 4 = jal x1 main は後で patch)
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
