/// @file cc11.sc
/// @brief C コンパイラ。ELF リロケータブルオブジェクトを出力する。
///
/// 設計は docs/stage010-c89.md，出力形式は docs/stage008-elf-ld.md，
/// 入力言語の基礎は docs/stage005-sc.md 2 章。第 3 部の 4 の cc10.sc を
/// 出発点とし，**文字エスケープを C89 の全種類に広げた** ものである
/// (Stage 10 の補遺)。
///
/// @section escfix なぜ補遺が要ったか
/// 第 3 部の 4 で「C89 の言語仕様が揃った」としたが，字句の側に穴が残って
/// いた。エスケープが \n \t \0 \\ \' \" の 6 種しかなく，C89 7.1.3 が
/// 定める \a \b \f \r \v \? と，8 進・16 進のエスケープが無かった。
/// Stage 11 の libc を書き始めて isspace が \f \r \v を必要とし，そこで
/// 気づいた。libc 側で 12 や 13 と数値で書けば通るが，それは処理系の穴を
/// 利用者に押しつけることになるので，処理系を直す。
///
/// 消費の仕方も変えた。数値エスケープは桁数ぶん読み進めるので，
/// **escv 自身が読取り位置を進め，呼ぶ側は進めない**。
///
/// @section diff9 第 3 部の 3 からの変更点
///   構造体の返却   struct P mk(int x) { ... return p; }
///
/// @section strret 構造体の返却はデータスタックで受け渡す
/// 返却値の受渡しはこれまで 1 語だった。構造体は複数語なので，
/// **引数と同じくデータスタックに積んで返す**。
///
///   呼ばれた側: 語 k-1 ... 語 0 の順に積み，最後に従来どおり 1 語の
///               返却値 (中身は使わない) を積んで戻る
///   x9 から上へ: [使わない 1 語][語 0][語 1] ... [語 k-1]
///   呼んだ側:   呼出しの直後に語をフレームの一時領域へ引き取り，
///               1 + k 語ぶん x9 を戻す
///
/// 引き取りを出力段で行うのは，**x9 が IR から触れない** ためである。
/// IR は「フレーム上の位置」と「値番号」しか知らないので，データスタックの
/// 上を読む操作を組み立てられない。そこで呼出し命令に側情報 iret を持たせ，
/// 出力段が呼出しの直後に複写を埋め込む。
///
/// @section diff8 第 3 部の 2 からの変更点 (第 3 部の 3 で入ったもの)
///   局所の構造体変数   struct S s;
///   入れ子のメンバ     struct A { struct B b; };
///   構造体の代入       a = b;      (これまでは先頭 4 バイトしか複写しなかった)
///   構造体の値渡し     f(s);       (これまでは先頭 4 バイトしか積まなかった)
///
/// @section struval 構造体の値は「アドレス」で持ち回る
/// 式の値は 1 語 (レジスタ 1 本) に載るという前提で IR を組んでいる。
/// 構造体はそこに載らないので，**構造体型の式の値はその実体のアドレス**
/// とし，rv() で読み出さないことにした。配列の退化と同じ考え方だが，
/// 型は構造体のままにしておく (メンバ参照がオフセットを足せるように)。
///
/// この規則ひとつで，代入も値渡しも「アドレスから語単位で複写する」
/// 形に揃う。複写は IR の段で語ごとの load/store に展開するので，
/// 出力段・dce・レジスタ割付けには手を入れずに済む。
///
/// @section strarg 構造体の実引数は複数語を占める
/// 引数はデータスタックに 1 語ずつ積み，呼ばれた側がフレームへ移す。
/// 構造体は ceil(大きさ/4) 語を占め，語 0 から順に積む。呼ばれた側は
/// 後ろから取り出すので，フレーム上の並びは元の記憶域の並びと一致する。
/// つまり **仮引数の構造体は，ふつうの局所の構造体変数と同じ形** になり，
/// メンバ参照は何も変えずに動く。
///
/// このため引数の個数 cna は「語数」を数える。呼ぶ側も語数で数えるので
/// 個数の検査はそのまま働く。
///
/// @section diff7 第 3 部の 1 からの変更点 (第 3 部の 2 で入ったもの)
///   可変長引数   int f(int n, ...);  va_list / va_start / va_arg
///
/// @section vararg 可変長引数を「積む順序」で解く
/// 引数はデータスタック (x9) 経由で渡す。呼ばれた側のプロローグは
/// 名前つき引数を後ろから 1 語ずつ取り出してフレームへ移す。素直に
/// 左から積むと，余分な実引数が最後に積まれて一番上に来てしまい，
/// プロローグがそれを名前つき引数として取り出してしまう。
///
/// そこで **可変部を逆順に先に積み，名前つきを後から順に積む**。
///
///   積む順:  arg[n-1] ... arg[k]  arg[0] arg[1] ... arg[k-1]   (k = 名前つきの個数)
///   x9 から: arg[k-1] ... arg[0]  arg[k] arg[k+1] ... arg[n-1]
///
/// こうすればプロローグは 1 命令も変わらず，名前つきを取り出し終えた
/// 時点の x9 がそのまま可変部の先頭を指す。その値を隠しローカル
/// __va_ptr へ書いておけば，va_start はただの代入になる。
/// 余分に積んだぶんは呼んだ側が返却値と一緒に捨てる。
///
/// @section diff6 第 2 部の 5 からの変更点 (第 3 部の 1 で入ったもの)
///   初期化子   int a = 5;  int t[] = {1,2,3};  char s[] = "abc";
///              char *p = "abc";  int *q = &g;  局所変数の初期化
///
/// @section datainit 初期値を持つ大域変数をどこに置くか
/// 普通の C 処理系は .data セクションを設ける。本実装では **.text の中に
/// そのまま置く**。ベアメタルで MMU が無く，像はフラットに読み込まれるので
/// セクションの保護属性に意味が無いためである。こうするとオブジェクト形式も
/// リンカも変えずに済み，データの中のポインタ (char *p = "abc") も
/// 既存の .rela.text の R_RISCV_32 でそのまま解ける。
/// セクションを分けるのは，実行環境を移す Stage 12 の課題とする。
///
/// @section diff5 第 2 部の 4 からの変更点 (第 2 部の 5 で入ったもの)
///   関数ポインタ    int (*f)(int);  値としての関数名，間接呼出し
///   static          ELF のローカルシンボルとして出し，翻訳単位内で閉じる
///
/// @section fnptr 関数ポインタ
/// 型の表に関数型を足した。配列型と同じ考え方で，基底の t_fn 番以降を
/// 関数型の表の添字に割り当て，返却型を持たせる。式に現れるのは常に
/// 「関数へのポインタ」なので，型としては (1 << 16) | (t_fn + k) になる。
///
/// 呼出しは 2 通りになる。記号を直接呼ぶ従来の形 (jal + JAL 再配置) と，
/// 値を呼ぶ形 (jalr) である。後者のために IR へ命令を 1 つ足した。
///
/// @section staticlink static のリンケージ
/// リンカ側の変更は要らなかった。ld は既に
///   - 大域シンボルの収集を sh_info (最初の非ローカル) から始める
///   - 再配置の解決を st_shndx で行い，1 / 2 なら自分のオブジェクト内で解く
/// という作りになっている。したがって **cc が static の記号を
/// ローカル領域へ並べ替え，sh_info を正しく書くだけ** で閉じる。
///
/// @section diff4 第 2 部の 3 からの変更点 (第 2 部の 4 で入ったもの)
///   unsigned / signed / short / long
///
/// **本段で初めてコード生成規則が変わる。** 型ごとに
///   - ロードの幅と拡張   lb / lbu / lh / lhu / lw
///   - ストアの幅         sb / sh / sw
///   - 除算・剰余・右シフト・大小比較の符号つき / 符号なし
/// を選ぶ必要があるためである。これまでの各部は「コード生成に触れない」
/// ことをブートストラップ 1 段目と正本の一致で保証してきたが，本段では
/// その保証は使えない。固定点 (B2 == B3) は従来どおり成り立つ。
///
/// @section charsign char の符号
/// 素の char は **符号なし** とする。C は処理系定義としており，sc の
/// 時代から lbu で読んできたのでその挙動を引き継ぐ。符号つきが要るときは
/// signed char と書く。
///
/// @section promote 整数の格上げ
/// char と short は算術の中で int へ格上げされる。int がその全ての値を
/// 表せるので，unsigned char / unsigned short も **符号つき int** になる。
/// したがって演算が符号なしになるのは **unsigned int (= unsigned long) の
/// ときだけ** である。ロードの時点で 32 bit へ拡張してあるので，格上げに
/// 命令は要らず，型を付け替えるだけで済む。
///
/// @section diff2 第 2 部の 2 からの変更点
///   識別子の長さ 31   C89 が内部識別子に要求する長さ (それまでは 15)
///   多次元配列        int a[3][4] と，その添字・sizeof・引数への退化
///
/// 多次元配列のために，型の表現に **配列型** を足した。これまでの型は
/// 「ポインタの深さ << 16 | 基底」という平坦な表現で，基底は char / int /
/// 構造体 / void しか無かった。配列は「型から型を作る」ので平坦には表せない。
/// 基底の 1024 番以降を配列型の表の添字に割り当て，要素型と要素数を持たせる。
///
/// IR の命令種別・コード生成規則・ABI・出力形式は変えていない。添字計算は
/// もともと「要素の大きさで掛ける」形なので，要素が配列でもそのまま働く。
///
/// @section diff 第 2 部の 1 からの変更点 (第 2 部の 2 で入ったもの)
/// 追加したのは宣言の構文だけで，IR の命令種別・コード生成規則・ABI・
/// 出力形式は一切変えていない。
///
///   関数プロトタイプ    int f(int, char *);  定義せずに宣言だけする
///   extern             他の翻訳単位にある変数・関数を参照する
///   static             受理する (リンケージは変えない。下の制限を参照)
///   ブロック内宣言      { で始まる複文の先頭に宣言を置ける
///
/// これでヘッダが「宣言を共有する場所」になる。Stage 9 の時点では
/// ヘッダに書けるのはマクロと型定義だけで，関数や変数の宣言は書けなかった。
///
/// @section blockdecl ブロック内宣言とフレームの割付け
/// 隠しスロット (&& / || / ?: / switch が使う) はフレームの局所変数領域の
/// 直後から取っていた。ブロック内宣言があると，本体の解析中に局所変数が
/// 増えるので，この 2 つが同じ領域を奪い合う。そこで **隠しスロットも
/// 局所変数と同じ割付けポインタ (cloff) から取る** ことにした。
/// どちらも「フレーム上の 1 語」であり，区別する理由はもともと無い。
///
/// @section written_in 何で書かれているか
/// 第 2 部の 1 のコンパイラ (cc10b) でビルドするので，そこまでに実装した
/// 構文 —— for / switch / ++ / 複合代入 / ?: / sizeof / typedef / enum /
/// union —— をここでは使ってよい。
///
/// @section pipeline 処理の流れ
/// 入力 1 本を読み切り，関数単位で以下を回してオブジェクトを組み立てる。
///
///   ソース -> 字句 -> 再帰下降構文解析 -> IR 構築
///          -> fold (定数畳み込み) -> dce (不要コード削除)
///          -> regalloc (線形走査割付け) -> emitfn (命令出力)
///
/// @section lvalue 左辺値の扱い (elv / ety)
/// 式の解析結果は 2 つの大域で伝える。ety はその式の型，elv は 1 なら
/// 「返した値番号は値そのものではなくアドレス」を意味する。変数を読んだ
/// 直後は elv = 1 にしておき，値が要る場面で rv() を通してロードを発行する。
/// この形にしておくと，代入の左辺・&・複合代入・++ が「アドレスを 1 度だけ
/// 求めて 2 回使う」を素直に書ける (docs/stage010-c89.md 4.2)。
///
/// @section why_sc なぜ sc 言語で書かれているか
/// 自分が実装する機能を自分の記述には使えない。cc.sc は前段の cc8 が
/// コンパイルできる範囲，すなわち sc 言語 (for も switch も ++ も無い) で
/// 書く必要がある。ループの途中脱出は完了フラグで表し，添字は i = i + 1 と
/// 書くのはこのためである。第 2 部以降は本段のコンパイラでビルドされるので，
/// ここで実装した構文が使えるようになる。
///
/// @section limits sc 言語側の制約に由来する書き方
/// sc には構造体配列・列挙型・定数構文がないため，
/// - 表はすべて添字を共有する「並行配列」で表現する
/// - 種別定数は大域変数として init() で設定する
/// - 「見つからない」は -1 で表す (0 は正当な添字なので使えない)

// ---- 領域: 入出力バッファと記号表 ----
// 記号表はいずれも並行配列で，同じ添字 e が 1 エントリを指す。

char src[262144];         ///< 入力ソース全体 (EOT 0x04 まで読み込む)
char ob[524288];          ///< 生成バイナリ。後埋め (backpatch) するため一旦ここに溜める

char gname[65536];        ///< 大域記号の名前 (32 バイト固定スロット x 2048)
int gkind[2048];          ///< 種別: 0 = 変数, 1 = 関数
int gty[2048];            ///< 型 (変数は自身の型，関数は返却型)
int gval[2048];           ///< 変数: 絶対アドレス / 関数: 定義済みならコード位置，未定義なら未解決呼出しリストの先頭
int gdef[2048];           ///< 1 = 定義済み。0 のまま入力が終われば未解決の前方参照 (エラー 2)
int garr[2048];           ///< 1 = 配列。式中では先頭要素へのポインタに退化する
int gna[2048];            ///< 引数個数。-1 = 未知 (前方参照で個数がまだ判らない)
int gsz[2048];            ///< 大域変数の大きさ (バイト)。ELF シンボルの st_size になる
int gsta[2048];           ///< 1 = static。ELF のローカルシンボルとして出す
int gtxt[2048];           ///< 1 = 初期値を持ち，実体が .text にある
int gvar[2048];           ///< 1 = 可変長引数を取る関数 (gna は名前つきの個数)
int gused[2048];          ///< 1 = 個数が判らないまま呼出しを出した (前方参照)
int gidx[2048];           ///< 大域記号 -> ELF シンボル番号
int nsta;                 ///< ローカル側へ寄せた static 記号の数
int gcnt;                 ///< 大域記号の登録数

char lname[8192];         ///< ローカル記号の名前 (32 バイト x 256)。関数ごとに作り直す
int lty[256];             ///< 型
int loff[256];            ///< フレームポインタ x8 からのオフセット
int larr[256];            ///< 1 = 配列
int lsz[256];             ///< 大きさ (バイト)。sizeof が配列全体を返すために要る
int lcnt;                 ///< ローカル記号の登録数

char sname[8192];         ///< 構造体名 (32 バイト x 256)
int ssize[256];           ///< 構造体のサイズ (4 バイト境界へ切り上げ済み)
int sunion[256];          ///< 1 = union。メンバの位置を 0 に固定し，大きさは最大値
int scnt;                 ///< 構造体の登録数。union も同じ表に入る

// typedef は「名前 -> 型」の対応にすぎない。型そのものを増やすわけではないので
// 別表を 1 つ持てば済む。
char tdname[8192];        ///< typedef 名 (32 バイト x 256)
int tdty[256];            ///< 対応する型
int tdcnt;                ///< 登録数

// enum 定数も「名前 -> 値」の対応にすぎない。型は int である。
// タグ (enum e { ... } の e) は型の区別を生まないので表に持たない。
char ecname[8192];        ///< 列挙定数の名前 (32 バイト x 256)
int ecval[256];           ///< その値
int eccnt;                ///< 登録数

// 配列型。これまでの型は「ポインタの深さ << 16 | 基底」という平坦な表現で，
// 基底は char / int / 構造体 / void しか無かった。多次元配列は「型から型を
// 作る」ので平坦には表せない。基底の t_arr 番以降をこの表の添字に割り当てる。
int aelem[512];           ///< 要素の型
int acnt[512];            ///< 要素数
int arrcnt;               ///< 登録数

// 関数型。配列型と同じ考え方で，基底の t_fn 番以降を添字に割り当てる。
// 仮引数の型までは持たない (呼出しの検査は個数だけで行う)。
int frty[256];            ///< 返却型
int fncnt;                ///< 登録数

char mname[65536];        ///< メンバ名 (32 バイト x 2048)。全構造体のメンバを 1 本の表に持つ
int msid[2048];           ///< 所属する構造体の番号。探索はこれで絞り込む
int mty[2048];            ///< メンバの型
int moff[2048];           ///< 構造体先頭からのオフセット
int marr[2048];           ///< 1 = 配列メンバ
int msz[2048];            ///< 大きさ (バイト)。sizeof 用
int mcnt;                 ///< メンバの登録数

char tname[32];           ///< 直近に読んだ識別子。記号表の探索はすべてこれを鍵にする
char snam[32];            ///< struct 名の退避先 (tname はメンバ名の解析で上書きされるため)
char sbuf[256];           ///< 文字列リテラルの組立てバッファ
int slen;                 ///< sbuf の有効長

int pos;                  ///< src 内の読取り位置
int tok;                  ///< 現在のトークン種別
int tval;                 ///< 現在のトークンの値 (数値・文字リテラル)
int outp;                 ///< ob への書込み位置 (= 生成コードのオフセット)
int bssp;                 ///< .bss の割付けポインタ (オブジェクト内オフセット。0 から上向き)

