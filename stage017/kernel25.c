/* kernel25.c --- 簡易 OS のカーネル (Stage 17 第 5 部の世代)
 *
 * kernel24 の写しに**sfs4 と新しい配置と広い経路**を入れたものである
 * (docs/stage017-gcc.md 7 章)。変えたのは #define と，起動時に読む
 * magic と，イメージの大きさの検査だけである。
 *
 *   sfs4      表項目が 72 -> 128 バイトになり，名前の枠が 48 -> 104
 *             バイト (終端を除いて 103) に広がった。名前より後ろの
 *             6 語の並びは sfs3 と同じで，頭の 32 バイトも同じ
 *   SFSA      0x8400_0000 -> 0xa000_0000。窓が 32 MiB -> 512 MiB
 *   PATHMAX   経路を受ける器が 63 -> 255 バイト (statat / spawn)
 *
 * **名前を広げただけでは足りない。** 名前 1 段が 103 バイト持てても，
 * 経路を受ける器が 63 バイトなら，92 バイトの名前は根に置いても
 * stat できない。GCC 4.7.4 の木は最長の経路が 131 バイト・深さ 12
 * なので，そこに余りを足して 255 にした。
 *
 * **なぜ広げたか。** GCC 4.7.4 の配布木を測ると，名前の最長が 92
 * バイト，47 バイトを超えるものが 248 個あり，最小のイメージが
 * 473,459,336 バイト要る (tests/stage017/expected/gcc47-tree.txt)。
 * sfs3 と kernel24 では名前も容量も届かない。
 *
 * **なぜ退避領域より上へ移したか。** 0x8600_0000 より下は 1 バイトも
 * 動かせない。トラップフレーム (0x8370_0000)・カーネルスタック
 * (0x8380_0000)・ユーザ像のロード先 (0x8600_0000) は ld16 の前置部に
 * 機械語として焼き込まれているからである (kernel19 と同じ制約)。
 * 上へ伸ばすしかなく，SAVETOP (0xa000_0000) の先が唯一の空きである。
 * RAM を 512 MiB から 1 GiB へ広げ，0xa000_0000〜0xc000_0000 を
 * sfs に当てた。
 *
 *   SFSA    0x8400_0000 -> 0xa000_0000   sfs イメージ
 *   SFSTOP  (新)           0xc000_0000   その上端 = RAM の終わり
 *
 * **0x8400_0000〜0x8600_0000 の 32 MiB が空いた。** 0x8100_0000〜
 * 0x8370_0000 と合わせ，当面は使わない。
 *
 * **イメージが窓に入らなければ起動しない。** 頭の大きさの欄を見て，
 * 窓を超えるなら 'S' を出して止まる。黙って載せると，はみ出した先は
 * 何も無い番地なので，書いた中身が消える。
 *
 * kernel24 (第 4 部の 1) は時刻を入れた世代である。以下はそれ以前
 * からの引き継ぎ。
 *
 *   sfs3          表項目が 64 -> 72 バイトになり，末尾に更新時刻
 *                 (epoch からのナノ秒) を u32 2 本で持つ
 *   goldfish RTC  0x101000 を読んで今の時刻を得る
 *   statat (79)   長さ・更新時刻・種別を返す
 *
 * **秒に直さずナノ秒のまま持つ。** 秒にするには 64 bit の除算が要り，
 * 我々の C は 32 bit である。比較は上位語 -> 下位語の 2 段で済むので，
 * make が要るぶんにはこれで足りる (11.2)。
 *
 * **TIME_LOW を読むと TIME_HIGH が固定される** 仕様なので，必ず
 * 下位から読む。逆に読むと桁上がりの瞬間に 1 秒ずれた値が出る。
 *
 * 立てる場所は 2 つだけである (11.4)。
 *
 *   sfsmk     作ったとき
 *   sfswrite  書いたとき。**0 バイトの書込みでも更新する**
 *             (`> file` で切り詰めるのも変更である)
 *
 * 読み出しでは触らない (atime は持たない)。
 *
 * kernel23 (第 2 部) は引数の数と長さを広げた世代である。以下はそれ
 * 以前からの引き継ぎ。
 *
 * kernel22 の写しに**引数の数と長さの拡張**を入れたものである
 * (docs/stage017-cc.md 8 章)。
 *
 *   引数の数    8 -> 64
 *   引数の全長  511 -> 4095 バイト
 *
 * これが要るのは書庫を作るからである。`ar rcs libc.a` に libc の 8 つ
 * の員を並べると語数は 11 になる。kernel22 は
 *
 *     while (kargc < 8) { ... }
 *
 * と書いていて，**溢れたぶんを黙って捨てていた**。ar は受け取った
 * 8 語だけを見て 5 員の書庫を作り，0 を返して終わる。呼んだ側からは
 * 成功に見えるのに中身が足りない。我々の台帳でいう bad である
 * (docs/stage016-os.md 9.4)。数を増やすだけでは同じ罠が先へ動く
 * だけなので，**溢れたら E2BIG で落とす**ことを併せて入れる。
 *
 * kernel22 (Stage 16 第 4 部の 3) は kernel21 の写しに**空装置
 * /dev/null** を入れた世代である。以下はそれ以前からの引き継ぎ。
 *
 * kernel21 (第 4 部の 2) の写しに**空装置 /dev/null** を入れたもので
 * ある (docs/stage016-os.md 11.5)。
 *
 *   /dev/null   読めば必ず 0 バイト，書けば必ず全部受け取って捨てる
 *
 * これが要るのは configure が
 *   $cc $OPT1 $OPT2 -o a.out -c -xc - < /dev/null > cc_msg.txt 2>&1
 * と書くからである。**空装置から読む**ので，「開けないから捨てる」
 * では済まない。開けなければ spawn が ENOENT で落ちる。
 *
 * 空のふつうのファイルを 1 つ置くのでは駄目である。読む側は確かに
 * 0 バイトを得るが，**書くと中身が溜まる**。溜まった後に読めば以前
 * 書いたものが返る。動くように見えて意味が違う，我々が何度も踏んだ
 * 型の誤りになる。そこで項目に印 (F_NULL) を付け，sfsread と
 * sfswrite の 2 箇所でだけ振り分ける。/dev と /dev/null は起動時に
 * 無ければ作る。イメージの側に用意を求めない。
 *
 * kernel21 (第 4 部の 2) は標準エラーのつなぎ替えを入れた世代である。
 * 以下はそれ以前からの引き継ぎ。
 *
 * kernel20 (第 4 部の 1) の写しに**標準エラーのつなぎ替え**を入れた
 * ものである (docs/stage016-os.md 10.5)。
 *
 *   spawn2 (501)  spawn (500) と同じだが，記録が 5 語で err を持つ
 *
 * 500 番はそのまま残す。記録の長さが違うので**番号を分けないと，古い
 * 呼び手が積んだ 4 語の記録の後ろを読んでしまう**。
 *
 * これが要るのは sh2 が 2>&1 を実装できないからである。configure は
 *   diff $TMPH config.h >/dev/null 2>&1
 * のように標準エラーを捨てる/集める書き方をしており，つなぎ替えが
 * 無いと端末へ漏れる。
 *
 * kernel20 (第 4 部の 1) はファイルの削除を入れた世代である。以下は
 * それ以前からの引き継ぎ。
 *
 * kernel19 (第 3 部) の写しに**ファイルの削除**を入れたものである
 * (docs/stage016-os.md 9.4)。
 *
 *   unlinkat (35)  項目を未使用に戻す。データの穴は詰めない
 *
 * 詰めないのは，詰めると全項目の dataoff を書き換えることになり，
 * **開いている fd が指す位置がずれる**からである。代償は「作っては
 * 消すを繰り返すとイメージを使い切る」ことだが，それは ENOSPC で
 * はっきり失敗するので bad ではない。
 *
 * 削除はディレクトリには効かない (中身が残るため)。空のディレクトリを
 * 消す道は要るときに足す。
 *
 * 第 4 部はシェルにパイプを入れるのが本題で，そのパイプは一時ファイルで
 * 中継する。**削除が無いと一時ファイルが溜まる一方になる**ので，
 * これが土台になる。
 *
 * kernel19 (第 3 部) は記憶域を 14 MB から 256 MB へ広げた世代である。
 * 以下はそれ以前からの引き継ぎ。
 *
 * kernel18 (第 2 部) の写しに**記憶域の拡張**を入れたものである
 * (docs/stage016-os.md 8 章)。変えたのは配置の #define 4 つだけで，
 * コードは 1 行も違わない。
 *
 *   UBRKMAX  0x86e0_0000 -> 0x9600_0000   ヒープの上限 (14 MB -> 256 MB)
 *   USP      0x8700_0000 -> 0x9700_0000   フレームスタックの上端
 *   SAVEA    0x8100_0000 -> 0x9700_0000   spawn の退避領域
 *   SAVETOP  0x8370_0000 -> 0xa000_0000   同上限
 *
 * **0x8600_0000 より下は 1 バイトも動かしていない。** トラップフレーム
 * (0x8370_0000)・カーネルスタック (0x8380_0000)・ユーザ像のロード先
 * (0x8600_0000) は ld16 の前置部に機械語として焼き込まれており，動かす
 * とリンカの世代が要るからである (8.2)。広げるのは上だけにした。
 *
 * 退避領域 (144 MB) がヒープ (256 MB) より小さいのは意図した釣り合い
 * である (8.4)。足りなければ sys_spawn が ENOMEM を返す。
 *
 * 0x8100_0000〜0x8370_0000 は退避領域を移した跡が空いている。当面は
 * 使わない (sfs2 を 32 MB より大きくしたくなったときの行き先)。
 *
 * kernel18 (第 2 部) はディレクトリの操作を入れた世代である。以下は
 * それ以前からの引き継ぎ。
 *
 * kernel17 (第 1 部) の写しに**ディレクトリの操作**を入れたものである
 * (docs/stage016-os.md 7 章)。
 *
 *   作業ディレクトリ  cwd を 1 つ持つ。walk は先頭が / ならルートから，
 *                     そうでなければ cwd から辿る。. は動かず，.. は
 *                     親へ上がる (ルートの親はルート自身)
 *   getdents64 (61)   ディレクトリの項目を Linux と同じ形で返す。
 *                     ディレクトリの fd では fdpos が「次に見る表の索引」
 *                     の意味になるので，read / write は EISDIR で拒む
 *   mkdirat (34)      ディレクトリを作る (RV32 に mkdir は無い)
 *   chdir (49) / getcwd (17)
 *
 * cwd は spawn の退避レコードにも入れた。子は親の cwd を引き継いで
 * 始まり，子が chdir しても親は動かない (Unix と同じ)。
 *
 * kernel17 (第 1 部) は sfs2 の経路解決を入れた世代である。以下は
 * それ以前からの引き継ぎ。
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
#define SAVEA   0x97000000      /* spawn の退避領域 (ここから上へ積む) */
#define SAVETOP 0xa0000000      /* 退避領域の上限 (= RAM の終わり) */
#define SFSA    0xa0000000      /* 共有領域 (sfs イメージ) */
#define SFSTOP  0xc0000000      /* その上端 (= RAM の終わり) */