// ---- ELF 出力 ----
// ob は .text の内容だけを保持し，ELF ファイル全体は eb に組み立てて出力する。
// 再配置は「.text 内の位置」「対象シンボル」「種別」「加数」の 4 本の並行配列。
// 対象シンボルの符号化: 0 以上なら大域記号の番号 e，
// 負なら文字列リテラル用のローカルシンボル k を -1 - k で表す。
char eb[1048576];         ///< ELF ファイル全体の組立てバッファ
int ep;                   ///< eb への書込み位置
int rof[8192];            ///< 再配置: .text 内オフセット
int rsy[8192];            ///< 再配置: 対象シンボル (上記の符号化)
int rty[8192];            ///< 再配置: 種別 (R_RISCV_*)
int rad[8192];            ///< 再配置: 加数
int rcnt;                 ///< 再配置の件数
int lsoff[1024];          ///< 文字列リテラルのローカルシンボル: .text 内オフセット
int nlsym;                ///< ローカルシンボルの数
char stt[65536];          ///< .strtab の内容
int stp;                  ///< stt の有効長
int r_32; int r_jal; int r_hi20; int r_lo12i;   ///< 再配置種別の番号

int ety;                  ///< 直前に解析した式の型
int elv;                  ///< 1 = その式は左辺値 (値ではなくアドレスが手元にある)
int earr;                 ///< 1 = その式は配列オブジェクト (sizeof のためだけに要る)
int esz;                  ///< earr が 1 のときの配列全体のバイト数
int erv;                  ///< 1 = その構造体は呼出しの返却値 (一時領域にある)。
                          ///< アドレスはあるが代入先にはできない

// ---- 制御構造の飛び先 ----
// break は反復と switch の両方が受け，continue は反復だけが受ける。
// 2 本に分けておくと「switch の中の continue は外側のループへ抜ける」が
// 自然に成り立つ (switch は conl を積まない)。
int brkl[64];             ///< break の飛び先ラベル (積み)
int brkn;                 ///< その深さ
int conl[64];             ///< continue の飛び先ラベル (積み)
int conn;                 ///< その深さ

// switch は本体を先に，振分けを後に出す。本体を読みながらここへ控える。
int swval[256];           ///< case のラベル値
int swlab[256];           ///< 対応する IR ラベル番号
int swn;                  ///< 控えた数 (入れ子の switch は同じ配列を積んで使う)
int swdep;                ///< switch の入れ子の深さ。0 なら case / default は誤り
int swdflt;               ///< 現在の switch の default のラベル。-1 = 無し

// goto のラベル表 (関数ごとに作り直す)
char glname[2048];        ///< ラベル名 (32 バイトスロット x 64)
int gllab[64];            ///< 対応する IR ラベル番号
int gldef[64];            ///< 1 = 定義済み (「名前:」が現れた)
int glcnt;                ///< 登録数

// 字句解析器の退避先。1 トークン先を覗いて戻すために使う
// (「x:」がラベルか式か，「(」の次が型かキャストか)
int svpos; int svtok; int svtval;
char svname[32];
int cloff;                ///< 解析中の関数のローカル割付けポインタ
int cmax;                 ///< cloff の最大値。ブロックを抜けると cloff は戻るが，
                          ///< フレームの大きさは最大値で決まる
int cext;                 ///< 1 = この宣言に extern が付いている
int cstat;                ///< 1 = この宣言に static が付いている
int cna;                  ///< 解析中の関数の引数個数 (名前つきのみ)
int cvarg;                ///< 1 = 解析中の関数が可変長引数を取る
int cvaoff;               ///< 可変部の先頭を保持する隠しローカルの位置 (-1 = 無し)
int cretty;               ///< 解析中の関数の返却型 (return が構造体かを見る)
int cty;                  ///< 解析中の宣言の型
int mainok;               ///< 1 = main を定義済み
int mainoff;              ///< main のコード位置 (ランタイム前置部から呼ぶために保持)
int *wp;                  ///< char 配列 ob へ語単位で書くための作業ポインタ

// ---- IR (関数単位。関数を 1 つ処理するたびに作り直す) ----
//
// 3 番地コードの列。命令 i は高々 1 個の値を定義し，その値は「定義した命令の
// 番号 i」そのもので参照する。つまり値番号と命令番号が一致する。式の一時値は
// 構文上ちょうど一度しか定義されないので，この IR は φ 関数を持たない SSA に
// なっている (制御フローを跨ぐ値は変数と同じくメモリ経由にしてあるため，
// 合流点で複数定義が出会うことがない。&& / || が隠しスロットを使うのはこの
// 性質を保つためである)。
//
// iop[i] が c_bin 以上なら二項演算で，演算種別は iop[i] - c_bin。
// ia/ib の意味は命令種別ごとに異なる:
//   CONST      ia = 即値
//   LADDR      ia = フレームオフセット      GADDR ia = 絶対アドレス
//   GSTR       ia = 文字列プール内オフセット
//   LOADW/B    ia = アドレスの値番号
//   STOREW/B   ia = アドレスの値番号, ib = 格納する値番号
//   NEG/NOT    ia = 値番号
//   ARG/RET    ia = 値番号
//   CALL       ia = 記号番号, ib = 実引数の個数
//   LABEL/JMP  ia = ラベル番号
//   BZ/BNZ     ia = 条件の値番号, ib = 飛び先ラベル
//   BIN        ia, ib = 両オペランドの値番号

int iop[8192];            ///< 命令種別 (c_* のいずれか。c_bin 以上は二項演算)
int ia[8192];             ///< 第 1 オペランド (意味は命令種別による。上表参照)
int ib[8192];             ///< 第 2 オペランド (同上)
int icnt;                 ///< 命令数

int lastu[8192];          ///< 値が最後に使われる命令位置。-1 = 一度も使われない
int vreg[8192];           ///< 割付け結果: >= 0 レジスタ番号 / -1 未割付 / -2-n スピルスロット n
int live[8192];           ///< dce の結果: 1 = 生存 (出力する)
int iret[8192];           ///< CALL の側情報: 構造体を返す呼出しの引取り先
                          ///< (フレームオフセット。0 = 構造体を返さない)

int labpos[1024];         ///< ラベル番号 -> 出力オフセット。-1 = 未確定 (まだ現れていない)
int labcnt;               ///< ラベル数
int lfix[2048];           ///< 前方ラベル参照の後埋め: 命令を書いた出力位置
int lflab[2048];          ///< 同上: 参照先のラベル番号
int lfixn;                ///< 後埋め待ちの件数

char spool[8192];         ///< 文字列リテラルの実体 (関数単位。本体の後ろへ出力する)
int spcnt;                ///< spool の有効長
int spfix[256];           ///< 文字列アドレスの後埋め: li を書いた出力位置 (lui 側)
int spofs[256];           ///< 同上: spool 内オフセット
int spfn;                 ///< 後埋め待ちの件数
int gspsym[256];          ///< 大域初期化子の文字列: 予約したローカルシンボル番号
int gspofs[256];          ///< 同上: spool 内オフセット
int gspn;                 ///< 同上の件数

int hcnt;                 ///< 隠しスロットの数。&& / || の結果を一旦メモリに置くために使う
int nspill;               ///< スピルスロットの数

int rheld[32];            ///< 割付け作業用: レジスタ -> 現在保持している値番号 (-1 = 空き)
int rused[32];            ///< 1 = この関数で使った。プロローグで退避する対象になる

// フレームの配置 (x8 = フレームポインタ基準，低位から):
//   [0] 戻り先 ra  [4] 旧 x8  [8..] 引数  [..] ローカル  [spbase..] スピル  [svbase..] 退避レジスタ
int fnf;                  ///< フレーム総サイズ
int spbase;               ///< スピル領域の先頭オフセット
int svbase;               ///< レジスタ退避領域の先頭オフセット

// ---- トークン種別・IR 命令種別 ----
//
// sc に定数構文がないため大域変数として持ち，init() で値を入れる。
// t_* トークンの大分類, k_* 予約語, o_* 演算子・記号,
// c_* IR 命令種別, b_* 二項演算の種別 (c_bin からの差分)。

int eot;
int t_eof; int t_num; int t_str; int t_id;
int k_int; int k_char; int k_struct; int k_if; int k_else; int k_while; int k_return;
int k_for; int k_do; int k_switch; int k_case; int k_default;
int k_break; int k_continue; int k_goto; int k_sizeof;
int k_enum; int k_union; int k_typedef; int k_const; int k_volatile; int k_void;
int k_extern; int k_static;
int k_unsigned; int k_signed; int k_short; int k_long;
int t_void;               ///< void の型番号 (構造体の 2..257 と離した値)
int t_arr;                ///< 配列型の基底番号の起点 (t_arr + k が配列型 k)
int t_fn;                 ///< 関数型の基底番号の起点 (t_fn + k が関数型 k)
int t_schar;              ///< signed char (素の char は符号なしなので別番号)
int t_short; int t_ushort; int t_uint;
int o_asn; int o_lt; int o_gt; int o_add; int o_sub; int o_mul; int o_div; int o_mod;
int o_amp; int o_or; int o_xor; int o_not; int o_lp; int o_rp; int o_lb; int o_rb;
int o_lc; int o_rc; int o_semi; int o_comma; int o_dot;
int o_eq; int o_ne; int o_le; int o_ge; int o_shl; int o_shr; int o_aa; int o_oo; int o_arrow;
int o_que; int o_col; int o_inc; int o_dec; int o_ellip;
// 複合代入。o_asnb を足すと対応する二項演算の b_* になるよう並べる
int o_asnb;
int a_add; int a_sub; int a_mul; int a_div; int a_rem;
int a_and; int a_or; int a_xor; int a_shl; int a_shr;
int c_const; int c_laddr; int c_gaddr; int c_gstr;
int c_loadw; int c_loadb; int c_stw; int c_stb;
int c_neg; int c_not; int c_arg; int c_call; int c_ret;
int c_lab; int c_jmp; int c_bz; int c_bnz;
int c_loadbs; int c_loadh; int c_loadhu; int c_sth; int c_calli;
int c_bin;
int b_add; int b_sub; int b_mul; int b_div; int b_rem;
int b_and; int b_or; int b_xor; int b_sll; int b_srl;
int b_slt; int b_sgt; int b_sle; int b_sge; int b_seq; int b_sne;
int b_udiv; int b_urem; int b_sra;
int b_ult; int b_ugt; int b_ule; int b_uge;

int init() {
  eot = 4;
  t_eof = 0; t_num = 1; t_str = 2; t_id = 3;
  k_int = 10; k_char = 11; k_struct = 12; k_if = 13; k_else = 14; k_while = 15; k_return = 16;
  k_for = 17; k_do = 18; k_switch = 19; k_case = 20; k_default = 21;
  k_break = 22; k_continue = 23; k_goto = 24; k_sizeof = 25;
  // 26..29 は空いているが，o_* が 30 から始まるので衝突を避けて 80 台に置く
  // (複合代入が 70..79 を使っている)
  k_enum = 80; k_union = 81; k_typedef = 82; k_const = 83; k_volatile = 84;
  k_void = 85; k_extern = 86; k_static = 87;
  k_unsigned = 88; k_signed = 89; k_short = 90; k_long = 91;
  o_asn = 30; o_lt = 31; o_gt = 32; o_add = 33; o_sub = 34; o_mul = 35; o_div = 36; o_mod = 37;
  o_amp = 38; o_or = 39; o_xor = 40; o_not = 41; o_lp = 42; o_rp = 43; o_lb = 44; o_rb = 45;
  o_lc = 46; o_rc = 47; o_semi = 48; o_comma = 49; o_dot = 50;
  o_eq = 51; o_ne = 52; o_le = 53; o_ge = 54; o_shl = 55; o_shr = 56; o_aa = 57; o_oo = 58; o_arrow = 59;
  o_que = 60; o_col = 61; o_inc = 62; o_dec = 63; o_ellip = 64;
  // 複合代入は 70 + b_* に置く。o_asnb を引けば二項演算の種別になる
  o_asnb = 70;
  a_add = 70; a_sub = 71; a_mul = 72; a_div = 73; a_rem = 74;
  a_and = 75; a_or = 76; a_xor = 77; a_shl = 78; a_shr = 79;
  c_const = 1; c_laddr = 2; c_gaddr = 3; c_gstr = 4;
  c_loadw = 5; c_loadb = 6; c_stw = 7; c_stb = 8;
  c_neg = 9; c_not = 10; c_arg = 11; c_call = 12; c_ret = 13;
  c_lab = 14; c_jmp = 15; c_bz = 16; c_bnz = 17;
  // 幅と符号のあるロード・ストア。c_bin は二項演算の起点なので後ろへずらす
  c_loadbs = 18; c_loadh = 19; c_loadhu = 20; c_sth = 21;
  c_calli = 22;                    // 値を呼ぶ (jalr)。ia = 呼出し先の値番号
  c_bin = 23;
  b_add = 0; b_sub = 1; b_mul = 2; b_div = 3; b_rem = 4;
  b_and = 5; b_or = 6; b_xor = 7; b_sll = 8; b_srl = 9;
  b_slt = 10; b_sgt = 11; b_sle = 12; b_sge = 13; b_seq = 14; b_sne = 15;
  b_udiv = 16; b_urem = 17; b_sra = 18;
  b_ult = 19; b_ugt = 20; b_ule = 21; b_uge = 22;
  // RISC-V ELF psABI の再配置種別
  r_32 = 1; r_jal = 17; r_hi20 = 26; r_lo12i = 27;
  t_void = 300;
  t_arr = 1024;
  t_schar = 301; t_short = 302; t_ushort = 303; t_uint = 304;
  t_fn = 2048;
  return 0;
}

// ---- 名前操作 ----
// 記号表の名前は 32 バイト固定スロットに 0 詰めで格納する。可変長にすると
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
/// @brief 記号表の鍵 tname に，字句解析を通さずに名前を置く。
/// @param s 置く名前 (32 バイト未満)
/// @return 常に 0
/// @note コンパイラが自前で作るローカル (可変長引数の __va_ptr) を
///       登録するために使う。lnew() が tname を鍵にするためである。
int setname(char *s) {
  int i;
  i = 0;
  while (s[i]) { tname[i] = s[i]; i = i + 1; }
  while (i < 32) { tname[i] = 0; i = i + 1; }
  return 0;
}
/// @brief 名前スロットの複写 (常に 32 バイト固定)。
/// @param d 複写先スロット
/// @param s 複写元スロット
/// @return 常に 0
int copyn(char *d, char *s) {
  int i;
  i = 0;
  while (i < 32) { d[i] = s[i]; i = i + 1; }
  return 0;
}

// ---- 字句解析 (scc と同一) ----
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
/// @note sc では英小文字と _ に限っていたが，enum 定数や typedef 名は
///       大文字が慣習であり，取り込む外部の C も大文字を使う。ここで解除する。
///       予約語はすべて小文字なので，大文字を許しても衝突しない。
int isidh(int c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  return c == '_';
}
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

/// @brief 16 進数字か。
int isxd(int c) {
  if (isdig(c)) return 1;
  if (c >= 'a' && c <= 'f') return 1;
  return c >= 'A' && c <= 'F';
}

/// @brief 16 進数字の値。
int xdv(int c) {
  if (isdig(c)) return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

/// @brief エスケープを 1 個読み，値を返す。
/// @return 文字コード。読取り位置はエスケープの直後まで進む
/// @note 逆斜線は読み終えているものとする。数値エスケープが桁数ぶん
///       読み進めるので，**消費は自分で行い呼ぶ側は進めない**。
///       C89 7.1.3 の単純エスケープ 9 種と，8 進 (最大 3 桁)・16 進を扱う。
int escv() {
  int c; int v; int n;
  c = getch();
  adv();
  if (c == 'n') return 10;
  if (c == 't') return 9;
  if (c == 'r') return 13;
  if (c == 'f') return 12;
  if (c == 'v') return 11;
  if (c == 'a') return 7;
  if (c == 'b') return 8;
  if (c == 92) return 92;
  if (c == 39) return 39;
  if (c == 34) return 34;
  if (c == 63) return 63;
  if (c == 'x') {
    // 16 進。桁数の制限は無いが 1 バイトへ切り詰める
    v = 0;
    n = 0;
    while (isxd(getch())) { v = v * 16 + xdv(getch()); adv(); n = n + 1; }
    if (n == 0) exit(1);
    return v & 255;
  }
  if (c >= '0' && c <= '7') {
    // 8 進。最大 3 桁 (\0 もここで扱う)
    v = c - '0';
    n = 1;
    while (n < 3 && getch() >= '0' && getch() <= '7') {
      v = v * 8 + (getch() - '0');
      adv();
      n = n + 1;
    }
    return v & 255;
  }
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
    if (n == 31) exit(1);
    tname[n] = getch();
    n = n + 1;
    adv();
  }
  while (n < 32) { tname[n] = 0; n = n + 1; }
  if (streq(tname, "int")) { tok = k_int; return 0; }
  if (streq(tname, "char")) { tok = k_char; return 0; }
  if (streq(tname, "struct")) { tok = k_struct; return 0; }
  if (streq(tname, "if")) { tok = k_if; return 0; }
  if (streq(tname, "else")) { tok = k_else; return 0; }
  if (streq(tname, "while")) { tok = k_while; return 0; }
  if (streq(tname, "return")) { tok = k_return; return 0; }
  if (streq(tname, "for")) { tok = k_for; return 0; }
  if (streq(tname, "do")) { tok = k_do; return 0; }
  if (streq(tname, "switch")) { tok = k_switch; return 0; }
  if (streq(tname, "case")) { tok = k_case; return 0; }
  if (streq(tname, "default")) { tok = k_default; return 0; }
  if (streq(tname, "break")) { tok = k_break; return 0; }
  if (streq(tname, "continue")) { tok = k_continue; return 0; }
  if (streq(tname, "goto")) { tok = k_goto; return 0; }
  if (streq(tname, "sizeof")) { tok = k_sizeof; return 0; }
  if (streq(tname, "enum")) { tok = k_enum; return 0; }
  if (streq(tname, "union")) { tok = k_union; return 0; }
  if (streq(tname, "typedef")) { tok = k_typedef; return 0; }
  if (streq(tname, "const")) { tok = k_const; return 0; }
  if (streq(tname, "volatile")) { tok = k_volatile; return 0; }
  if (streq(tname, "void")) { tok = k_void; return 0; }
  if (streq(tname, "extern")) { tok = k_extern; return 0; }
  if (streq(tname, "static")) { tok = k_static; return 0; }
  if (streq(tname, "unsigned")) { tok = k_unsigned; return 0; }
  if (streq(tname, "signed")) { tok = k_signed; return 0; }
  if (streq(tname, "short")) { tok = k_short; return 0; }
  if (streq(tname, "long")) { tok = k_long; return 0; }
  tok = t_id;
  return 0;
}