/* 経路の上限 (終端を除く)。**名前の上限より深い理由がある。**
 * 名前 1 段が 103 バイト持てても，経路を受ける器が 63 バイトしか
 * 無ければ，その名前は stat も spawn もできない。GCC 4.7.4 の木は
 * 最長の経路が 131 バイト・深さ 12 なので，そこに余りを足して 255 と
 * した (器は 256 バイト。カーネルスタックは 0x8380_0000 にある) */
#define PATHMAX 255
#define UBASE   0x86000000      /* ユーザ像のロード位置 */
#define USP     0x97000000      /* ユーザのフレームスタック上端 */
#define UBRKMAX 0x96000000      /* brk の上限 */
#define UARTA   0x10000000      /* UART */
/* goldfish-rtc (QEMU virt の device tree に rtc@101000 として居る)。
 * +0 が epoch からのナノ秒の下位 32 bit，+4 が上位 32 bit */
#define RTCA    0x00101000
#define RTC_LO  0
#define RTC_HI  4

/* sfs4 の表 (docs/stage017-gcc.md 7 章)。sfs3 との違いは**名前の枠が
 * 48 -> 104 バイトに広がり，項目が 72 -> 128 バイトになった**ことだけ
 * である。128 は 2 の冪なので，索引から位置を出す算術が軽くなる。
 * 以下は sfs3 からの引き継ぎ。
 *
 * sfs3 の表 (docs/stage017-cc.md 11.3)。sfs2 との違いは**項目が
 * 64 -> 72 バイトになり，末尾に更新時刻が付いた**ことだけである。
 * 以下は sfs2 からの引き継ぎ。
 *
 * sfs2 の表 (docs/stage016-os.md 6.3)。sfs1 との違いは
 * **名前が 52 -> 48 に縮み，空いた 4 バイトが親の索引になった**こと。
 * 項目は 64 バイトのままなので算術は変わらない */
#define ENTSZ   128
#define E_NAME  0
#define NAMEMAX 103
#define E_PAR   104
#define E_OFF   108
#define E_LEN   112
#define E_FLAG  116
#define F_USED  1
#define F_DIR   2
/* 消されたが，まだ開いている fd が指している項目 (第 4 部の 1)。
 * F_USED は立てたままにするので sfsmk が再利用しない。名前と親は
 * 消してあるので経路からは引けない。最後の fd が閉じたとき解放する */
#define F_DEL   4
/* 空装置 (第 4 部の 3)。読めば 0 バイト，書けば捨てる。表の上では
 * ふつうの項目と同じに見えるので，ls も cd も getdents64 も何も
 * 知らなくてよい。振り分けるのは sfsread と sfswrite の 2 箇所だけ */
#define F_NULL  8

/* 更新時刻 (epoch からのナノ秒)。**秒に直さない** (11.2) */
#define E_MTLO  120
#define E_MTHI  124

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
#define SYS_SPAWN2 501
/* 第 2 部で足したディレクトリ操作 (docs/stage016-os.md 7.2) */
#define SYS_GETCWD    17
#define SYS_MKDIRAT   34
#define SYS_UNLINKAT  35
#define SYS_CHDIR     49
#define SYS_GETDENTS  61
/* 第 4 部の 1 で足した。長さ・更新時刻・種別だけを返す。
 * **持っていない欄 (許可・所有者) は返さない** (11.5) */
#define SYS_STATAT    79

/* getdents64 の 1 件の形 (Linux と同じ)。64 bit の欄は 32 bit の語
 * 2 本として書く。カーネルは 32 bit なので上位は常に 0 である */
#define D_INO    0              /* u64 */
#define D_OFF    8              /* s64 */
#define D_RECLEN 16             /* u16 */
#define D_TYPE   18             /* u8 */
#define D_NAME   19
#define DT_DIR   4
#define DT_REG   8

/* errno (負値で返す) */
#define ENOENT   2
#define E2BIG    7
#define ENOEXEC  8
#define EBADF    9
#define ENOMEM   12
#define EEXIST   17
#define EBUSY    16
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENOSPC   28
#define ERANGE   34
#define ENOSYS   38