/// @brief 文字リテラルを読み，その文字コードを tval へ入れる。
/// @return 常に 0
int lexchr() {
  adv();
  if (getch() == eot) exit(1);
  if (getch() == 92) { adv(); tval = escv(); }
  else { tval = getch(); adv(); }
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
    if (getch() == 92) { adv(); c = escv(); }
    else { c = getch(); adv(); }
    sbuf[slen] = c;
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
    if (getch() == '<') {
      adv();
      if (getch() == '=') { adv(); tok = a_shl; return 0; }
      tok = o_shl;
      return 0;
    }
    tok = o_lt;
    return 0;
  }
  if (c == '>') {
    if (getch() == '=') { adv(); tok = o_ge; return 0; }
    if (getch() == '>') {
      adv();
      if (getch() == '=') { adv(); tok = a_shr; return 0; }
      tok = o_shr;
      return 0;
    }
    tok = o_gt;
    return 0;
  }
  if (c == '&') {
    if (getch() == '&') { adv(); tok = o_aa; return 0; }
    if (getch() == '=') { adv(); tok = a_and; return 0; }
    tok = o_amp;
    return 0;
  }
  if (c == '|') {
    if (getch() == '|') { adv(); tok = o_oo; return 0; }
    if (getch() == '=') { adv(); tok = a_or; return 0; }
    tok = o_or;
    return 0;
  }
  if (c == '-') {
    if (getch() == '>') { adv(); tok = o_arrow; return 0; }
    if (getch() == '-') { adv(); tok = o_dec; return 0; }
    if (getch() == '=') { adv(); tok = a_sub; return 0; }
    tok = o_sub;
    return 0;
  }
  if (c == '+') {
    if (getch() == '+') { adv(); tok = o_inc; return 0; }
    if (getch() == '=') { adv(); tok = a_add; return 0; }
    tok = o_add;
    return 0;
  }
  if (c == '*') {
    if (getch() == '=') { adv(); tok = a_mul; return 0; }
    tok = o_mul;
    return 0;
  }
  if (c == '/') {
    if (getch() == '=') { adv(); tok = a_div; return 0; }
    tok = o_div;
    return 0;
  }
  if (c == '%') {
    if (getch() == '=') { adv(); tok = a_rem; return 0; }
    tok = o_mod;
    return 0;
  }
  if (c == '^') {
    if (getch() == '=') { adv(); tok = a_xor; return 0; }
    tok = o_xor;
    return 0;
  }
  if (c == '?') { tok = o_que; return 0; }
  if (c == ':') { tok = o_col; return 0; }
  if (c == '(') { tok = o_lp; return 0; }
  if (c == ')') { tok = o_rp; return 0; }
  if (c == '[') { tok = o_lb; return 0; }
  if (c == ']') { tok = o_rb; return 0; }
  if (c == '{') { tok = o_lc; return 0; }
  if (c == '}') { tok = o_rc; return 0; }
  if (c == ';') { tok = o_semi; return 0; }
  if (c == ',') { tok = o_comma; return 0; }
  if (c == '.') {
    // '...' は 1 個のトークン。'.' の後にもう 1 個 '.' があれば
    // 3 個目まで揃っていなければならない (C に '..' は無い)
    if (getch() == '.') {
      adv();
      if (getch() != '.') exit(1);
      adv();
      tok = o_ellip;
      return 0;
    }
    tok = o_dot;
    return 0;
  }
  exit(1);
  return 0;
}