#define NFD 16
#define MAXDEP 8

int tblo;                       /* 表の先頭 (sfs 内オフセット) */
int tbln;                       /* 表の件数 */
/* 作業ディレクトリ (表の項目番号。0 = ルート)。プロセスは一度に 1 つ
 * しか走らないので，カーネルが 1 つ持てば足りる (7.3) */
int cwd;
int fdent[16];                  /* fd -> 表の項目番号 (-1 = 未使用)。NFD 個 */
int fdpos[16];                  /* fd -> 読み書き位置 */
unsigned ubrk;                  /* ユーザの break */

/* fd 0 / fd 1 の結び先 (-1 = UART，それ以外 = sfs の項目番号) */
int fd0ent;
int fd0pos;
int fd1ent;
int fd1pos;
/* fd 2 のつなぎ先 (-1 = UART)。第 4 部の 2 で足した */
int fd2ent;
int fd2pos;

/* spawn の入れ子。sbase[d] は深さ d の親の退避レコードの先頭 */
int depth;
unsigned savecur;
unsigned sbase[8];              /* MAXDEP 段 */

/* spawn / boot の引数の写し。親の像は子の配置で消えるので，文字列は
 * 先にカーネル側へ写す (docs/stage013-tools.md 3.2) */
#define NARGV   64              /* 引数の数の上限 (kernel22 は 8) */
#define NARGS   4096            /* 引数文字列の全長 (kernel22 は 512) */
char kargs[NARGS];              /* 引数文字列の連結 (NUL 区切り) */
int kargo[NARGV];               /* kargs 内の各引数の開始位置 */
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

/* 第 4 部の 1 で足したもの。sys_close が定義より前で呼ぶので宣言しておく */
int inuse(int i);
int freeent(int i);

/* 表の項目 i の絶対アドレス */
unsigned ent(int i) { return SFSA + tblo + i * ENTSZ; }

/* 今の時刻 (epoch からのナノ秒) を lo / hi へ。
 *
 * **必ず下位から読む。** goldfish は TIME_LOW を読んだ瞬間の上位語を
 * 固定して TIME_HIGH に見せる。逆に読むと桁上がりの瞬間に 1 秒ずれる */
void rtcnow(unsigned *lo, unsigned *hi) {
  *lo = *(volatile unsigned *)(RTCA + RTC_LO);
  *hi = *(volatile unsigned *)(RTCA + RTC_HI);
}

/* 項目 i に今の時刻を書く */
void touchent(int i) {
  unsigned lo;
  unsigned hi;
  rtcnow(&lo, &hi);
  st4(ent(i) + E_MTLO, lo);
  st4(ent(i) + E_MTHI, hi);
}

/* 項目の名前が s の [0, n) と一致し，そこで終わっているか。
 * 経路の 1 段ぶんを切り出さずに比べるための形である */
int nameeqn(unsigned e, char *s, int n) {
  char *p;
  int i;
  p = (char *)e;
  if (n > NAMEMAX) return 0;
  for (i = 0; i < n; i++)
    if (p[i] != s[i]) return 0;
  return p[n] == 0;
}

/* 親 par の中から名前 s[0..n) を持つ項目を引く。無ければ -1 */
int childof(int par, char *s, int n) {
  int i;
  unsigned e;
  for (i = 0; i < tbln; i++) {
    e = ent(i);
    if ((ld4(e + E_FLAG) & F_USED) == 0) continue;
    if (ld4(e + E_FLAG) & F_DEL) continue;      /* 消された項目は引けない */
    if (i == 0) continue;               /* 索引 0 はルート (親は自分自身) */
    if ((int)ld4(e + E_PAR) != par) continue;
    if (nameeqn(e + E_NAME, s, n)) return i;
  }
  return -1;
}

/* 経路を辿る。stop が 1 なら**最後の 1 段の手前まで**で止め，
 * その親を返す (作成に使う)。見つからなければ -1。
 * 返した後 *lastp は最後の段の先頭を，*lastn はその長さを指す。
 *
 * 出発点は先頭の文字で決まる (7.3)。/ で始まればルート，そうでなければ
 * 作業ディレクトリである。第 1 部との違いはここだけで，あとは . と ..
 * の 2 段が増えただけである */
int walk(char *path, int stop, char **lastp, int *lastn) {
  int cur;
  int i;
  int st;
  int n;
  i = 0;
  if (path[0] == '/') {
    cur = 0;                            /* 絶対経路はルートから */
    while (path[i] == '/') i = i + 1;   /* 先頭の / は読み飛ばす */
  } else {
    cur = cwd;                          /* 相対経路は作業ディレクトリから */
  }
  *lastp = path + i;
  *lastn = 0;
  while (path[i]) {
    st = i;
    while (path[i] && path[i] != '/') i = i + 1;
    n = i - st;
    while (path[i] == '/') i = i + 1;   /* 続く / をまとめて読み飛ばす */
    if (n == 0) continue;               /* 空の段 ("a//b" の中) は無視 */
    if (path[i] == 0 && stop) {         /* これが最後の段 */
      *lastp = path + st;
      *lastn = n;
      return cur;
    }
    /* . は動かない。.. は親へ上がる。**ルートの親はルート自身**なので
     * /.. は / になる (Linux と同じ)。親は必ずディレクトリなので，
     * この 2 段では下の「ファイルを途中に挟む」検査は要らない */
    if (n == 1 && path[st] == '.') continue;
    if (n == 2 && path[st] == '.' && path[st + 1] == '.') {
      cur = (int)ld4(ent(cur) + E_PAR);
      continue;
    }
    cur = childof(cur, path + st, n);
    if (cur < 0) return -1;
    if ((ld4(ent(cur) + E_FLAG) & F_DIR) == 0 && path[i]) return -1;
    *lastp = path + st;
    *lastn = n;
  }
  return cur;
}