/// @brief 字句解析器の状態を退避する。
/// @return 常に 0
/// @note 1 トークン先を覗いてから戻すために使う。入力全体が src にあるので，
///       巻き戻しは読取り位置の代入で足りる。退避は 1 段だけで，
///       lsave と lrest のあいだで再び lsave を呼んではならない。
int lsave() {
  svpos = pos; svtok = tok; svtval = tval;
  copyn(svname, tname);
  return 0;
}
/// @brief lsave で退避した状態へ戻す。
/// @return 常に 0
int lrest() {
  pos = svpos; tok = svtok; tval = svtval;
  copyn(tname, svname);
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

// ---- 命令語の組立て ----
// RV32I の即値は形式ごとに命令語中へ散らばって配置される。ここではその
// 詰め替えだけを行い，opcode/funct を含む「素の語」は呼び手が base で渡す。

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
/// @note 表現範囲は ±4KiB しかない。occ が条件分岐をこの形式で直接
///       出さないのはそのため (lrefj の @note を参照)。
int benc(int rel) {
  return (((rel >> 12) & 1) << 31) | (((rel >> 5) & 63) << 25)
       | (((rel >> 1) & 15) << 8) | (((rel >> 11) & 1) << 7);
}

/// @brief R 形式 (レジスタ 3 つ) の命令語を組み立てる。
/// @param base opcode と funct3/funct7 を含む素の語
/// @param rd 書込み先レジスタ番号
/// @param rs1 第 1 入力レジスタ番号
/// @param rs2 第 2 入力レジスタ番号
/// @return 完成した命令語
int rw3(int base, int rd, int rs1, int rs2) {
  return base | (rd << 7) | (rs1 << 15) | (rs2 << 20);
}

/// @brief I 形式 (レジスタ 2 つ + 即値) の命令語を組み立てる。
/// @param base opcode と funct3 を含む素の語
/// @param rd 書込み先レジスタ番号
/// @param rs1 入力レジスタ番号
/// @param imm 12 bit 即値 (呼び手が範囲内に収めること)
/// @return 完成した命令語
int iw3(int base, int rd, int rs1, int imm) {
  return base | (rd << 7) | (rs1 << 15) | (imm << 20);
}

/// @brief S 形式 (ストア) の命令語を組み立てる。
/// @param base opcode と funct3 を含む素の語
/// @param rs1 アドレスを保持するレジスタ番号
/// @param rs2 格納する値のレジスタ番号
/// @param imm 12 bit 変位。上位 7 bit と下位 5 bit に分かれて配置される
/// @return 完成した命令語
int sw3(int base, int rs1, int rs2, int imm) {
  return base | (((imm >> 5) & 127) << 25) | ((imm & 31) << 7) | (rs1 << 15) | (rs2 << 20);
}

/// @brief 任意の 32 bit 定数をレジスタへ置く (lui + addi の 2 語)。
/// @param rd 書込み先レジスタ番号
/// @param v 置きたい値
/// @return 常に 0
/// @note addi の即値は符号拡張されるため，下位 12 bit の最上位が立つ値では
///       上位側が 1 足りなくなる。lui へ渡す前に +2048 しておくことで
///       この目減りを相殺している。
int liw(int rd, int v) {
  outw((((v + 2048) & 0xfffff000) | 0x37) | (rd << 7));
  outw(iw3(0x13, rd, rd, v & 4095));
  return 0;
}

// ---- 記号表 (scc と同一) ----
// いずれも線形探索。規模 (大域 2048 / ローカル 256) では十分で，
// ハッシュを持つと表の初期化と衝突処理の分だけ実装が増えるため採らない。
// 探索の鍵は常に直近に読んだ識別子 tname である。
// 見つからない場合は -1 を返す。0 は正当な添字なので不可を表せない。

/// @brief tname と一致する大域記号を探す。
/// @return エントリ添字。見つからなければ -1
int gfind() {
  int i;
  i = 0;
  while (i < gcnt) {
    if (streq(gname + i * 32, tname)) return i;
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
  copyn(gname + e * 32, tname);
  gsz[e] = 0;
  gvar[e] = 0;
  gused[e] = 0;
  return e;
}
/// @brief tname と一致するローカル記号 (引数・ローカル変数) を探す。
/// @return エントリ添字。見つからなければ -1
/// @note ローカルを先に引き，無ければ大域を引く。これが名前の遮蔽になる。
int lfind() {
  int i;
  i = 0;
  while (i < lcnt) {
    if (streq(lname + i * 32, tname)) return i;
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
  copyn(lname + e * 32, tname);
  return e;
}
/// @brief tname と一致する構造体を探す。
/// @return 構造体番号。見つからなければ -1
int sfind() {
  int i;
  i = 0;
  while (i < scnt) {
    if (streq(sname + i * 32, tname)) return i;
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
    if (streq(sname + i * 32, snam)) return i;
    i = i + 1;
  }
  return -1;
}
/// @brief 構造体 k のメンバのうち tname と一致するものを探す。
/// @param k 構造体番号
/// @return メンバ表の添字。見つからなければ -1
/// @note メンバは全構造体で 1 本の表に並べ，msid で所属を絞る。
///       構造体ごとに表を分けるより添字管理が簡単で済む。
int mfind(int k) {
  int i;
  i = 0;
  while (i < mcnt) {
    if (msid[i] == k) {
      if (streq(mname + i * 32, tname)) return i;
    }
    i = i + 1;
  }
  return -1;
}

/// @brief 再配置を 1 件記録する。
/// @param off .text 内の適用位置
/// @param sym 対象シンボル (0 以上 = 大域記号番号, 負 = -1 - ローカル番号)
/// @param ty 種別 (R_RISCV_*)
/// @param ad 加数
/// @return 常に 0
int addrel(int off, int sym, int ty, int ad) {
  if (rcnt > 8191) exit(6);
  rof[rcnt] = off;
  rsy[rcnt] = sym;
  rty[rcnt] = ty;
  rad[rcnt] = ad;
  rcnt = rcnt + 1;
  return 0;
}

/// @brief シンボルのアドレスを lui + addi でレジスタへ置き，再配置を張る。
/// @param d 書込み先レジスタ番号
/// @param sym 対象シンボル (addrel と同じ符号化)
/// @return 常に 0
/// @note 値はリンク時に決まるので，ここでは即値 0 の 2 語を置くだけにして
///       HI20 と LO12_I の再配置で埋めてもらう。両者は psABI 上それぞれが
///       独立にシンボルと加数を持つので，対にする必要はない。
int esymaddr(int d, int sym) {
  addrel(outp, sym, r_hi20, 0);
  outw(0x37 | (d << 7));
  addrel(outp, sym, r_lo12i, 0);
  outw(iw3(0x13, d, d, 0));
  return 0;
}

// ---- 型の解析 (scc と同一) ----
//
// 型は 1 個の int に詰める:  ty = (ポインタ深さ << 16) | 基底
//   基底 0 = char, 1 = int, 2+k = 構造体 k
// この表現なら「* を 1 つ被せる/剥がす」が 65536 の加減算になり，
// ポインタかどうかの判定が (ty >> 16) で済む。型表を別に持たなくてよい。

/// @brief 基底型 (int / char / struct 名) を読む。
/// @return 基底型の番号。型として不正なら終了コード 1，未知の構造体なら 2 で停止する
int anoncnt;              ///< 無名の struct / union に振る通し番号

/// @brief 無名の struct / union のタグ名を snam へ作る。
/// @return 常に 0
/// @note 先頭を '$' にしておくと，識別子の字句規則では作れない名前になるので
///       利用者の書いたタグと衝突しない。
int anonnam() {
  int n;
  int i;
  n = anoncnt;
  anoncnt++;
  snam[0] = '$';
  i = 1;
  if (n == 0) { snam[1] = '0'; i = 2; }
  while (n > 0) { snam[i] = '0' + n % 10; i++; n = n / 10; }
  while (i < 32) { snam[i] = 0; i++; }
  return 0;
}

/// @brief typedef 名を tname で引く。
/// @return 表の添字。無ければ -1
int tdfind() {
  int i;
  for (i = 0; i < tdcnt; i++)
    if (streq(tdname + i * 32, tname)) return i;
  return -1;
}

/// @brief 列挙定数を tname で引く。
/// @return 表の添字。無ければ -1
int ecfind() {
  int i;
  for (i = 0; i < eccnt; i++)
    if (streq(ecname + i * 32, tname)) return i;
  return -1;
}

/// @brief 型指定子の始まりか。
/// @note 識別子が typedef 名かどうかで宣言か式かが決まる。C の構文が
///       文脈自由でない有名な箇所で，字句だけでは判断できない。
int istype() {
  if (tok == k_int || tok == k_char || tok == k_void) return 1;
  if (tok == k_unsigned || tok == k_signed || tok == k_short || tok == k_long) return 1;
  if (tok == k_struct || tok == k_union || tok == k_enum) return 1;
  if (tok == k_const || tok == k_volatile) return 1;
  if (tok == t_id) return tdfind() >= 0;
  return 0;
}

/// @brief enum の本体 { 名前 (= 定数)? , ... } を読み，列挙定数を登録する。
/// @return 常に 0
/// @note 値を省略すると「直前の値 + 1」，最初は 0 になる。
int enumbody() {
  int v;
  int i;
  next();
  v = 0;
  while (tok != o_rc) {
    if (tok != t_id) exit(1);
    if (ecfind() >= 0) exit(4);
    if (eccnt > 255) exit(6);
    i = eccnt;
    eccnt++;
    copyn(ecname + i * 32, tname);
    next();
    if (tok == o_asn) {
      next();
      if (tok == o_sub) { next(); if (tok != t_num) exit(1); v = -tval; }
      else { if (tok != t_num) exit(1); v = tval; }
      next();
    }
    ecval[i] = v;
    v++;
    if (tok == o_comma) next();
    else if (tok != o_rc) exit(1);
  }
  next();
  return 0;
}

/// @brief 型指定子を 1 個読む (const / volatile は読み飛ばす)。
/// @return 型番号
/// @note struct と union は同じ表を共有する。型としての違いはメンバの
///       配置だけで，参照する側の扱いは同じだからである。
int ptype() {
  int k;
  int u;
  int uns; int sgn; int sht; int bas;
  while (tok == k_const || tok == k_volatile) next();
  if (tok == k_void) { next(); k = t_void; }
  else if (tok == k_struct || tok == k_union) {
    u = 0;
    if (tok == k_union) u = 1;
    next();
    if (tok == t_id) { copyn(snam, tname); next(); }
    else anonnam();
    if (tok == o_lc) strudef(u);
    k = sfind2();
    if (k < 0) exit(2);
    k = k + 2;
  } else if (tok == k_enum) {
    // enum は型としては int。タグは型の区別を生まないので読み捨てる
    next();
    if (tok == t_id) next();
    if (tok == o_lc) enumbody();
    k = 1;
  } else if (tok == t_id) {
    k = tdfind();
    if (k < 0) exit(1);
    k = tdty[k];
    next();
  } else if (tok == k_unsigned || tok == k_signed || tok == k_short
             || tok == k_long || tok == k_int || tok == k_char) {
    // 整数型の修飾子。順序は自由なので一通り読んでから決める
    uns = 0;
    sgn = 0;
    sht = 0;
    bas = -1;
    while (1) {
      if (tok == k_unsigned) { uns = 1; next(); }
      else if (tok == k_signed) { sgn = 1; next(); }
      else if (tok == k_short) { sht = 1; next(); }
      else if (tok == k_long) { next(); }       // long は int と同じ幅
      else if (tok == k_int) { bas = 1; next(); }
      else if (tok == k_char) { bas = 0; next(); }
      else break;
    }
    if (sht) {
      if (uns) k = t_ushort;
      else k = t_short;
    } else if (bas == 0) {
      // 素の char は符号なし。signed char だけ別の型になる
      if (sgn) k = t_schar;
      else k = 0;
    } else {
      if (uns) k = t_uint;
      else k = 1;
    }
  } else exit(1);
  while (tok == k_const || tok == k_volatile) next();
  return k;
}
/// @brief 基底型に続く '*' を読み，ポインタ深さを足し込む。
/// @param b 基底型
/// @return 完全な型
int pstars(int b) {
  while (tok == o_mul) { b = b + 65536; next(); }
  return b;
}

/// @brief 型が指す実体 1 個の大きさ (バイト)。ポインタ演算のスケール係数になる。
/// @param t 型
/// @return バイト数
/// @brief その型が構造体・共用体か。
/// @note 型番号は「ポインタの深さ << 16 | 基底」で，基底 2 以上が構造体表の
///       添字である。void を離れた番号に置いてあるので範囲で見分けられる。
int isstru(int t) {
  if ((t >> 16) != 0) return 0;
  if (t < 2) return 0;
  return t < 2 + scnt;
}

/// @brief その型がデータスタック上で占める語数。
/// @param t 型
/// @return 構造体なら ceil(大きさ/4)，それ以外は 1
/// @note 引数の受渡しと，仮引数のフレーム配置の両方でこの数を使う。
int nwords(int t) {
  if (isstru(t)) return tsize(t) >> 2;
  return 1;
}

/// @brief その型が関数型か (ポインタを剥がした後の基底で見る)。
int isfn(int t) {
  if ((t >> 16) != 0) return 0;
  if (t < t_fn) return 0;
  return t < t_fn + fncnt;
}

/// @brief 返却型 r の関数型を得る (同じものがあれば使い回す)。
int ftype(int r) {
  int i;
  for (i = 0; i < fncnt; i++)
    if (frty[i] == r) return t_fn + i;
  if (fncnt > 255) exit(6);
  i = fncnt;
  fncnt++;
  frty[i] = r;
  return t_fn + i;
}

/// @brief その型が配列型か。
int isarr(int t) {
  if ((t >> 16) != 0) return 0;
  if (t < t_arr) return 0;
  return t < t_arr + arrcnt;
}

/// @brief 配列型を「先頭要素へのポインタ」へ退化させる。
/// @note 配列は式の中では常にこの形になる。要素がさらに配列なら
///       「配列へのポインタ」になり，添字を 1 つ進めるたびに 1 段ずつ剥がれる。
int adecay(int t) { return aelem[t - t_arr] + 65536; }

/// @brief 要素型 e，要素数 n の配列型を得る (同じものがあれば使い回す)。
int atype(int e, int n) {
  int i;
  for (i = 0; i < arrcnt; i++)
    if (aelem[i] == e && acnt[i] == n) return t_arr + i;
  if (arrcnt > 511) exit(6);
  i = arrcnt;
  arrcnt++;
  aelem[i] = e;
  acnt[i] = n;
  return t_arr + i;
}

/// @brief その型を「符号なしとして読む」か (ロードの拡張と切詰めで使う)。
/// @note 素の char は符号なしとする (C は処理系定義)。
int isuty(int t) {
  if ((t >> 16) != 0) return 0;
  if (t == 0) return 1;
  return t == t_ushort || t == t_uint;
}

/// @brief 算術が符号なしになる型か。
/// @note char と short は int へ格上げされるので，符号なしになるのは
///       unsigned int (= unsigned long) のときだけである。
int isuar(int t) { return t == t_uint; }

int tsize(int t) {
  if ((t >> 16) != 0) return 4;
  if (t == 0) return 1;
  if (t == 1) return 4;
  if (t == t_schar) return 1;
  if (t == t_short || t == t_ushort) return 2;
  if (t == t_uint) return 4;
  if (t == t_void) return 1;
  if (isarr(t)) return acnt[t - t_arr] * tsize(aelem[t - t_arr]);
  if (isfn(t)) return 4;
  return ssize[t - 2];
}

/// @brief 算術の結果の型 (整数の格上げと通常の算術変換)。
int arith2(int a, int b) {
  if (isuar(a) || isuar(b)) return t_uint;
  return 1;
}

char fpnam[32];           ///< 関数ポインタの宣言子から取り出した名前

/// @brief 「( * …」で始まるか (関数ポインタの宣言子か)。
/// @note 括弧の次を 1 つ覗く。lsave / lrest は 1 段しか無いので，
///       この判定と fnpdec のあいだで別の退避を挟んではならない。
int isfnp() {
  int r;
  if (tok != o_lp) return 0;
  lsave();
  next();
  r = 0;
  if (tok == o_mul) r = 1;
  lrest();
  return r;
}

/// @brief 関数ポインタの宣言子「( * 名前 ) ( 仮引数 )」を読む。
/// @param b 返却型
/// @return 関数へのポインタの型。名前は fpnam に入る
/// @note 仮引数の型は持たないので，括弧の対応だけ数えて読み飛ばす。
///       呼出しの検査は名前つきの関数と違って個数も見ない。
int fnpdec(int b) {
  int n;
  next();
  if (tok != o_mul) exit(1);
  next();
  if (tok != t_id) exit(1);
  copyn(fpnam, tname);
  next();
  if (tok != o_rp) exit(1);
  next();
  if (tok != o_lp) exit(1);
  next();
  n = 1;
  while (n > 0) {
    if (tok == t_eof) exit(1);
    if (tok == o_lp) n = n + 1;
    if (tok == o_rp) n = n - 1;
    next();
  }
  return 65536 | ftype(b);
}

/// @brief 宣言子の [n] の並びを読み，配列型を組み立てる。
/// @param b 要素の型
/// @return 配列型 (「[」が無ければ b をそのまま)
/// @note 再帰で読むと，内側の次元から順に型が作られる。
///       int a[3][4] は array(3, array(4, int)) になる。
int pdims(int b) {
  int n;
  if (tok != o_lb) return b;
  next();
  n = 0;
  if (tok == t_num) { n = tval; next(); }
  if (tok != o_rb) exit(1);
  next();
  return atype(pdims(b), n);
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

// ---- IR 構築 ----

/// @brief IR 命令を 1 個追加する。
/// @param op 命令種別 (c_*。二項演算は c_bin + b_*)
/// @param a 第 1 オペランド
/// @param b 第 2 オペランド
/// @return 追加した命令の番号。これがそのまま「その命令が定義する値」の番号になる
int emit(int op, int a, int b) {
  if (icnt > 8191) exit(6);
  iop[icnt] = op;
  ia[icnt] = a;
  ib[icnt] = b;
  iret[icnt] = 0;          // 構造体を返す呼出しだけが後から書き換える
  icnt = icnt + 1;
  return icnt - 1;
}
/// @brief 新しいラベル番号を確保する。
/// @return ラベル番号
int newlab() {
  if (labcnt > 1023) exit(6);
  labcnt = labcnt + 1;
  return labcnt - 1;
}

/// @brief 名前を持たない一時変数をフレーム上に 1 語確保する。
/// @return フレームオフセット
/// @note && と || の結果に使う。この IR は値を「定義した命令番号」で指すので，
///       分岐で合流した先に 2 つの定義が届く形にはできない (φ 関数がない)。
///       そこで両方の経路からメモリへ書き，合流後に読み直すことで
///       1 命令 1 定義の性質を保っている。
int frame1(int n) {
  cloff = cloff + n;
  if (cloff > cmax) cmax = cloff;
  return cloff - n;
}

int hslot() {
  hcnt = hcnt + 1;
  return frame1(4);
}

// ---- 式 (いずれも「値番号」を返す) ----
//
// 式の解析結果は 2 つの大域で伝える:
//   ety … その式の型
//   elv … 1 なら「左辺値」= 返した値番号は値そのものではなくアドレス
//
// 変数を読んだ直後は elv = 1 (アドレスだけ判っている状態) にしておき，
// 実際に値が要る場面で rv() を通してロードを発行する。こうすると
// 代入の左辺や & の対象では余計なロードを出さずに済む。
// 「値が要る」側の演算子はすべて自分でオペランドを rv() に通す責任を持つ。

/// @brief 左辺値なら値へ変換する (必要ならロードを発行する)。
/// @param v 式が返した値番号
/// @return 値そのものを保持する値番号。elv が 0 なら v をそのまま返す
int rv(int v) {
  if (elv) {
    // 構造体の値は 1 語に載らない。実体のアドレスをそのまま値とする
    // (@section struval)。型は構造体のままなので，メンバ参照は変わらない
    if (isstru(ety)) { elv = 0; return v; }
    v = ldval(v, ety);
    elv = 0;
  }
  return v;
}

/// @brief 文字列リテラルを文字列プールへ積み，その先頭アドレスを表す値を作る。
/// @return 値番号 (型は char *)
/// @note 実体は関数本体の後ろにまとめて出力する。アドレスはその時点まで
///       決まらないので，ここでは GSTR にプール内オフセットだけ持たせ，
///       emitfn の末尾で実アドレスへ書き換える。
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

/// @brief メンバ参照 (. および ->) を解析し，メンバのアドレスを表す値を作る。
/// @param k 構造体番号
/// @param v 構造体の先頭アドレスを保持する値番号
/// @return メンバのアドレスを保持する値番号
/// @note 呼び出し時点でトークンは '.' か '->' を指している。
///       配列メンバは先頭要素へのポインタに退化させるので elv = 0 とし，
///       スカラメンバは左辺値 (elv = 1) のままにしてロードを遅延させる。
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
  if (marr[m]) { ety = adecay(mty[m]); elv = 0; earr = 1; }
  else { ety = mty[m]; elv = 1; earr = 0; }
  esz = msz[m];
  return v;
}

/// @brief 実引数 i が占める領域の，先頭からの語オフセット。
/// @param aw 実引数ごとの語数
/// @param i 実引数の番号
/// @return 語オフセット
/// @note 可変長の呼出しで「どこから可変部か」を語数で判定するために使う。
int argofs(int *aw, int i) {
  int k; int o;
  k = 0;
  o = 0;
  while (k < i) { o = o + aw[k]; k = k + 1; }
  return o;
}

/// @brief 実引数を 1 個積む。構造体は語 0 から順に複数語を積む。
/// @param v 値番号 (構造体では実体のアドレス)
/// @param w 語数
/// @return 常に 0
/// @note 呼ばれた側は後ろから取り出すので，語 0 から積めばフレーム上の
///       並びが元の記憶域の並びと一致する (@section strarg)。
int pusharg(int v, int w) {
  int k; int a;
  if (w == 1) { emit(c_arg, v, 0); return 0; }
  k = 0;
  while (k < w) {
    a = emit(c_bin + b_add, v, emit(c_const, k * 4, 0));
    emit(c_arg, emit(c_loadw, a, 0), 0);
    k = k + 1;
  }
  return 0;
}

/// @brief 関数呼出しの実引数並びを解析し，CALL を発行する。
/// @param e 呼び出す関数の大域記号番号
/// @return 返却値を保持する値番号
/// @note 実引数は左から順に ARG で積む (データスタック経由の受渡しは
///       scc と同じ ABI)。gna が -1 のときは前方参照でまだ個数が
///       判らないので個数検査を省く。判っていて食い違えば型エラー (5)。
int ecallseq(int e) {
  int np; int n; int i; int k;
  int av[32];
  int aw[32];
  next();
  np = 0;
  n = 0;
  if (tok != o_rp) {
    // 実引数は代入式。ここで expr() を呼ぶとカンマが引数の区切りでなく
    // カンマ演算子として食われてしまう。
    // 値の計算は左から順に行い，ARG を出す順序だけ後で決める。
    // 構造体は複数語を占めるので，実引数の個数 np と語数 n は別に数える
    while (1) {
      if (np > 31) exit(6);
      av[np] = rv(assign());
      aw[np] = nwords(ety);
      n = n + aw[np];
      np = np + 1;
      if (tok != o_comma) break;
      next();
    }
  }
  if (tok != o_rp) exit(1);
  next();
  k = gna[e];
  if (gvar[e]) {
    // 可変部を逆順に先に積み，名前つきを順に積む (@section vararg)。
    // 可変部は語数と実引数の個数がずれると並べ替えられないので，
    // 構造体は名前つきの側にしか置けない
    if (k < 0) k = 0;
    if (n < k) exit(5);
    i = np;
    while (i > 0) {
      i = i - 1;
      if (argofs(aw, i) < k) break;
      if (aw[i] != 1) exit(5);
      emit(c_arg, av[i], 0);
    }
    i = 0;
    while (argofs(aw, i) < k) { pusharg(av[i], aw[i]); i = i + 1; }
  } else {
    if (k >= 0 && k != n) exit(5);
    if (k < 0) gused[e] = 1;
    i = 0;
    while (i < np) { pusharg(av[i], aw[i]); i = i + 1; }
  }
  i = emit(c_call, e, n);
  if (isstru(gty[e])) {
    // 返却された構造体はデータスタックに積まれて来る。フレーム上に
    // 引取り先を取り，呼出し命令の側情報として渡す。実際の複写は
    // 出力段が呼出しの直後に埋め込む (@section strret)
    iret[i] = frame1(tsize(gty[e]));
    ety = gty[e];
    elv = 0;
    earr = 0;
    erv = 1;                 // 一時領域なので代入先にはできない
    return emit(c_laddr, iret[i], 0);
  }
  ety = gty[e];
  elv = 0;
  earr = 0;
  return i;
}

/// @brief 識別子を解決する (ローカル -> 大域 -> 未知なら前方参照の関数呼出し)。
/// @return 値番号
/// @note 未知の名前は「これから定義される関数の呼出し」としてのみ許す。
///       直後が '(' でなければ未定義識別子 (エラー 2)。仮登録した記号は
///       gdef = 0 のままなので，最後まで定義されなければ main が検出する。
int eident() {
  int e;
  int v;
  e = lfind();
  if (e >= 0) {
    if (larr[e]) { ety = adecay(lty[e]); elv = 0; earr = 1; }
    else { ety = lty[e]; elv = 1; earr = 0; }
    esz = lsz[e];
    next();
    return emit(c_laddr, loff[e], 0);
  }
  e = gfind();
  if (e >= 0) {
    if (gkind[e] == 1) {
      next();
      if (tok == o_lp) return ecallseq(e);
      // 関数名を値として使う。値はその関数のアドレスで，型は関数へのポインタ
      ety = 65536 | ftype(gty[e]);
      elv = 0;
      earr = 0;
      return emit(c_gaddr, e, 0);
    }
    if (garr[e]) { ety = adecay(gty[e]); elv = 0; earr = 1; }
    else { ety = gty[e]; elv = 1; earr = 0; }
    esz = gsz[e];
    next();
    return emit(c_gaddr, e, 0);      // アドレスはリンク時に決まるので記号番号を持つ
  }
  // 列挙定数。名前空間は変数と同じなので，局所・大域の後に引く
  e = ecfind();
  if (e >= 0) {
    v = emit(c_const, ecval[e], 0);
    ety = 1; elv = 0; earr = 0;
    next();
    return v;
  }
  next();
  if (tok != o_lp) exit(2);
  e = gnew();
  gkind[e] = 1; gty[e] = 1; gval[e] = 0; gdef[e] = 0; garr[e] = 0; gna[e] = -1;
  return ecallseq(e);
}

/// @brief 一次式 (リテラル・識別子・括弧) を解析する。
/// @return 値番号
int eprim() {
  int v;
  earr = 0;
  erv = 0;
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

/// @brief 関数へのポインタを通した呼出しを解析する。
/// @param f  呼出し先を表す値番号
/// @param rt 返却型
/// @return 値番号
/// @note 引数の積み方と返却値の受取りは名前つきの呼出しと同じ。
///       違うのは jal ではなく jalr を出す点だけである。
int ecalli(int f, int rt) {
  int n; int a; int w;
  next();
  n = 0;
  if (tok != o_rp) {
    // 関数ポインタは仮引数の型を持たないので個数の検査はできない。
    // 構造体は名前つきの呼出しと同じ形で積む
    while (1) {
      a = rv(assign());
      w = nwords(ety);
      pusharg(a, w);
      n = n + w;
      if (tok != o_comma) break;
      next();
    }
  }
  if (tok != o_rp) exit(1);
  next();
  // 構造体を返す関数ポインタは受けない。引取りの語数を出力段で決められない
  if (isstru(rt)) exit(5);
  ety = rt;
  elv = 0;
  earr = 0;
  return emit(c_calli, f, n);
}

/// @brief 後置演算 (添字 [] ・メンバ . ・アロー ->) を左から畳み込む。
/// @return 値番号
/// @note a[i] は *(a + i) と同義に展開する。添字は指し先の大きさで
///       スケールし，結果は左辺値 (elv = 1) として返すので，
///       代入の左辺にも読み出しにも使える。
int epost() {
  int v; int i; int pt; int k; int sz; int c;
  v = eprim();
  while (tok == o_lb || tok == o_dot || tok == o_arrow
         || tok == o_inc || tok == o_dec || tok == o_lp) {
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
      if (isarr(ety)) {
        // 多次元配列の途中。値ではなく「次の次元の先頭」を指している
        esz = tsize(ety);
        ety = adecay(ety);
        elv = 0;
        earr = 1;
      } else {
        elv = 1;
        earr = 0;
      }
    } else if (tok == o_dot) {
      // 構造体の値はどれも実体のアドレスなので，左辺値でなくても
      // メンバは取れる (mk().x のような呼出しの返却値)。ただしそれは
      // 一時領域なので，書き込みは erv を見て撥ねる
      if (!isstru(ety)) exit(5);
      v = emember(ety - 2, v);
    } else if (tok == o_arrow) {
      v = rv(v);
      if ((ety >> 16) != 1) exit(5);
      k = ety & 65535;
      if (!isstru(k)) exit(5);
      v = emember(k - 2, v);
    } else if (tok == o_lp) {
      // 関数へのポインタを通した呼出し
      v = rv(v);
      if ((ety >> 16) != 1) exit(5);
      k = ety & 65535;
      if (!isfn(k)) exit(5);
      v = ecalli(v, frty[k - t_fn]);
    } else {
      // 後置 ++ / --。式の値は変更前の値
      if (elv == 0) exit(5);
      k = 0;
      if (tok == o_inc) k = 1;
      next();
      v = incdec(v, ety, k, 1);
    }
  }
  return v;
}

/// @brief 単項演算 (- ! * &) を解析する。
/// @return 値番号
/// @note * と & は elv の付け外しだけで済む。* は「値として得たアドレス」を
///       左辺値に変える操作 (elv = 1)，& は「左辺値のアドレス」をそのまま
///       値に変える操作 (elv = 0) であり，どちらも命令を生まない。
/// @brief 値を型 t の幅へ切り詰める (キャスト用)。
/// @return 切り詰めた値の値番号
/// @note 32 bit セルの上位を落とす。符号つきなら左シフト + 算術右シフトで
///       符号拡張し，符号なしなら論理右シフトで 0 拡張する。
int narrow(int v, int t) {
  int w; int n;
  if ((t >> 16) != 0) return v;
  w = tsize(t);
  if (w >= 4) return v;
  n = 32 - w * 8;
  v = emit(c_bin + b_sll, v, emit(c_const, n, 0));
  if (isuty(t)) return emit(c_bin + b_srl, v, emit(c_const, n, 0));
  return emit(c_bin + b_sra, v, emit(c_const, n, 0));
}

/// @brief sizeof を解析する (「sizeof (型)」と「sizeof 単項式」)。
/// @return 大きさを表す定数の値番号
/// @note C は式形式の被演算子を評価しない。IR は配列と個数で持っているので，
///       解析前に個数を控えておき，型を得た後に巻き戻すことで「出さない」を
///       実現する。配列に対しては配列全体の大きさを返す (earr / esz)。
int esizeof() {
  int t; int n; int si; int sl; int sh; int ss;
  next();
  if (tok == o_lp) {
    lsave();
    next();
    if (istype()) {
      t = pstars(ptype());
      if (tok != o_rp) exit(1);
      next();
      n = tsize(t);
      ety = 1; elv = 0; earr = 0;
      return emit(c_const, n, 0);
    }
    lrest();
  }
  si = icnt; sl = labcnt; sh = cloff; ss = spcnt;
  euna();
  n = tsize(ety);
  if (earr) n = esz;
  icnt = si; labcnt = sl; cloff = sh; spcnt = ss;
  ety = 1; elv = 0; earr = 0;
  return emit(c_const, n, 0);
}

int euna() {
  int v; int t; int k;
  if (tok == o_inc || tok == o_dec) {
    // 前置 ++ / --。式の値は変更後の値
    k = 0;
    if (tok == o_inc) k = 1;
    next();
    v = euna();
    if (elv == 0) exit(5);
    return incdec(v, ety, k, 0);
  }
  if (tok == k_sizeof) return esizeof();
  if (tok == o_lp) {
    // 「( 型 )」ならキャスト，そうでなければ括弧式。1 トークン先で見分ける
    lsave();
    next();
    if (istype()) {
      t = pstars(ptype());
      if (tok != o_rp) exit(1);
      next();
      v = rv(euna());
      // 幅の狭い型へのキャストは値を切り詰める
      v = narrow(v, t);
      ety = t; elv = 0; earr = 0;
      return v;
    }
    lrest();
  }
  if (tok == o_sub) {
    next();
    v = rv(euna());
    ety = 1; earr = 0;
    return emit(c_neg, v, 0);
  }
  if (tok == o_not) {
    next();
    v = rv(euna());
    ety = 1; earr = 0;
    return emit(c_not, v, 0);
  }
  if (tok == o_mul) {
    next();
    v = rv(euna());
    if ((ety >> 16) == 0) exit(5);
    ety = ety - 65536;
    if (isarr(ety)) {
      esz = tsize(ety);
      ety = adecay(ety);
      elv = 0;
      earr = 1;
    } else if (isfn(ety)) {
      // 関数を参照はがししても関数のままで，式の中では再びポインタへ戻る。
      // (*p)(x) と p(x) が同じ意味になるのはこのためである
      ety = ety + 65536;
      elv = 0;
      earr = 0;
    } else {
      elv = 1;
      earr = 0;
    }
    return v;
  }
  if (tok == o_amp) {
    next();
    v = euna();
    if (elv == 0) {
      // 関数のアドレスは関数そのものと同じ値である (&f と f は等価)
      if ((ety >> 16) == 1 && isfn(ety & 65535)) return v;
      exit(5);
    }
    elv = 0;
    ety = ety + 65536;
    earr = 0;
    return v;
  }
  return epost();
}

/// @brief 左辺値のアドレスから値を読む。
/// @param a アドレスを表す値番号
/// @param t その記憶域の型
/// @return 読んだ値の値番号
int ldval(int a, int t) {
  int w;
  if ((t >> 16) != 0) return emit(c_loadw, a, 0);
  w = tsize(t);
  if (w == 1) {
    if (isuty(t)) return emit(c_loadb, a, 0);
    return emit(c_loadbs, a, 0);
  }
  if (w == 2) {
    if (isuty(t)) return emit(c_loadhu, a, 0);
    return emit(c_loadh, a, 0);
  }
  return emit(c_loadw, a, 0);
}
/// @brief 左辺値のアドレスへ値を書く。
/// @return 書いた値の値番号 (代入式の値になる)
/// @brief 構造体の実体を語単位で複写する IR を出す。
/// @param d 複写先のアドレスを保持する値番号
/// @param s 複写元のアドレスを保持する値番号
/// @param n 大きさ (バイト。構造体の大きさは 4 の倍数に丸めてある)
/// @return 常に 0
/// @note IR の段で語ごとの load / store に展開する。出力段・dce・
///       レジスタ割付けに手を入れずに済むのが利点で，代わりに大きな
///       構造体では IR が長くなる。上限を設けて超えたら領域超過 (6)。
int scopy(int d, int s, int n) {
  int k; int da; int sa;
  if (n > 1024) exit(6);
  k = 0;
  while (k < n) {
    da = emit(c_bin + b_add, d, emit(c_const, k, 0));
    sa = emit(c_bin + b_add, s, emit(c_const, k, 0));
    emit(c_stw, da, emit(c_loadw, sa, 0));
    k = k + 4;
  }
  return 0;
}

int stval(int a, int r, int t) {
  int w;
  if ((t >> 16) != 0) { emit(c_stw, a, r); return r; }
  w = tsize(t);
  if (w == 1) emit(c_stb, a, r);
  else if (w == 2) emit(c_sth, a, r);
  else emit(c_stw, a, r);
  return r;
}

/// @brief ++ / -- を展開する。
/// @param a     左辺値のアドレスを表す値番号
/// @param t     その型
/// @param isadd 1 = ++, 0 = --
/// @param post  1 = 後置 (式の値は変更前), 0 = 前置 (変更後)
/// @return 式の値の値番号
/// @note アドレス a は呼び手が 1 度だけ評価しており，ここでは読みと書きで
///       同じ値番号を 2 度参照する。これで *p++ のような「副作用のある
///       左辺値」も 1 度しか評価されない (docs/stage010-c89.md 4.2)。
///       ポインタなら指し先の大きさで増減する。
int incdec(int a, int t, int isadd, int post) {
  int cur; int one; int nv; int sz;
  cur = ldval(a, t);
  sz = 1;
  if ((t >> 16) != 0) sz = tsize(t - 65536);
  one = emit(c_const, sz, 0);
  if (isadd) nv = emit(c_bin + b_add, cur, one);
  else nv = emit(c_bin + b_sub, cur, one);
  stval(a, nv, t);
  ety = t; elv = 0; earr = 0;
  if (post) return cur;
  return nv;
}

// 以降の二項演算子は優先順位ごとに 1 関数を割り当てた再帰下降で，
// 低い優先順位の関数が高い方を呼ぶ。いずれも「同じ優先順位が続く限り
// while で左から畳み込む」形なので，自然に左結合になる。

/// @brief 乗除算 (* / %) を解析する。
/// @return 値番号
int emul() {
  int v; int r; int op; int lt; int rt;
  v = euna();
  while (tok == o_mul || tok == o_div || tok == o_mod) {
    op = tok;
    v = rv(v);
    lt = ety;
    next();
    r = rv(euna());
    rt = ety;
    if (op == o_mul) v = emit(c_bin + b_mul, v, r);
    else if (op == o_div) {
      if (isuar(lt) || isuar(rt)) v = emit(c_bin + b_udiv, v, r);
      else v = emit(c_bin + b_div, v, r);
    } else {
      if (isuar(lt) || isuar(rt)) v = emit(c_bin + b_urem, v, r);
      else v = emit(c_bin + b_rem, v, r);
    }
    ety = arith2(lt, rt); elv = 0; earr = 0;
  }
  return v;
}

/// @brief 値を要素サイズ倍する (ポインタ演算のスケーリング)。
/// @param v スケールしたい値番号
/// @param sz 要素サイズ (バイト)
/// @return スケール後の値番号。sz が 1 なら命令を出さず v をそのまま返す
/// @note ここで作る CONST は fold の対象になるので，両辺が定数なら
///       乗算そのものがコンパイル時に消える。
int escale2(int v, int sz) {
  int c;
  if (sz == 1) return v;
  c = emit(c_const, sz, 0);
  return emit(c_bin + b_mul, v, c);
}

/// @brief 加減算 (+ -) を解析する。ポインタ演算のスケーリングもここで行う。
/// @return 値番号
/// @note C と同じ規則を実装する:
///       ポインタ ± 整数 -> 整数側を要素サイズ倍し，型はポインタのまま
///       整数 + ポインタ -> 可換なので同様に整数側をスケール
///       ポインタ - ポインタ -> バイト差を要素サイズで割り，型は int
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
        ety = arith2(lt, ety);
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
        ety = arith2(lt, ety);
      }
    }
    elv = 0;
    earr = 0;
  }
  return v;
}