/* 経路で表を引く。見つからなければ -1 */
int sfsfind(char *path) {
  char *lp;
  int ln;
  return walk(path, 0, &lp, &ln);
}

/* 新しい項目を作る。データはカーソル位置から追記で伸ばす (4.2)。
 * 親のディレクトリは既に無ければならない */
int sfsmk(char *path, int isdir) {
  int i;
  int j;
  int par;
  char *nm;
  int nn;
  unsigned e;
  par = walk(path, 1, &nm, &nn);
  if (par < 0) return -1;
  if (nn == 0 || nn > NAMEMAX) return -1;
  /* 最後の段が . や .. なら作れない。walk は最後の段を辿らずに返すので，
   * ここで弾かないと ".." という名前の項目ができてしまう */
  if (nn == 1 && nm[0] == '.') return -1;
  if (nn == 2 && nm[0] == '.' && nm[1] == '.') return -1;
  if ((ld4(ent(par) + E_FLAG) & F_DIR) == 0) return -1;
  for (i = 0; i < tbln; i++) {
    e = ent(i);
    if ((ld4(e + E_FLAG) & F_USED) == 0) {
      for (j = 0; j < nn; j++) *(char *)(e + j) = nm[j];
      *(char *)(e + j) = 0;
      st4(e + E_PAR, (unsigned)par);
      st4(e + E_OFF, ld4(SFSA + 16));   /* カーソル */
      st4(e + E_LEN, 0);
      if (isdir) st4(e + E_FLAG, F_USED + F_DIR);
      else st4(e + E_FLAG, F_USED);
      touchent(i);                      /* 作ったときに立てる (11.4) */
      return i;
    }
  }
  return -1;
}

/* 互換の名前 (呼び手を変えずに済ませる) */
int sfsnew(char *path) { return sfsmk(path, 0); }

/* /dev と /dev/null を用意する (第 4 部の 3)。既にあれば触らない。
 *
 * **イメージの側に置くのではなくカーネルが作る。** イメージは host の
 * ディレクトリを写したものなので「これは装置である」と書き表せない。
 * 空のふつうのファイルを置くと，書いた分が溜まる別物になる。
 *
 * 作れなかったとき (表が満杯) は黙って諦める。空装置が無いだけで
 * 起動そのものは通るし，使う側は open が ENOENT で失敗するので
 * 気づける */
int mknull(void) {
  int d;
  int i;
  d = sfsfind("/dev");
  if (d < 0) {
    d = sfsmk("/dev", 1);
    if (d < 0) return -1;
  }
  if ((ld4(ent(d) + E_FLAG) & F_DIR) == 0) return -1;
  i = sfsfind("/dev/null");
  if (i < 0) {
    i = sfsnew("/dev/null");
    if (i < 0) return -1;
  }
  st4(ent(i) + E_LEN, 0);
  st4(ent(i) + E_FLAG, ld4(ent(i) + E_FLAG) | F_NULL);
  return 0;
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
  /* 空装置は常に終端 (第 4 部の 3) */
  if (ld4(ent(ei) + E_FLAG) & F_NULL) return 0;
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
  /* 空装置は全部受け取って捨てる。E_LEN は 0 のままなので，後から
   * 読んでも何も出てこない (第 4 部の 3)。**時刻も立てない** ——
   * 中身を持たないものに更新時刻は無い */
  if (ld4(e + E_FLAG) & F_NULL) return n;
  /* **0 バイトの書込みでも立てる。** `> file` で切り詰めるのも変更で
   * ある。ここを n > 0 で括ると「切り詰めたのに古いまま」になり，
   * make が作り直しを飛ばす (11.4) */
  touchent(ei);
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
  if (fd == 2 && fd2ent >= 0) {
    w = sfswrite(fd2ent, fd2pos, buf, n);
    if (w < 0) return w;
    fd2pos = fd2pos + w;
    return w;
  }
  if (fd == 1 || fd == 2) {
    for (i = 0; i < n; i++) putc(*(char *)(buf + i) & 255);
    return n;
  }
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  if (ld4(ent(fdent[fd]) + E_FLAG) & F_DIR) return 0 - EISDIR;
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
  /* ディレクトリの fd では fdpos が「次に見る表の索引」の意味になる
   * (getdents64)。ふつうの read を通すと位置が混ざるので拒む */
  if (ld4(ent(fdent[fd]) + E_FLAG) & F_DIR) return 0 - EISDIR;
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
  } else if (ld4(ent(i) + E_FLAG) & F_DIR) {
    /* ディレクトリは読み出し (getdents64) のためだけに開ける。
     * 書き込みや切詰めを許すと表を壊す (第 2 部) */
    if (flags & (1 | 2 | 64 | 512)) return 0 - EISDIR;
  } else if (flags & 512) {                     /* O_TRUNC */
    st4(ent(i) + E_LEN, 0);
    touchent(i);                /* 切詰めも変更である (11.4) */
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
  int i;
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  i = fdent[fd];
  fdent[fd] = -1;
  /* 消されるのを待っていた項目なら，最後の fd が閉じたここで解放する */
  if ((ld4(ent(i) + E_FLAG) & F_DEL) && !inuse(i)) freeent(i);
  return 0;
}