/// @brief シフト (<< >>) を解析する。>> は論理右シフト。
/// @return 値番号
int eshift() {
  int v; int r; int op; int lt;
  v = eadd();
  while (tok == o_shl || tok == o_shr) {
    op = tok;
    v = rv(v);
    lt = ety;
    next();
    r = rv(eadd());
    if (op == o_shl) v = emit(c_bin + b_sll, v, r);
    // 右シフトは左辺の符号で決まる。符号つきなら算術シフト
    else if (isuar(lt)) v = emit(c_bin + b_srl, v, r);
    else v = emit(c_bin + b_sra, v, r);
    ety = arith2(lt, lt); elv = 0; earr = 0;
  }
  return v;
}

/// @brief 大小比較 (< > <= >=) を解析する。結果は 1 / 0。
/// @return 値番号
int erel() {
  int v; int r; int op; int lt;
  v = eshift();
  while (tok == o_lt || tok == o_gt || tok == o_le || tok == o_ge) {
    op = tok;
    v = rv(v);
    lt = ety;
    next();
    r = rv(eshift());
    if (isuar(lt) || isuar(ety)) {
      if (op == o_lt) v = emit(c_bin + b_ult, v, r);
      else if (op == o_gt) v = emit(c_bin + b_ugt, v, r);
      else if (op == o_le) v = emit(c_bin + b_ule, v, r);
      else v = emit(c_bin + b_uge, v, r);
    } else {
      if (op == o_lt) v = emit(c_bin + b_slt, v, r);
      else if (op == o_gt) v = emit(c_bin + b_sgt, v, r);
      else if (op == o_le) v = emit(c_bin + b_sle, v, r);
      else v = emit(c_bin + b_sge, v, r);
    }
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief 等値比較 (== !=) を解析する。結果は 1 / 0。
/// @return 値番号
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
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief ビット AND (&) を解析する。
/// @return 値番号
int eband() {
  int v; int r;
  v = eeq();
  while (tok == o_amp) {
    v = rv(v);
    next();
    r = rv(eeq());
    v = emit(c_bin + b_and, v, r);
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief ビット XOR (^) を解析する。
/// @return 値番号
int exor() {
  int v; int r;
  v = eband();
  while (tok == o_xor) {
    v = rv(v);
    next();
    r = rv(eband());
    v = emit(c_bin + b_xor, v, r);
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief ビット OR (|) を解析する。
/// @return 値番号
int ebor() {
  int v; int r;
  v = exor();
  while (tok == o_or) {
    v = rv(v);
    next();
    r = rv(exor());
    v = emit(c_bin + b_or, v, r);
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief 論理 AND (&&) を解析する。左辺が偽なら右辺を評価しない (短絡)。
/// @return 値番号 (1 / 0)
/// @note 展開の形:
///         左辺が 0 なら l1 へ飛ぶ
///         右辺を評価し，!! で 1/0 に正規化して隠しスロットへ格納 -> l2 へ
///       l1: スロットへ 0 を格納
///       l2: スロットを読み直したものが式の値
///       合流点で 1 つの定義に見せるためにメモリを経由する (hslot 参照)。
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
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief 論理 OR (||) を解析する。左辺が真なら右辺を評価しない (短絡)。
/// @return 値番号 (1 / 0)
/// @note 構造は ecand と対称で，飛び先で格納する定数が 1 になる。
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
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief 条件演算子 (?:) を解析する。右結合。
/// @return 値番号
int econd() {
  int v; int a; int b; int off; int l1; int l2; int t;
  v = ecor();
  if (tok != o_que) return v;
  v = rv(v);
  // && / || と同じ理由で隠しスロットを使う。この IR は値を「定義した命令の
  // 番号」で指すので，2 つの経路から届く値を 1 つの値番号では表せない
  off = hslot();
  l1 = newlab();
  l2 = newlab();
  emit(c_bz, v, l1);
  next();
  a = rv(expr());
  t = ety;
  emit(c_stw, emit(c_laddr, off, 0), a);
  emit(c_jmp, l2, 0);
  emit(c_lab, l1, 0);
  if (tok != o_col) exit(1);
  next();
  b = rv(econd());
  emit(c_stw, emit(c_laddr, off, 0), b);
  emit(c_lab, l2, 0);
  v = emit(c_loadw, emit(c_laddr, off, 0), 0);
  // 結果の型は then 側とする。通常の算術変換は第 2 部で型を広げてから扱う
  ety = t; elv = 0; earr = 0;
  return v;
}

/// @brief 代入 (単純代入と複合代入) を解析する。右結合。
/// @return 値番号
/// @note 左辺は左辺値でなければならない (エラー 5)。複合代入では左辺の
///       アドレスを表す値番号を読みと書きで 2 度使うので，左辺の評価は
///       1 度で済む (docs/stage010-c89.md 4.2)。
///       複合代入のトークンは o_asnb + b_* に並べてあるので，
///       o_asnb を引けばそのまま二項演算の種別になる。
int assign() {
  int v; int r; int t; int op; int cur; int res; int sz;
  v = econd();
  if (tok == o_asn) {
    // 返却されたばかりの構造体 (とそのメンバ) は一時領域にある。
    // 代入を許すと捨てられる領域へ書くだけになるので撥ねる
    if (elv == 0 || erv) exit(5);
    t = ety;
    elv = 0;
    next();
    r = rv(assign());
    if (isstru(t)) {
      // 同じ構造体型どうしでなければ代入できない。大きさが同じでも
      // 別の型なら別物である
      if (ety != t) exit(5);
      scopy(v, r, tsize(t));
    } else stval(v, r, t);
    ety = t; earr = 0;
    return r;
  }
  if (tok < o_asnb) return v;
  if (tok > o_asnb + 9) return v;
  if (elv == 0) exit(5);
  t = ety;
  op = tok - o_asnb;
  elv = 0;
  next();
  cur = ldval(v, t);
  r = rv(assign());
  if ((t >> 16) != 0) {
    // ポインタに許すのは += と -= だけ。整数側を指し先の大きさでスケールする
    if (op != b_add && op != b_sub) exit(5);
    sz = tsize(t - 65536);
    if (sz != 1) r = emit(c_bin + b_mul, r, emit(c_const, sz, 0));
  }
  res = emit(c_bin + op, cur, r);
  stval(v, res, t);
  ety = t; elv = 0; earr = 0;
  return res;
}

/// @brief 式を解析する (カンマ演算子を含む最上位)。
/// @return 値番号 (最後の被演算子の値)
/// @note 左側の値は捨てるが，副作用は既に IR に積まれている。使われない
///       値を作る命令は dce が落とすので，ここで何かする必要はない。
int expr() {
  int v;
  v = assign();
  while (tok == o_comma) {
    next();
    v = assign();
  }
  return v;
}

// ---- 文 ----

/// @brief break の飛び先を積む。
int pushbrk(int l) {
  if (brkn > 63) exit(6);
  brkl[brkn] = l;
  brkn = brkn + 1;
  return 0;
}
/// @brief continue の飛び先を積む。
int pushcon(int l) {
  if (conn > 63) exit(6);
  conl[conn] = l;
  conn = conn + 1;
  return 0;
}

/// @brief goto のラベルを名前 (tname) で引く。無ければ未定義として登録する。
/// @return ラベル表の添字
int glfind() {
  int i;
  i = 0;
  while (i < glcnt) {
    if (streq(glname + i * 32, tname)) return i;
    i = i + 1;
  }
  if (glcnt > 63) exit(6);
  i = glcnt;
  glcnt = glcnt + 1;
  copyn(glname + i * 32, tname);
  gllab[i] = newlab();
  gldef[i] = 0;
  return i;
}

/// @brief case のラベル値を読む。
/// @return 値
/// @note 整数定数と文字定数に限る (先頭の - は許す)。enum 定数を含む
///       定数式は第 2 部で enum と一緒に扱う。
int caseval() {
  int neg; int v;
  neg = 0;
  if (tok == o_sub) { neg = 1; next(); }
  if (tok != t_num) exit(1);
  v = tval;
  next();
  if (neg) return 0 - v;
  return v;
}

/// @brief 文を 1 個解析して IR を積む。
/// @return 常に 0
/// @note 分岐先はここでは番号 (ラベル) で置くだけで，実アドレスの解決は
///       出力段に任せる。構文解析の時点で飛距離を知る必要がないので，
///       前方参照のための後戻りが要らない。
int stmt() {
  int c; int l0; int l1; int l2; int l3;
  int i; int n; int b0; int s; int d; int k; int w;
  if (tok == o_lc) {
    next();
    // 複文の先頭に宣言を置ける (C89)。名前は複文を抜けると見えなくなり，
    // フレームの割付けも戻す。フレームの大きさは cmax で決まるので，
    // 兄弟の複文は同じ領域を使い回せる
    n = lcnt;
    b0 = cloff;
    while (istype()) plocal();
    while (tok != o_rc) stmt();
    next();
    lcnt = n;
    cloff = b0;
    return 0;
  }
  // 「名前 :」ならラベル。式の始まりと見分けるには 1 トークン先が要る
  if (tok == t_id) {
    lsave();
    next();
    k = 0;
    if (tok == o_col) k = 1;
    lrest();
    if (k) {
      i = glfind();
      if (gldef[i]) exit(4);
      gldef[i] = 1;
      emit(c_lab, gllab[i], 0);
      next();
      next();
      return 0;
    }
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
    pushbrk(l1);
    pushcon(l0);
    stmt();
    brkn = brkn - 1;
    conn = conn - 1;
    emit(c_jmp, l0, 0);
    emit(c_lab, l1, 0);
    return 0;
  }
  if (tok == k_do) {
    next();
    l0 = newlab();
    l1 = newlab();
    l2 = newlab();
    emit(c_lab, l0, 0);
    pushbrk(l2);
    pushcon(l1);
    stmt();
    brkn = brkn - 1;
    conn = conn - 1;
    emit(c_lab, l1, 0);
    if (tok != k_while) exit(1);
    next();
    if (tok != o_lp) exit(1);
    next();
    c = rv(expr());
    if (tok != o_rp) exit(1);
    next();
    if (tok != o_semi) exit(1);
    next();
    emit(c_bnz, c, l0);
    emit(c_lab, l2, 0);
    return 0;
  }
  if (tok == k_for) {
    next();
    if (tok != o_lp) exit(1);
    next();
    if (tok != o_semi) expr();
    if (tok != o_semi) exit(1);
    next();
    l0 = newlab();
    l2 = newlab();
    emit(c_lab, l0, 0);
    if (tok != o_semi) {
      c = rv(expr());
      emit(c_bz, c, l2);
    }
    if (tok != o_semi) exit(1);
    next();
    if (tok == o_rp) {
      // 歩進が無いので while と同じ形でよい
      next();
      pushbrk(l2);
      pushcon(l0);
      stmt();
      brkn = brkn - 1;
      conn = conn - 1;
      emit(c_jmp, l0, 0);
      emit(c_lab, l2, 0);
      return 0;
    }
    // 歩進は構文上ここにあるが実行は本体の後である。IR は解析順にしか
    // 積めないので，本体へ飛び越す jmp を 1 個入れて順序を入れ替える:
    //   L0: 条件 -> bz L2 / jmp L3 / L1: 歩進 -> jmp L0 / L3: 本体 -> jmp L1 / L2:
    l1 = newlab();
    l3 = newlab();
    emit(c_jmp, l3, 0);
    emit(c_lab, l1, 0);
    expr();
    emit(c_jmp, l0, 0);
    if (tok != o_rp) exit(1);
    next();
    emit(c_lab, l3, 0);
    pushbrk(l2);
    pushcon(l1);
    stmt();
    brkn = brkn - 1;
    conn = conn - 1;
    emit(c_jmp, l1, 0);
    emit(c_lab, l2, 0);
    return 0;
  }
  if (tok == k_switch) {
    next();
    if (tok != o_lp) exit(1);
    next();
    c = rv(expr());
    if (tok != o_rp) exit(1);
    next();
    // 本体を先に，振分けを後に出す。こうすると本体を読み終える前に
    // case の一覧を知る必要がない (docs/stage010-c89.md 3.3)。
    // 値を隠しスロットへ置くのは，本体を跨いで値番号を生かせないためである
    s = hslot();
    emit(c_stw, emit(c_laddr, s, 0), c);
    l0 = newlab();
    l1 = newlab();
    emit(c_jmp, l0, 0);
    b0 = swn;
    d = swdflt;
    swdflt = 0 - 1;
    swdep = swdep + 1;
    pushbrk(l1);
    stmt();
    brkn = brkn - 1;
    swdep = swdep - 1;
    emit(c_jmp, l1, 0);
    emit(c_lab, l0, 0);
    i = b0;
    while (i < swn) {
      c = emit(c_loadw, emit(c_laddr, s, 0), 0);
      c = emit(c_bin + b_seq, c, emit(c_const, swval[i], 0));
      emit(c_bnz, c, swlab[i]);
      i = i + 1;
    }
    if (swdflt >= 0) emit(c_jmp, swdflt, 0);
    emit(c_lab, l1, 0);
    // 内側の switch が控えた分は使い切ったので巻き戻す
    swn = b0;
    swdflt = d;
    return 0;
  }
  if (tok == k_case) {
    if (swdep == 0) exit(1);
    next();
    n = caseval();
    if (tok != o_col) exit(1);
    next();
    if (swn > 255) exit(6);
    l0 = newlab();
    swval[swn] = n;
    swlab[swn] = l0;
    swn = swn + 1;
    emit(c_lab, l0, 0);
    return 0;
  }
  if (tok == k_default) {
    if (swdep == 0) exit(1);
    next();
    if (tok != o_col) exit(1);
    next();
    l0 = newlab();
    swdflt = l0;
    emit(c_lab, l0, 0);
    return 0;
  }
  if (tok == k_break) {
    next();
    if (tok != o_semi) exit(1);
    next();
    if (brkn == 0) exit(1);
    emit(c_jmp, brkl[brkn - 1], 0);
    return 0;
  }
  if (tok == k_continue) {
    next();
    if (tok != o_semi) exit(1);
    next();
    if (conn == 0) exit(1);
    emit(c_jmp, conl[conn - 1], 0);
    return 0;
  }
  if (tok == k_goto) {
    next();
    if (tok != t_id) exit(1);
    i = glfind();
    next();
    if (tok != o_semi) exit(1);
    next();
    emit(c_jmp, gllab[i], 0);
    return 0;
  }
  if (tok == k_return) {
    next();
    if (tok == o_semi) emit(c_ret, emit(c_const, 0, 0), 0);
    else {
      c = rv(expr());
      if (tok != o_semi) exit(1);
      if (isstru(cretty)) {
        // 語を逆順に積む。最後に積んだ語 0 が x9 の指す位置の 1 つ上に来る
        // (@section strret)。返却型と同じ構造体でなければならない
        if (ety != cretty) exit(5);
        w = tsize(cretty);
        while (w > 0) {
          w = w - 4;
          emit(c_arg, emit(c_loadw, emit(c_bin + b_add, c, emit(c_const, w, 0)), 0), 0);
        }
      }
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
// いずれも IR 配列上のその場書き換え。適用順は fold -> dce -> regalloc。
// fold が定数を潰すと使われなくなる値が出るので，dce はその後に置く。

/// @brief 二項演算命令か。
/// @param i 命令番号
/// @return 1 = 二項演算
int isbin(int i) { return iop[i] >= c_bin; }

/// @brief 副作用を持たない (削除してよい) 命令か。
/// @param i 命令番号
/// @return 1 = 純粋
/// @note ロードも純粋として扱う。この言語では I/O が getc/putc に限られ，
///       読むだけで意味が変わるアドレスが無いため。副作用があるのは
///       ストア・呼出し・分岐・返却で，これらは常に残す。
int ispure(int i) {
  if (isbin(i)) return 1;
  if (iop[i] == c_const || iop[i] == c_laddr || iop[i] == c_gaddr || iop[i] == c_gstr) return 1;
  if (iop[i] == c_loadw || iop[i] == c_loadb) return 1;
  if (iop[i] == c_loadbs || iop[i] == c_loadh || iop[i] == c_loadhu) return 1;
  if (iop[i] == c_neg || iop[i] == c_not) return 1;
  return 0;
}

/// @brief 二項演算をコンパイル時に計算する。
/// @param k 演算種別 (b_*)
/// @param x 左オペランドの値
/// @param y 右オペランドの値
/// @return 演算結果
/// @note occ 自身が動く環境と生成コードが動く環境は同じ RV32 なので，
///       ここでの計算結果は実行時の結果と一致する。
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
  if (k == b_srl) {
    // 論理右シフト。このコンパイラ自身の >> は符号つきなら算術シフトに
    // なったので，上位に伸びた符号ビットを必ず落とす。
    // 旧世代 (>> が論理シフト) で解釈しても同じ値になる書き方である
    if (y == 0) return x;
    if (y >= 32) return 0;
    return (x >> y) & ((1 << (32 - y)) - 1);
  }
  if (k == b_slt) return x < y;
  if (k == b_sgt) return x > y;
  if (k == b_sle) return x <= y;
  if (k == b_sge) return x >= y;
  if (k == b_seq) return x == y;
  return x != y;
}

/// @brief 命令 1 個に定数畳み込みを試みる。
/// @param i 命令番号
/// @return 常に 0
/// @note オペランドが CONST なら結果を計算し，命令自体を CONST へ書き換える。
///       値番号は命令番号のままなので参照側を直す必要がない。
///       除数 0 は畳み込まない (コンパイル時に落ちてしまうため，
///       実行時の挙動に委ねる)。条件分岐は，条件が定数なら
///       無条件ジャンプか「何もしない CONST」に化ける。
///
///       fold から切り出してあるのは可読性のためだけではない。occ.sc は
///       まず scc でコンパイルされるが，scc の条件分岐は B-type (±4KiB) を
///       距離検査せずに吐くため，1 関数の本体が大きすぎるとブートストラップ
///       段のバイナリが壊れる (docs/stage007-occ.md 3 章)。
int foldins(int i) {
  int k;
  if (isbin(i)) {
    if (iop[ia[i]] == c_const && iop[ib[i]] == c_const) {
      k = iop[i] - c_bin;
      // 符号なし系と算術右シフトは，このコンパイラ自身が符号つきでしか
      // 計算できないので畳まない (負の値で答えが食い違う)
      if (k >= b_udiv) return 0;
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

/// @brief 定数畳み込みパス。IR を先頭から 1 回走査する。
/// @return 常に 0
/// @note 1 回で足りるのは，値が必ず定義より後で使われるため。前から
///       順に潰していけば，オペランドは自分の番が来る前に確定している。
int fold() {
  int i;
  i = 0;
  while (i < icnt) {
    foldins(i);
    i = i + 1;
  }
  return 0;
}

/// @brief 値 v とその計算に必要な値を再帰的に生存として印を付ける。
/// @param v 値番号
/// @return 常に 0
/// @note 既に印が付いていれば即座に戻る。これが再帰の停止条件であり，
///       同じ部分式を共有していても指数的に辿らずに済む。
int markv(int v) {
  if (live[v]) return 0;
  live[v] = 1;
  if (isbin(v)) { markv(ia[v]); markv(ib[v]); return 0; }
  if (iop[v] == c_loadw || iop[v] == c_loadb || iop[v] == c_neg || iop[v] == c_not
      || iop[v] == c_loadbs || iop[v] == c_loadh || iop[v] == c_loadhu) {
    markv(ia[v]);
    return 0;
  }
  return 0;
}

/// @brief 不要コード削除パス。
/// @return 常に 0
/// @note 副作用のある命令を根として，そこから到達できる値だけを生存にする。
///       印の付かなかった純粋な値は出力段が読み飛ばす (命令列からは
///       取り除かず live で表す。詰めると値番号がずれてしまうため)。
int dce() {
  int i;
  i = 0;
  while (i < icnt) { live[i] = 0; i = i + 1; }
  i = 0;
  while (i < icnt) {
    if (!ispure(i)) {
      live[i] = 1;
      if (iop[i] == c_stw || iop[i] == c_stb || iop[i] == c_sth) { markv(ia[i]); markv(ib[i]); }
      else if (iop[i] == c_arg || iop[i] == c_ret || iop[i] == c_calli) markv(ia[i]);
      else if (iop[i] == c_bz || iop[i] == c_bnz) markv(ia[i]);
    }
    i = i + 1;
  }
  return 0;
}

// ---- レジスタ割付け (線形走査) ----
//
// 割付対象は x13..x27 の 15 本で，すべて callee-saved として扱う。
// 呼び出す側ではなく呼ばれた側が退避するので，CALL を跨いで生きる値が
// あっても呼出しの前後で退避・復元を挟まなくてよい。使ったレジスタだけを
// プロローグ/エピローグでまとめて出し入れする。
// x10 / x11 は割付けから外し，ロード・ストアとスピルの作業用に空けておく。

/// @brief 値 v の最終使用位置を i まで延ばす。
/// @param v 値番号
/// @param i 使用している命令番号
/// @return 常に 0
int usemark(int v, int i) {
  if (lastu[v] < i) lastu[v] = i;
  return 0;
}

/// @brief 線形走査でレジスタを割り付ける。結果は vreg に入る。
/// @return 常に 0
/// @note 手順は 3 段階:
///       1. 全命令を走査して各値の最終使用位置 lastu を求める
///       2. 先頭から進みながら，最終使用を過ぎた値のレジスタを解放する
///       3. 値を定義する命令に空きレジスタを与える。空きが無ければスピル
///
///       IR が SSA (1 値 1 定義) なので生存区間は「定義位置から lastu まで」
///       の 1 本の区間になり，区間の分割や合流を考えなくてよい。これが
///       線形走査で足りる理由である。
///
///       定義した直後に死ぬ値 (lastu <= i) にはレジスタを与えない。
///       出力段はそれを見て命令自体を省く。
int regalloc() {
  int i; int r; int f;
  i = 0;
  while (i < icnt) { lastu[i] = -1; vreg[i] = -1; i = i + 1; }
  i = 0;
  while (i < icnt) {
    if (live[i]) {
      if (isbin(i)) { usemark(ia[i], i); usemark(ib[i], i); }
      else if (iop[i] == c_loadw || iop[i] == c_loadb || iop[i] == c_neg || iop[i] == c_not
               || iop[i] == c_loadbs || iop[i] == c_loadh || iop[i] == c_loadhu) usemark(ia[i], i);
      else if (iop[i] == c_stw || iop[i] == c_stb || iop[i] == c_sth) { usemark(ia[i], i); usemark(ib[i], i); }
      else if (iop[i] == c_arg || iop[i] == c_ret || iop[i] == c_calli) usemark(ia[i], i);
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
      if (ispure(i) || iop[i] == c_call || iop[i] == c_calli) {
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
// 割付け結果 (vreg) を見ながら IR を 1 命令ずつ命令語へ落とす。
// スピルされた値は読むときに x10/x11 へ載せ，書いたら即座に書き戻す
// (レジスタに置き続けられないからスピルなので，保持はしない)。

/// @brief 値 v を読み出すレジスタ番号を得る。スピルなら作業レジスタへロードする。
/// @param v 値番号
/// @param sc2 スピル時に使う作業レジスタ (x10 か x11)
/// @return 実際に値が入っているレジスタ番号
/// @note 二項演算では左右で別の作業レジスタを渡す必要がある。同じものを
///       渡すと後のロードが前の値を潰す。
int oreg(int v, int sc2) {
  if (vreg[v] >= 0) return vreg[v];
  outw(iw3(0x2003, sc2, 8, spbase + (0 - 2 - vreg[v]) * 4));
  return sc2;
}

/// @brief 値 v を書き込む先のレジスタ番号を得る。
/// @param v 値番号
/// @return レジスタ番号。スピル値は一旦 x10 に作ってから dstore で書き戻す
int dreg(int v) {
  if (vreg[v] >= 0) return vreg[v];
  return 10;
}

/// @brief 直前に x10 へ作った値が，スピル対象ならフレームへ書き戻す。
/// @param v 値番号
/// @return 常に 0
/// @note vreg が -1 (未割付 = 誰も使わない値) のときは何もしない。
int dstore(int v) {
  if (vreg[v] >= 0) return 0;
  if (vreg[v] == -1) return 0;
  outw(sw3(0x2023, 8, 10, spbase + (0 - 2 - vreg[v]) * 4));
  return 0;
}

/// @brief ラベルへのジャンプを出力する。前方参照なら後埋めに登録する。
/// @param l ラベル番号
/// @return 常に 0
/// @note 分岐に必ず jal (±1MiB) を使うのが occ の要点である。scc までは
///       条件分岐を B-type で直接吐いていたが，B-type の変位は ±4KiB しか
///       なく，しかも距離検査をしていなかったため，本体が 4KiB を超える
///       if / while を黙って誤ったアドレスへ飛ぶコードにしていた
///       (Stage 7 の開発中に発見)。occ は条件分岐を
///       「逆条件で 2 語先へ飛ぶ B-type + jal」に分解することで，
///       条件付きでも距離の制約を受けないようにしている。
int lrefj(int l) {
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

/// @brief 二項演算種別に対応する RV32IM の素の命令語を返す。
/// @param k 演算種別 (b_*)
/// @return opcode と funct を含む語
/// @note 比較系 (b_sgt 以降) はここでは slt を返し，
///       オペランドの入替えや後続の補正命令は emitins 側で行う。
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
  if (k == b_udiv) return 0x02005033;
  if (k == b_urem) return 0x02007033;
  if (k == b_sra) return 0x40005033;
  return 0x2033;                     // slt (比較系の基本)
}

/// @brief IR 命令 1 個を命令語へ落とす。
/// @param i 命令番号
/// @return 1 = 直後にエピローグを出す必要がある (RET だった)。それ以外は 0
/// @note 値を定義する命令は，dce で死んでいるか割付けが無い (誰も使わない)
///       場合に丸ごと省かれる。
///
///       比較演算は RV32I に等値比較の命令が無いため合成する:
///         a >  b : slt の左右を入れ替える
///         a <= b : (b < a) を xor 1 で反転
///         a >= b : (a < b) を xor 1 で反転
///         a == b : 差を取り sltiu ..,1 (差が 0 なら 1)
///         a != b : 差を取り sltu x0,.. (差が 0 以外なら 1)
int emitins(int i) {
  int k; int d; int a; int b; int j;
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
    esymaddr(d, ia[i]);              // ia = 大域記号番号
    return dstore(i);
  }
  if (k == c_gstr) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    d = dreg(i);
    if (spfn > 255) exit(6);
    // 文字列の実体は関数本体の後ろに出すので位置がまだ決まらない。
    // 位置とプール内オフセットを控え，emitfn の末尾で記号と再配置を作る。
    spfix[spfn] = outp;
    spofs[spfn] = ia[i];
    spfn = spfn + 1;
    outw(0x37 | (d << 7));
    outw(iw3(0x13, d, d, 0));
    return dstore(i);
  }
  if (k == c_loadw || k == c_loadb || k == c_loadbs || k == c_loadh || k == c_loadhu) {
    if (!live[i]) return 0;
    if (vreg[i] == -1) return 0;
    a = oreg(ia[i], 10);
    d = dreg(i);
    // funct3: lb=000 lh=001 lw=010 lbu=100 lhu=101
    if (k == c_loadb) outw(iw3(0x4003, d, a, 0));
    else if (k == c_loadbs) outw(iw3(0x0003, d, a, 0));
    else if (k == c_loadh) outw(iw3(0x1003, d, a, 0));
    else if (k == c_loadhu) outw(iw3(0x5003, d, a, 0));
    else outw(iw3(0x2003, d, a, 0));
    return dstore(i);
  }
  if (k == c_stw || k == c_stb || k == c_sth) {
    a = oreg(ia[i], 10);
    b = oreg(ib[i], 11);
    // funct3: sb=000 sh=001 sw=010
    if (k == c_stb) outw(sw3(0x23, a, b, 0));
    else if (k == c_sth) outw(sw3(0x1023, a, b, 0));
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
    addrel(outp, b, r_jal, 0);       // 自オブジェクト内の呼出しもリンカに委ねる
    outw(0xef);
    d = 10;
    if (live[i] && vreg[i] != -1) d = dreg(i);
    outw(iw3(0x2003, d, 9, 0));
    a = 4;
    if (iret[i]) {
      // 構造体の返却。1 語目の上に語 0 から順に積まれている。
      // x9 を戻す前にフレームの一時領域へ引き取る (@section strret)。
      // 呼出しの値そのものは使われないので x10 を壊してよい
      k = tsize(gty[b]);
      j = 0;
      while (j < k) {
        outw(iw3(0x2003, 10, 9, 4 + j));
        outw(sw3(0x2023, 8, 10, iret[i] + j));
        j = j + 4;
      }
      a = 4 + k;
    }
    // 返却値の 1 語を捨てる。可変長の呼出しでは，呼ばれた側が取り出さ
    // なかった可変部もここでまとめて捨てる (積んだ側の責任)
    if (gvar[b] && gna[b] >= 0) a = a + (ib[i] - gna[b]) * 4;
    outw(iw3(0x13, 9, 9, a));
    if (live[i] && vreg[i] != -1) return dstore(i);
    return 0;
  }
  if (k == c_calli) {
    // 値を呼ぶ。引数の積み方と返却値の受取りは c_call と同じ
    a = oreg(ia[i], 10);
    outw(0xe7 | (a << 15));          // jalr ra, a, 0
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
  else if (k == b_ugt || k == b_ule) outw(rw3(0x3033, d, b, a));   // sltu (入替え)
  else if (k == b_ult || k == b_uge) outw(rw3(0x3033, d, a, b));   // sltu
  else if (k == b_seq || k == b_sne) outw(rw3(0x40000033, d, a, b));
  else outw(rw3(binbase(k), d, a, b));
  if (k == b_sle || k == b_sge || k == b_ule || k == b_uge) outw(iw3(0x4013, d, d, 1));
  else if (k == b_seq) outw(iw3(0x3013, d, d, 1));
  else if (k == b_sne) outw(rw3(0x3033, d, 0, d));
  return dstore(i);
}

/// @brief エピローグ (退避レジスタの復元・フレーム解放・復帰) を出力する。
/// @return 常に 0
/// @note 返却値はデータスタックに積んだ状態で来る。RET のたびに出力するので
///       1 つの関数に複数回現れうる。
int epilog2() {
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

/// @brief 構築済みの IR を最適化・割付けし，関数 1 個分のコードを出力する。
/// @param e この関数の大域記号番号
/// @return 常に 0
/// @note 全体の段取り:
///       1. fold -> dce -> regalloc (ここで初めてスピル数と使用レジスタが判る)
///       2. フレーム配置を確定する。スピル領域とレジスタ退避領域の大きさは
///          割付けの結果に依存するので，この順序でなければ決められない
///       3. 関数アドレスを確定し，溜まっていた前方参照呼出しを解決する
///       4. プロローグ -> 本体 -> ラベル後埋め -> 文字列プール，の順に出力
///
///       引数はデータスタックに積まれて来るので，プロローグで逆順に
///       取り出してフレームへ移す。以降は普通のローカル変数として扱える。
int emitfn(int e) {
  int i; int r; int o; int h; int nsv;
  // 割付けとフレームレイアウト
  fold();
  dce();
  regalloc();
  nsv = 0;
  r = 13;
  while (r < 28) { if (rused[r]) nsv = nsv + 1; r = r + 1; }
  spbase = cmax;
  svbase = spbase + nspill * 4;
  fnf = svbase + nsv * 4;
  if (fnf > 2040) exit(6);
  // 関数アドレス (.text 内オフセット) の確定。呼出し側は再配置で解決される
  gval[e] = outp;
  gdef[e] = 1;
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
  // 名前つきを取り出し終えた時点の x9 が可変部の先頭である
  if (cvaoff >= 0) outw(sw3(0x2023, 8, 9, cvaoff));
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
  // 文字列プールを本体の後ろへ出し，参照ごとにローカルシンボルと再配置を作る
  o = outp;
  i = 0;
  while (i < spcnt) { outbyte(spool[i]); i = i + 1; }
  i = 0;
  while (i < spfn) {
    if (nlsym > 1023) exit(6);
    lsoff[nlsym] = o + spofs[i];     // この文字列の .text 内オフセット
    addrel(spfix[i], 0 - 1 - nlsym, r_hi20, 0);
    addrel(spfix[i] + 4, 0 - 1 - nlsym, r_lo12i, 0);
    nlsym = nlsym + 1;
    i = i + 1;
  }
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
/// @brief 配列型なら，いちばん奥の要素の型を返す。
int abase(int t) {
  while (isarr(t)) t = aelem[t - t_arr];
  return t;
}

int memb(int se, int t) {
  int m;
  int sz;
  int al;
  // 入れ子のメンバ (struct A { struct B b; }) を許す。自分自身を
  // メンバに持つことはできない (大きさが決まらない)
  if (isstru(t) && t == 2 + se) exit(5);
  if (tok != t_id) exit(1);
  if (mfind(se) >= 0) exit(4);
  if (mcnt > 2047) exit(6);
  m = mcnt;
  mcnt = mcnt + 1;
  msid[m] = se;
  copyn(mname + m * 32, tname);
  mty[m] = t;
  next();
  // まずメンバの大きさと整列を決め，配置は struct / union で分ける。
  // 整列は「いちばん奥の要素の型」で決まる (char の配列だけ 1 バイト境界)
  t = pdims(t);
  mty[m] = t;
  sz = tsize(t);
  al = tsize(abase(t));
  if (al > 4) al = 4;
  if (isarr(t)) marr[m] = 1;
  else { marr[m] = 0; sz = tsize(t); if (sz > 4 && !isstru(t)) sz = 4; }
  msz[m] = sz;
  if (sunion[se]) {
    // union のメンバはすべて先頭に重なる。大きさは最大値
    moff[m] = 0;
    if (sz > ssize[se]) ssize[se] = sz;
  } else {
    if (al == 4) moff[m] = (ssize[se] + 3) & 0xfffffffc;
    else if (al == 2) moff[m] = (ssize[se] + 1) & 0xfffffffe;
    else moff[m] = ssize[se];
    ssize[se] = moff[m] + sz;
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

/// @brief 構造体定義 (struct 名 { ... };) を解析して登録する。
/// @return 常に 0
/// @note 先に空の構造体として登録してからメンバを読む。こうすると
///       メンバに自分自身へのポインタ (連結リストの next など) を書ける。
int strudef(int u) {
  int se;
  copyn(tname, snam);
  if (sfind() >= 0) exit(4);
  if (scnt > 255) exit(6);
  se = scnt;
  scnt = scnt + 1;
  copyn(sname + se * 32, tname);
  ssize[se] = 0;
  sunion[se] = u;
  next();
  while (tok != o_rc) memb(se, pstars(ptype()));
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
  if (isfnp()) {
    // 仮引数の関数ポインタ
    b = fnpdec(b);
    copyn(tname, fpnam);
    if (lfind() >= 0) exit(4);
    i = lnew();
    loff[i] = 8 + cna * 4;
    lty[i] = b;
    larr[i] = 0;
    lsz[i] = 4;
    cna = cna + 1;
    return 0;
  }
  // プロトタイプ宣言では仮引数の名前を省略できる (int f(int, char *);)
  if (tok == t_id) {
    if (lfind() >= 0) exit(4);
    i = lnew();
    loff[i] = 8 + cna * 4;
    next();
    // 仮引数の配列は先頭要素へのポインタになる (C の規則)
    b = pdims(b);
    if (isarr(b)) b = adecay(b);
    lty[i] = b;
    larr[i] = 0;
    lsz[i] = tsize(b);
  } else {
    b = pdims(b);
  }
  // 構造体の仮引数は ceil(大きさ/4) 語を占める (@section strarg)。
  // cna は語数を数えるので，これで配置もフレームの起点も揃う
  cna = cna + nwords(b);
  return 0;
}

/// @brief ローカル変数宣言を 1 個解析して登録する。
/// @return 常に 0
/// @note 宣言は関数本体の先頭にまとめる仕様なので，走査の途中で
///       フレームサイズが後戻りすることがない。
/// @brief 局所変数の初期化子を読み，代入のコードを出す。
/// @param i 局所記号の番号
/// @return 常に 0
/// @note 大域と違い実体は毎回フレーム上に作られるので，初期化は「宣言の
///       位置で代入する」ことに等しい。値は定数でなくてよい。
int linit(int i) {
  int t; int es; int n; int k; int a; int v; int w;
  t = lty[i];
  next();
  if (isarr(t)) {
    es = aelem[t - t_arr];
    n = acnt[t - t_arr];
    if (n == 0) exit(1);        // 局所では要素数を省略できない
    // 多次元は大域と同じく平らに並べる
    while (isarr(es)) {
      n = n * acnt[es - t_arr];
      es = aelem[es - t_arr];
    }
    w = tsize(es);
    if (tok == t_str) {
      k = 0;
      while (k < n) {
        a = emit(c_bin + b_add, emit(c_laddr, loff[i], 0), emit(c_const, k * w, 0));
        if (k < slen) v = emit(c_const, sbuf[k], 0);
        else v = emit(c_const, 0, 0);
        stval(a, v, es);
        k = k + 1;
      }
      next();
      return 0;
    }
    if (tok != o_lc) exit(1);
    next();
    k = 0;
    while (tok != o_rc) {
      if (k >= n) exit(6);
      v = rv(assign());
      a = emit(c_bin + b_add, emit(c_laddr, loff[i], 0), emit(c_const, k * w, 0));
      stval(a, v, es);
      k = k + 1;
      if (tok == o_comma) next();
      else if (tok != o_rc) exit(1);
    }
    next();
    while (k < n) {
      a = emit(c_bin + b_add, emit(c_laddr, loff[i], 0), emit(c_const, k * w, 0));
      stval(a, emit(c_const, 0, 0), es);
      k = k + 1;
    }
    return 0;
  }
  v = rv(assign());
  stval(emit(c_laddr, loff[i], 0), v, t);
  return 0;
}

int plocal() {
  int b; int i; int fp;
  b = pstars(ptype());
  fp = 0;
  if (isfnp()) {
    b = fnpdec(b);
    copyn(tname, fpnam);
    fp = 1;
  }
  if (tok != t_id && !fp) exit(1);
  if (lfind() >= 0) exit(4);
  i = lnew();
  loff[i] = cloff;
  if (!fp) {
    next();
    b = pdims(b);
  }
  lty[i] = b;
  lsz[i] = tsize(b);
  if (isarr(b)) {
    larr[i] = 1;
    frame1((lsz[i] + 3) & 0xfffffffc);
  } else {
    larr[i] = 0;
    // 構造体は 1 語に収まらない。実体をフレーム上に取る
    // (構造体の大きさは登録時に 4 の倍数へ丸めてある)
    if (isstru(b)) frame1(lsz[i]);
    else frame1(4);
  }
  if (tok == o_asn) linit(i);
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

/// @brief 関数定義を解析し，IR を構築して出力まで行う。
/// @return 常に 0
/// @note 既に記号がある場合は前方参照で仮登録されたものなので，
///       関数であること・未定義であることを確かめてから引き継ぐ。
///       本体末尾には無条件に「return 0」相当を足す。return を通らずに
///       関数の終わりへ到達した場合の返却値を仕様どおり 0 にするため。
int funcdef() {
  int e; int i;
  e = gfind();
  if (e >= 0) {
    if (gkind[e] != 1) exit(4);
  } else {
    e = gnew();
    gkind[e] = 1;
    gval[e] = 0;
    garr[e] = 0;
    // 引数の個数はまだ判らない。プロトタイプか定義かで後から決まる
    gna[e] = -1;
  }
  gty[e] = cty;
  cretty = cty;
  lcnt = 0;
  cna = 0;
  cvarg = 0;
  cvaoff = -1;
  next();
  // 「(void)」は引数なしの明示である。仮引数 1 個の void と紛らわしいので
  // 1 トークン先を覗いて見分ける
  if (tok == k_void) {
    lsave();
    next();
    if (tok != o_rp) lrest();
  }
  if (tok != o_rp) {
    pparam();
    while (tok == o_comma) {
      next();
      // 「, ...」は仮引数の並びの終わり。名前つきが 1 個も無い形
      // (int f(...);) は C89 が認めないので構文エラーにする
      if (tok == o_ellip) { next(); cvarg = 1; break; }
      pparam();
    }
  }
  if (tok != o_rp) exit(1);
  if (cvarg && cna == 0) exit(1);
  next();
  // 個数が判らないまま呼出しを出した後で可変長と判ると，既に出した
  // 呼出しの積み方が間違っている。宣言を先に置かせるしかない
  if (cvarg && gused[e]) exit(5);
  if (gna[e] >= 0 && gvar[e] != cvarg) exit(5);
  gvar[e] = cvarg;
  if (tok == o_semi) {
    // プロトタイプ宣言。引数の個数と返却型だけを控え，本体は待つ
    next();
    if (gna[e] >= 0 && gna[e] != cna) exit(5);
    gna[e] = cna;
    lcnt = 0;
    return 0;
  }
  if (gdef[e]) exit(4);
  gdef[e] = 1;
  gsta[e] = cstat;
  // 先にプロトタイプがあれば，引数の個数が一致していなければならない
  if (gna[e] >= 0 && gna[e] != cna) exit(5);
  gna[e] = cna;
  if (tok != o_lc) exit(1);
  cloff = 8 + cna * 4;
  cmax = cloff;
  if (cvarg) {
    // 可変部の先頭を保持する隠しローカル。プロローグが名前つき引数を
    // 取り出し終えた時点の x9 をここへ書く。名前を持たせておけば
    // va_start は「隠しローカルからの代入」で済み，式の構文を増やさない。
    //
    // 登録は '{' を読み進める前に行う。setname() は記号表の鍵 tname を
    // 書き換えるので，先に next() してしまうと本体の先頭トークンの名前が
    // 消え，istype() が typedef 名を見失う (va_list ap; が宣言に見えなくなる)
    setname("__va_ptr");
    i = lnew();
    loff[i] = frame1(4);
    lty[i] = 65536;              // char *
    larr[i] = 0;
    lsz[i] = 4;
    cvaoff = loff[i];
  }
  next();
  // 局所変数の初期化子はコードを出すので，IR の初期化を宣言より前に置く
  icnt = 0;
  labcnt = 0;
  spcnt = 0;
  hcnt = 0;
  while (istype()) plocal();
  // 制御構造の状態も関数ごとに作り直す
  brkn = 0;
  conn = 0;
  swn = 0;
  swdep = 0;
  swdflt = 0 - 1;
  glcnt = 0;
  while (tok != o_rc) stmt();
  next();
  // goto されたまま定義されなかったラベルは未定義の識別子である
  i = 0;
  while (i < glcnt) {
    if (gldef[i] == 0) exit(2);
    i = i + 1;
  }
  emit(c_ret, emit(c_const, 0, 0), 0);
  return emitfn(e);
}

/// @brief .text へ 1 語書き，位置を進める。
int outw4(int w) {
  outw(w);
  return 0;
}

/// @brief 出力位置を 4 バイト境界へ揃える。
int align4() {
  while (outp & 3) outbyte(0);
  return 0;
}

/// @brief 初期化子の「1 個の値」を読み，.text へ 1 語 (または 1 バイト) 書く。
/// @param t 対象の型
/// @return 常に 0
/// @note 定数のほか，文字列リテラル・大域変数のアドレス (&x)・関数名を
///       受ける。アドレスは R_RISCV_32 の再配置として書く。データが .text に
///       あるので既存の .rela.text がそのまま使える。
int ginit1(int t) {
  int v; int e; int neg; int w;
  w = tsize(t);
  if (tok == t_str) {
    // char *p = "abc";  実体は後回しにし，そこへの再配置だけを置く
    v = gstrq();
    addrel(outp, 0 - 1 - v, r_32, 0);
    outw4(0);
    return 0;
  }
  if (tok == o_amp) {
    next();
    if (tok != t_id) exit(1);
    e = gfind();
    if (e < 0) exit(2);
    next();
    addrel(outp, e, r_32, 0);
    outw4(0);
    return 0;
  }
  if (tok == t_id) {
    // 関数名または配列名。どちらもそのアドレスになる
    e = gfind();
    if (e < 0) {
      e = ecfind();
      if (e < 0) exit(2);
      v = ecval[e];
      next();
      if (w == 1) outbyte(v & 255);
      else if (w == 2) { outbyte(v & 255); outbyte((v >> 8) & 255); }
      else outw4(v);
      return 0;
    }
    next();
    addrel(outp, e, r_32, 0);
    outw4(0);
    return 0;
  }
  neg = 0;
  if (tok == o_sub) { neg = 1; next(); }
  if (tok != t_num) exit(1);
  v = tval;
  if (neg) v = 0 - v;
  next();
  if (w == 1) outbyte(v & 255);
  else if (w == 2) { outbyte(v & 255); outbyte((v >> 8) & 255); }
  else outw4(v);
  return 0;
}

/// @brief 大域初期化子の中の文字列リテラルを予約し，ローカルシンボルを返す。
/// @return ローカルシンボルの番号
/// @note 実体をここで書いてはならない。書くと初期化子の実体 (ポインタ語や
///       配列の要素) がその後ろへずれ，記号の値 gval[e] と食い違う。
///       関数本体と同じく spool へ積んでおき，初期化子を書き終えてから
///       gstrflush() でまとめて出す。実体の位置が決まるのはその時なので，
///       ローカルシンボルの番号だけ先に確保し，lsoff は後で埋める。
int gstrq() {
  int a; int p; int i;
  p = (slen + 4) & 0xfffffffc;
  if (spcnt + p > 8191) exit(6);
  if (nlsym > 1023) exit(6);
  if (gspn > 255) exit(6);
  a = nlsym;
  nlsym = nlsym + 1;
  gspsym[gspn] = a;
  gspofs[gspn] = spcnt;
  gspn = gspn + 1;
  i = 0;
  while (i < p) { spool[spcnt] = sbuf[i]; spcnt = spcnt + 1; i = i + 1; }
  next();
  return a;
}

/// @brief 予約した文字列リテラルの実体を .text へ出し，lsoff を埋める。
/// @return 常に 0
int gstrflush() {
  int o; int i;
  if (gspn == 0) { spcnt = 0; return 0; }
  align4();
  o = outp;
  i = 0;
  while (i < spcnt) { outbyte(spool[i]); i = i + 1; }
  i = 0;
  while (i < gspn) {
    lsoff[gspsym[i]] = o + gspofs[i];
    i = i + 1;
  }
  gspn = 0;
  spcnt = 0;
  return 0;
}

/// @brief 大域変数の初期化子を読み，.text へ実体を書く。
/// @param e 記号番号
/// @return 常に 0
/// @note 要素数を省略した配列は初期化子の個数から決まる。
int ginit(int e) {
  int t; int n; int i; int es; int flat;
  next();
  align4();
  gval[e] = outp;
  gdef[e] = 1;
  t = gty[e];
  if (isarr(t)) {
    es = aelem[t - t_arr];
    n = acnt[t - t_arr];
    // 多次元は平らに並べる。要素型が配列でなくなるまで畳んでおくと，
    // 以降は 1 次元と同じ扱いで済む。畳んだ場合は型と大きさが宣言で
    // 決まっているので，読み終えた後に書き直してはならない
    flat = 0;
    while (isarr(es)) {
      if (n == 0) exit(1);      // 内側が配列なのに外側の要素数が無い
      n = n * acnt[es - t_arr];
      es = aelem[es - t_arr];
      flat = 1;
    }
    if (tok == t_str) {
      // char s[] = "abc";  終端の NUL も含める
      i = 0;
      if (n == 0) n = slen + 1;
      while (i < n) {
        if (i < slen) outbyte(sbuf[i]);
        else outbyte(0);
        i = i + 1;
      }
      next();
      while (outp & 3) outbyte(0);
      if (!flat) { gty[e] = atype(es, n); gsz[e] = n; }
      gstrflush();
      return 0;
    }
    if (tok != o_lc) exit(1);
    next();
    i = 0;
    while (tok != o_rc) {
      ginit1(es);
      i = i + 1;
      if (tok == o_comma) next();
      else if (tok != o_rc) exit(1);
    }
    next();
    if (n == 0) n = i;
    if (i > n) exit(6);
    // 足りない分は 0 で埋める
    while (i < n) {
      if (tsize(es) == 1) outbyte(0);
      else if (tsize(es) == 2) { outbyte(0); outbyte(0); }
      else outw4(0);
      i = i + 1;
    }
    while (outp & 3) outbyte(0);
    if (!flat) { gty[e] = atype(es, n); gsz[e] = n * tsize(es); }
    gstrflush();
    return 0;
  }
  ginit1(t);
  while (outp & 3) outbyte(0);
  gsz[e] = tsize(t);
  gstrflush();
  return 0;
}

/// @brief 型を読んだ後の宣言を処理する (関数定義か大域変数かをここで分ける)。
/// @param b 基底型
/// @return 常に 0
/// @note 名前の次が '(' なら関数，そうでなければ大域変数。
///       大域変数には .bss 内のオフセットを与える。実アドレスはリンク時に
///       決まるので，参照側は再配置で解決される。
int dcont(int b) {
  int e; int fp;
  cty = pstars(b);
  // 「struct s { ... };」「enum { ... };」のように宣言子が無い形もある。
  // 型を登録するだけで，変数は作らない
  if (tok == o_semi) { next(); return 0; }
  fp = 0;
  if (isfnp()) {
    // int (*f)(int); 関数ポインタの大域変数
    cty = fnpdec(cty);
    copyn(tname, fpnam);
    fp = 1;
  } else {
    if (tok != t_id) exit(1);
    next();
    if (tok == o_lp) return funcdef();
  }
  e = gfind();
  if (e >= 0) {
    // extern で宣言済みの変数を後から定義するのは正しい。
    // それ以外の再宣言は重複定義
    if (gkind[e] != 0) exit(4);
    if (gdef[e]) exit(4);
    if (cext) { skipdecl(); return 0; }
  } else {
    e = gnew();
    gkind[e] = 0;
    gna[e] = 0;
  }
  if (!fp) cty = pdims(cty);
  gty[e] = cty;
  gsta[e] = cstat;
  // gsz は本当の大きさ (sizeof と ELF の st_size がこれを使う)。
  // .bss の割付けだけ 4 バイト境界へ切り上げる
  gsz[e] = tsize(cty);
  if (isarr(cty)) garr[e] = 1;
  else garr[e] = 0;
  gtxt[e] = 0;
  if (tok == o_asn) {
    // 初期値を持つものは .text に実体を置く (docs/stage010-c89.md 14 章)
    if (cext) exit(1);
    gtxt[e] = 1;
    ginit(e);
  } else if (cext) {
    // 実体は他の翻訳単位にある。未定義シンボルとして出し，リンカが解く
    gdef[e] = 0;
    gval[e] = 0;
  } else {
    gdef[e] = 1;
    gval[e] = bssp;
    bssp = bssp + ((gsz[e] + 3) & 0xfffffffc);
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

/// @brief トップレベルの宣言を 1 個処理する。
/// @return 常に 0
/// @note "struct 名 {" なら定義，"struct 名 名前" なら既存の構造体型を
///       使った宣言。1 トークン先読みするために型名を snam へ退避する。
/// @brief 宣言子の残り (配列の [n] と ;) を読み飛ばす。
/// @return 常に 0
/// @note extern の重複宣言のように「読むだけで何もしない」場合に使う。
int skipdecl() {
  if (tok == o_lb) {
    next();
    if (tok != t_num) exit(1);
    next();
    if (tok != o_rb) exit(1);
    next();
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

int typedef1() {
  int b;
  int i;
  next();
  b = pstars(ptype());
  if (isfnp()) {
    b = fnpdec(b);
    copyn(tname, fpnam);
  } else if (tok != t_id) exit(1);
  if (tdfind() >= 0) exit(4);
  if (tdcnt > 255) exit(6);
  i = tdcnt;
  tdcnt++;
  copyn(tdname + i * 32, tname);
  tdty[i] = b;
  if (tok == t_id) next();
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

int topdecl() {
  cext = 0;
  cstat = 0;
  // 記憶域クラスは型指定子の前に置かれる
  while (tok == k_extern || tok == k_static) {
    if (tok == k_extern) cext = 1;
    else cstat = 1;
    next();
  }
  if (tok == k_typedef) return typedef1();
  return dcont(ptype());
}

// ---- 組込み関数の登録 ----

/// @brief 組込み関数を「未定義の外部関数」として登録する。
/// @param nm 名前
/// @param na 引数個数
/// @return 常に 0
/// @note 引数個数だけを知っている状態にしておくことで，呼出しの個数検査は
///       効かせつつ，実体の解決はリンカに任せられる。
int biadd(char *nm, int na) {
  int e; int i;
  i = 0;
  while (nm[i]) { tname[i] = nm[i]; i = i + 1; }
  while (i < 32) { tname[i] = 0; i = i + 1; }
  e = gnew();
  gkind[e] = 1;
  gty[e] = 1;
  gval[e] = 0;
  gdef[e] = 0;                       // 未定義。リンカが前置部の実体へ解決する
  garr[e] = 0;
  gna[e] = na;
  return 0;
}
/// @brief getc / putc / exit を未定義シンボルとして登録する。
/// @return 常に 0
/// @note occ ではランタイム前置部の固定アドレスを直接埋めていたが，
///       前置部はリンカの持ち物になったので，ここでは名前だけを立てる。
int bireg() {
  biadd("getc", 0);
  biadd("putc", 1);
  biadd("exit", 1);
  return 0;
}

// ---- ELF 出力 ----
// eb にファイル全体を組み立ててから一気に書き出す。各セクションの位置は
// 先に計算し，セクションヘッダはその値を参照する。

/// @brief eb へ 1 バイト書く。
int ew1(int b) { eb[ep] = b; ep = ep + 1; return 0; }
/// @brief eb へ 16 bit をリトルエンディアンで書く。
int ew2(int h) { ew1(h & 255); ew1((h >> 8) & 255); return 0; }
/// @brief eb へ 32 bit をリトルエンディアンで書く。
int ew4(int w) { ew1(w & 255); ew1((w >> 8) & 255); ew1((w >> 16) & 255); ew1((w >> 24) & 255); return 0; }

/// @brief .strtab へ文字列を 1 個追加する。
/// @param nm NUL 終端文字列
/// @return .strtab 内のオフセット
int stadd(char *nm) {
  int o; int i;
  o = stp;
  i = 0;
  while (nm[i]) { stt[stp] = nm[i]; stp = stp + 1; i = i + 1; }
  stt[stp] = 0;
  stp = stp + 1;
  return o;
}

/// @brief .strtab へ .LCn 形式のローカルシンボル名を追加する。
/// @param n 通し番号
/// @return .strtab 内のオフセット
/// @note sc に数値の文字列化がないので手で作る。桁は逆順に組み立てて詰め直す。
int stadd_lc(int n) {
  int o; int i; int j; char d[16];
  o = stp;
  stt[stp] = '.'; stp = stp + 1;
  stt[stp] = 'L'; stp = stp + 1;
  stt[stp] = 'C'; stp = stp + 1;
  i = 0;
  if (n == 0) { d[0] = '0'; i = 1; }
  while (n) { d[i] = '0' + n % 10; n = n / 10; i = i + 1; }
  j = i;
  while (j) { j = j - 1; stt[stp] = d[j]; stp = stp + 1; }
  stt[stp] = 0;
  stp = stp + 1;
  return o;
}

/// @brief シンボル表エントリを 1 個書く。
/// @param nm .strtab 内のオフセット
/// @param val st_value
/// @param sz st_size
/// @param info st_info (bind << 4 | type)
/// @param shn st_shndx
int esym(int nm, int val, int sz, int info, int shn) {
  ew4(nm); ew4(val); ew4(sz); ew1(info); ew1(0); ew2(shn);
  return 0;
}

/// @brief セクションヘッダを 1 個書く (40 バイト)。
int eshdr(int nm, int ty, int fl, int off, int sz, int lk, int inf, int al, int es) {
  ew4(nm); ew4(ty); ew4(fl); ew4(0); ew4(off); ew4(sz); ew4(lk); ew4(inf); ew4(al); ew4(es);
  return 0;
}

/// @brief 再配置の対象シンボルを ELF のシンボル番号へ直す。
/// @param sym addrel の符号化 (0 以上 = 大域記号番号, 負 = -1 - ローカル番号)
/// @return ELF シンボル表の添字
/// @note 並びは [0] 空 [1..nlsym] ローカル [その後] 大域。ELF はローカルを
///       前に固めることを要求するので，この順序でなければならない。
int elfsym(int sym) {
  if (sym < 0) return 1 + (0 - 1 - sym);
  return gidx[sym];
}

/// @brief 大域記号 -> ELF シンボル番号の対応を作る。
/// @return 常に 0
/// @note ELF は「ローカルシンボルが先，大域が後」で並べ，sh_info に最初の
///       大域の番号を書く決まりである。static の記号はローカル側へ寄せる。
///       リンカは sh_info から先だけを大域表に集め，再配置は st_shndx が
///       1 / 2 ならそのオブジェクト内で解くので，これだけで翻訳単位に閉じる。
int mkgidx() {
  int i; int k;
  nsta = 0;
  i = 0;
  while (i < gcnt) {
    if (gsta[i] && gdef[i]) nsta = nsta + 1;
    i = i + 1;
  }
  k = 1 + nlsym;
  i = 0;
  while (i < gcnt) {
    if (gsta[i] && gdef[i]) { gidx[i] = k; k = k + 1; }
    i = i + 1;
  }
  i = 0;
  while (i < gcnt) {
    if (!(gsta[i] && gdef[i])) { gidx[i] = k; k = k + 1; }
    i = i + 1;
  }
  return 0;
}

/// @brief 組み立てた .text・シンボル・再配置から ELF ET_REL を出力する。
/// @return 常に 0
int writeelf() {
  int i; int nsym; int otext; int osym; int ostr; int orel; int oshs; int osh; int n;
  int bsz; int shsz;
  // .shstrtab の内容は固定なので，各名前の位置も定数として扱える
  // 0:"" 1:".text" 7:".bss" 12:".symtab" 20:".strtab" 28:".rela.text" 39:".shstrtab"
  shsz = 49;
  nsym = 1 + nlsym + gcnt;
  mkgidx();
  otext = 52;
  osym = otext + outp;
  // .strtab はシンボルを書きながら組み立てるので，長さは後で確定する
  stp = 0;
  stt[stp] = 0; stp = stp + 1;       // 先頭は空文字列 (規約)
  ep = 0;
  // ELF ヘッダ
  ew1(127); ew1('E'); ew1('L'); ew1('F');
  ew1(1); ew1(1); ew1(1); ew1(0);
  ew4(0); ew4(0);
  ew2(1);                            // e_type = ET_REL
  ew2(243);                          // e_machine = EM_RISCV
  ew4(1);                            // e_version
  ew4(0);                            // e_entry
  ew4(0);                            // e_phoff
  ew4(0);                            // e_shoff (後で埋める)
  ew4(0);                            // e_flags
  ew2(52); ew2(0); ew2(0);           // e_ehsize, e_phentsize, e_phnum
  ew2(40); ew2(7); ew2(6);           // e_shentsize, e_shnum, e_shstrndx
  // .text
  i = 0;
  while (i < outp) { ew1(ob[i]); i = i + 1; }
  // .symtab: [0] 空
  esym(0, 0, 0, 0, 0);
  // ローカル (文字列リテラル)。STB_LOCAL(0) STT_OBJECT(1) = 1
  i = 0;
  while (i < nlsym) {
    esym(stadd_lc(i), lsoff[i], 0, 1, 1);
    i = i + 1;
  }
  // static。STB_LOCAL(0) STT_FUNC(2) = 0x02 / STT_OBJECT(1) = 0x01
  bsz = 0;
  i = 0;
  while (i < gcnt) {
    if (gsta[i] && gdef[i]) {
      n = stadd(gname + i * 32);
      if (gkind[i] == 1) esym(n, gval[i], 0, 2, 1);
      else if (gtxt[i]) esym(n, gval[i], gsz[i], 1, 1);
      else esym(n, gval[i], gsz[i], 1, 2);
    }
    i = i + 1;
  }
  // 大域。関数は STB_GLOBAL(1) STT_FUNC(2) = 0x12,
  // 変数は STT_OBJECT(1) = 0x11, 未定義は STT_NOTYPE(0) = 0x10
  i = 0;
  while (i < gcnt) {
    if (!(gsta[i] && gdef[i])) {
      n = stadd(gname + i * 32);
      if (gdef[i] == 0) esym(n, 0, 0, 16, 0);
      else if (gkind[i] == 1) esym(n, gval[i], 0, 18, 1);
      else if (gtxt[i]) esym(n, gval[i], gsz[i], 17, 1);
      else esym(n, gval[i], gsz[i], 17, 2);
    }
    i = i + 1;
  }
  ostr = osym + nsym * 16;
  orel = ostr + stp;
  while (orel & 3) orel = orel + 1;
  oshs = orel + rcnt * 12;
  osh = oshs + shsz;
  while (osh & 3) osh = osh + 1;
  // .strtab
  i = 0;
  while (i < stp) { ew1(stt[i]); i = i + 1; }
  while (ep < orel) ew1(0);
  // .rela.text
  i = 0;
  while (i < rcnt) {
    ew4(rof[i]);
    ew4((elfsym(rsy[i]) << 8) | rty[i]);
    ew4(rad[i]);
    i = i + 1;
  }
  // .shstrtab
  ew1(0);
  i = 0; while (i < 6) { ew1(".text"[i]); i = i + 1; }
  i = 0; while (i < 5) { ew1(".bss"[i]); i = i + 1; }
  i = 0; while (i < 8) { ew1(".symtab"[i]); i = i + 1; }
  i = 0; while (i < 8) { ew1(".strtab"[i]); i = i + 1; }
  i = 0; while (i < 11) { ew1(".rela.text"[i]); i = i + 1; }
  i = 0; while (i < 10) { ew1(".shstrtab"[i]); i = i + 1; }
  while (ep < osh) ew1(0);
  // セクションヘッダ表
  eshdr(0, 0, 0, 0, 0, 0, 0, 0, 0);
  eshdr(1, 1, 6, otext, outp, 0, 0, 4, 0);
  eshdr(7, 8, 3, osym, bssp, 0, 0, 4, 0);
  eshdr(12, 2, 0, osym, nsym * 16, 4, 1 + nlsym + nsta, 4, 16);
  eshdr(20, 3, 0, ostr, stp, 0, 0, 1, 0);
  eshdr(28, 4, 0, orel, rcnt * 12, 3, 1, 4, 12);
  eshdr(39, 3, 0, oshs, shsz, 0, 0, 1, 0);
  // e_shoff を埋める
  eb[32] = osh & 255;
  eb[33] = (osh >> 8) & 255;
  eb[34] = (osh >> 16) & 255;
  eb[35] = (osh >> 24) & 255;
  i = 0;
  while (i < ep) { putc(eb[i]); i = i + 1; }
  return 0;
}

// ---- 駆動部 ----

/// @brief コンパイラ本体。標準入力からソースを読み，標準出力へ ELF を書く。
/// @return 常に 0 (異常時は exit で終了コードを返して停止する)
/// @note occ と違いランタイム前置部は出力しない。未定義シンボルの検査も
///       行わない (それはリンカの仕事になった)。main の有無も問わない。
int main() {
  int c;
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
  outp = 0; gcnt = 0; scnt = 0; mcnt = 0; lcnt = 0;
  bssp = 0; rcnt = 0; nlsym = 0; gspn = 0; spcnt = 0;
  bireg();
  next();
  while (tok != t_eof) topdecl();
  return writeelf();
}