/* ---- ディレクトリの操作 (第 2 部。docs/stage016-os.md 7 章) ---- */

/* 作業ディレクトリを移す */
int sys_chdir(unsigned path) {
  int i;
  i = sfsfind((char *)path);
  if (i < 0) return 0 - ENOENT;
  if ((ld4(ent(i) + E_FLAG) & F_DIR) == 0) return 0 - ENOTDIR;
  cwd = i;
  return 0;
}

/* 経路の長さ・更新時刻・種別を out へ書く (第 4 部の 1。11.5)。
 *
 * out の配置 (u32 4 語):
 *   0  長さ (バイト)
 *   4  更新時刻 (ナノ秒) の下位 32 bit
 *   8  同 上位 32 bit
 *   12 種別 (1 = ディレクトリ, 2 = ふつうのファイル)
 *
 * **許可・所有者は返さない。** 持っていない欄を 0 で埋めて名前だけ
 * 揃えると，呼び手が「見た」つもりになる。無いものは無いままにする */
int sys_statat(int dirfd, unsigned path, unsigned out) {
  char p[PATHMAX + 1];
  int i;
  unsigned e;
  (void)dirfd;                          /* 常に AT_FDCWD 相当 */
  if (cpystr(p, path, PATHMAX) < 0) return 0 - E2BIG;
  i = sfsfind(p);
  if (i < 0) return 0 - ENOENT;
  e = ent(i);
  st4(out + 0, ld4(e + E_LEN));
  st4(out + 4, ld4(e + E_MTLO));
  st4(out + 8, ld4(e + E_MTHI));
  st4(out + 12, (ld4(e + E_FLAG) & F_DIR) ? 1 : 2);
  return 0;
}

/* 作業ディレクトリの経路を buf へ書く。返り値は NUL を含む長さ
 * (Linux の getcwd と同じ)。**親を辿れるのは sfs2 だから**であって，
 * sfs1 にはそもそも親が無かった (7.3) */
int sys_getcwd(unsigned buf, int size) {
  int stk[64];                          /* 根までの項目番号 (下から順に) */
  int ns;
  int i;
  int k;
  int p;
  unsigned e;
  ns = 0;
  i = cwd;
  while (i != 0) {                      /* ルート (0) の親はルート自身 */
    if (ns >= 64) return 0 - ENOMEM;
    stk[ns] = i;
    ns = ns + 1;
    i = (int)ld4(ent(i) + E_PAR);
  }
  if (ns == 0) {                        /* ルートにいる */
    if (size < 2) return 0 - ERANGE;
    *(char *)buf = '/';
    *(char *)(buf + 1) = 0;
    return 2;
  }
  p = 0;
  for (k = ns - 1; k >= 0; k = k - 1) { /* 根の側から並べ直す */
    if (p + 1 >= size) return 0 - ERANGE;
    *(char *)(buf + p) = '/';
    p = p + 1;
    e = ent(stk[k]) + E_NAME;
    i = 0;
    while (*(char *)(e + i)) {
      if (p + 1 >= size) return 0 - ERANGE;
      *(char *)(buf + p) = *(char *)(e + i);
      p = p + 1;
      i = i + 1;
    }
  }
  *(char *)(buf + p) = 0;
  return p + 1;
}

/* ディレクトリを作る。RV32 に mkdir は無く mkdirat だけである */
/* ファイルを消す。項目を未使用に戻すだけで，データの穴は詰めない
 * (docs/stage016-os.md 9.4 の D1)。
 *
 * 開いている fd がその項目を指していても表からは消える。fd 側は
 * fdent に番号を持ったままなので読み書きは続く —— これは Unix の
 * 「開いている間は消えない」に近い振舞いで，一時ファイルの常套手段
 * (作って開いてすぐ消す) がそのまま通る */
/* 項目 i を指している fd があるか */
int inuse(int i) {
  int k;
  for (k = 3; k < NFD; k++) if (fdent[k] == i) return 1;
  if (fd0ent == i || fd1ent == i) return 1;
  return 0;
}

/* 項目を本当に解放する (名前・親・長さ・flags をすべて消す)。
 * データの穴は詰めない (docs/stage016-os.md 9.4 の D1) */
int freeent(int i) {
  unsigned e;
  e = ent(i);
  st4(e + E_FLAG, 0);
  st4(e + E_PAR, 0);
  st4(e + E_LEN, 0);
  st4(e + E_OFF, 0);
  st4(e + E_MTLO, 0);           /* 空いた枠に古い時刻を残さない */
  st4(e + E_MTHI, 0);
  *(char *)(e + E_NAME) = 0;
  return 0;
}

int sys_unlinkat(int dirfd, unsigned path, int flags) {
  int i;
  unsigned e;
  i = sfsfind((char *)path);
  if (i < 0) return 0 - ENOENT;
  if (i == 0) return 0 - EBUSY;                 /* ルートは消せない */
  e = ent(i);
  /* ディレクトリは中身が残るので消さない。空かどうかを見て許す道も
   * あるが，要るまで作らない (要らない機能は bad の温床である) */
  if (ld4(e + E_FLAG) & F_DIR) return 0 - EISDIR;
  if (inuse(i)) {
    /* まだ開いている。**名前と親だけ消して実体は残す。**
     * 経路からは引けなくなるが，その fd からは読み書きできる ——
     * Unix の「開いている間は消えない」である。一時ファイルの常套手段
     * (作って開いてすぐ消す) がこれに依る。
     *
     * F_USED を落とさないのは sfsmk に再利用させないためである。
     * 落とすと，まだ読んでいる fd の下で別のファイルが同じ項目に
     * 割り当たり，**黙って別の中身が読める**ことになる */
    st4(e + E_PAR, 0);
    *(char *)(e + E_NAME) = 0;
    st4(e + E_FLAG, ld4(e + E_FLAG) | F_DEL);
    return 0;
  }
  freeent(i);
  return 0;
}

int sys_mkdirat(int dirfd, unsigned path, int mode) {
  if (sfsfind((char *)path) >= 0) return 0 - EEXIST;
  if (sfsmk((char *)path, 1) < 0) return 0 - ENOENT;
  return 0;
}

/* ディレクトリの項目を読み出す。fdpos[fd] を「次に見る表の索引」として
 * 使い回す (ディレクトリの fd では読み書き位置の意味を持たない)。
 *
 * . と .. は返さない。sfs2 に実体が無く，POSIX も「dot / dot-dot を
 * 返すかどうかは未規定」としている */
int sys_getdents64(int fd, unsigned buf, int n) {
  int par;
  int i;
  int k;
  int p;
  int nl;
  int rl;
  unsigned e;
  if (fd < 3 || fd >= NFD || fdent[fd] < 0) return 0 - EBADF;
  par = fdent[fd];
  if ((ld4(ent(par) + E_FLAG) & F_DIR) == 0) return 0 - ENOTDIR;
  p = 0;
  i = fdpos[fd];
  if (i < 1) i = 1;                     /* 索引 0 はルート自身 */
  while (i < tbln) {
    e = ent(i);
    /* F_DEL は「消されたがまだ開いている」項目である。親を 0 にして
     * あるので，除かないとルートの一覧に空の名前で現れてしまう */
    if ((ld4(e + E_FLAG) & F_USED) != 0 && (ld4(e + E_FLAG) & F_DEL) == 0
        && (int)ld4(e + E_PAR) == par) {
      nl = 0;
      while (*(char *)(e + E_NAME + nl)) nl = nl + 1;
      rl = (D_NAME + nl + 1 + 7) / 8 * 8;       /* 8 バイト境界へ */
      if (p + rl > n) {
        /* 1 件も入らないなら器が小さすぎる。0 を返すと呼び手が
         * 「終わり」と読んでしまうので，はっきり誤りとして返す */
        if (p == 0) return 0 - EINVAL;
        break;                          /* この件は次回に回す */
      }
      st4(buf + p + D_INO, (unsigned)i);
      st4(buf + p + D_INO + 4, 0);
      st4(buf + p + D_OFF, (unsigned)(i + 1));
      st4(buf + p + D_OFF + 4, 0);
      *(char *)(buf + p + D_RECLEN) = rl & 255;
      *(char *)(buf + p + D_RECLEN + 1) = (rl >> 8) & 255;
      if ((ld4(e + E_FLAG) & F_DIR) != 0) *(char *)(buf + p + D_TYPE) = DT_DIR;
      else *(char *)(buf + p + D_TYPE) = DT_REG;
      for (k = 0; k < nl; k++)
        *(char *)(buf + p + D_NAME + k) = *(char *)(e + E_NAME + k);
      *(char *)(buf + p + D_NAME + nl) = 0;
      p = p + rl;
    }
    i = i + 1;
  }
  fdpos[fd] = i;
  return p;
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
 * USP - NARGS から置く。前置部が sp から読む (docs/stage012-os.md 5.5) */
unsigned setupstack(void) {
  unsigned w;
  unsigned sp;
  int i;
  int j;
  w = USP - NARGS;
  /* 文字列 NARGS + argc 1 語 + argv NARGV 語 + 番兵 2 語。
   * 端数は切り上げて 16 の倍数にしてある */
  sp = USP - NARGS - (16 + NARGV * 4);
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
 * +292 cwd 1 語 (第 2 部), +296 fd2ent, +300 fd2pos (第 4 部の 2),
 * +304 から像 [UBASE, ubrk) とフレームスタック [sp, USP) の複写 */
/* 記録が 5 語 (err つき) かどうかを持ち回す。spawn2 (501) だけが立てる */
static int spawnerr;

int sys_spawn(unsigned sa) {
  unsigned *tf;
  char path[PATHMAX + 1];
  char name[PATHMAX + 1];
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
  if (cpystr(path, ld4(sa), PATHMAX) < 0) return 0 - E2BIG;
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
  if (savecur + 304 + imgsz + stksz > SAVETOP) return 0 - ENOMEM;

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
    for (;;) {
      p = ld4(argvp + kargc * 4);
      if (p == 0) break;
      /* **溢れたら黙って捨てず落とす。** kernel22 はここが
       * while (kargc < 8) で，9 語目から先を捨てて 0 を返していた */
      if (kargc >= NARGV) return 0 - E2BIG;
      kargo[kargc] = n;
      i = cpystr(kargs + n, p, NARGS - 1 - n);
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
    if (cpystr(name, p, PATHMAX) < 0) return 0 - E2BIG;
    ie = sfsfind(name);
    if (ie < 0) return 0 - ENOENT;
    ipos = 0;
  }
  oe = fd1ent;
  opos = fd1pos;
  p = ld4(sa + 12);
  if (p != 0) {
    if (cpystr(name, p, PATHMAX) < 0) return 0 - E2BIG;
    oe = sfsfind(name);
    if (oe < 0) {
      oe = sfsnew(name);
    } else {
      st4(ent(oe) + E_LEN, 0);                  /* 切詰め */
      touchent(oe);
    }
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
  /* 作業ディレクトリも親のものへ戻す。子が chdir しても親は動かない
   * (Unix と同じ)。子は親の cwd を引き継いで始まる */
  st4(b + 292, (unsigned)cwd);
  st4(b + 296, (unsigned)fd2ent);
  st4(b + 300, (unsigned)fd2pos);
  wcopy(b + 304, UBASE, imgsz);
  wcopy(b + 304 + imgsz, psp, stksz);
  savecur = b + 304 + imgsz + stksz;
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
  /* 標準エラー。spawn2 (501) のときだけ 5 語目を見る。spawn (500) は
   * 記録が 4 語なので**読んではいけない** (後ろは別のものである) */
  fd2ent = -1;
  fd2pos = 0;
  if (spawnerr) {
    unsigned pe;
    pe = ld4(sa + 16);
    if (pe != 0) {
      if (cpystr(name, pe, PATHMAX) >= 0) {
        int ee;
        ee = sfsfind(name);
        if (ee < 0) {
          ee = sfsnew(name);
        } else {
          st4(ent(ee) + E_LEN, 0);
          touchent(ee);
        }
        fd2ent = ee;
        fd2pos = 0;
      }
    }
  }
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
  cwd = (int)ld4(b + 292);
  fd2ent = (int)ld4(b + 296);
  fd2pos = (int)ld4(b + 300);
  wcopy(UBASE, b + 304, imgsz);
  wcopy(psp, b + 304 + imgsz, stksz);
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
  else if (n == SYS_SPAWN) { spawnerr = 0; r = sys_spawn(tf[9]); }
  else if (n == SYS_SPAWN2) { spawnerr = 1; r = sys_spawn(tf[9]); spawnerr = 0; }
  else if (n == SYS_GETDENTS) r = sys_getdents64((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_MKDIRAT) r = sys_mkdirat((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_UNLINKAT) r = sys_unlinkat((int)tf[9], tf[10], (int)tf[11]);
  else if (n == SYS_CHDIR) r = sys_chdir(tf[9]);
  else if (n == SYS_GETCWD) r = sys_getcwd(tf[9], (int)tf[10]);
  else if (n == SYS_STATAT) r = sys_statat((int)tf[9], tf[10], tf[11]);
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
  unsigned isz;
  char name[64];

  if (ld4(SFSA) != 0x34736673) {        /* 'sfs4' */
    putc('?');
    putc('\n');
    return 1;
  }
  /* 窓に入らないイメージは載せない。引き算の向きに注意 ---
   * SFSA + 大きさ で比べると 32 bit を回り込んで通ってしまう。
   * 両辺を unsigned に揃えるのは，符号つきで比べると 2^31 以上の
   * でたらめな大きさが負数になって通り抜けるからである */
  isz = ld4(SFSA + 4);
  if (isz > (unsigned)(SFSTOP - SFSA)) {
    putc('S');
    putc('\n');
    return 1;
  }
  tblo = (int)ld4(SFSA + 8);
  tbln = (int)ld4(SFSA + 12);
  for (i = 0; i < NFD; i++) fdent[i] = -1;
  cwd = 0;                              /* 起動時の作業ディレクトリはルート */
  fd0ent = -1;
  fd1ent = -1;
  fd2ent = -1;
  depth = 0;
  savecur = SAVEA;
  mknull();                             /* /dev/null (第 4 部の 3) */

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
  while (kargc < NARGV) {
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
