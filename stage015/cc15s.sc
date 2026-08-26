/// @file cc15s.sc
/// @brief C コンパイラ 第 15 世代 その 19。cc15r との差は **ginit / ginit1**。
///
/// ## 文字列リテラルの族の，最後の 1 つ
///
/// `static char m[2][8] = { "ab", "cd" };` が壊れていた。**通ってしまうが
/// 値が違う** —— 台帳で bad と呼ぶ状態である (tests/stage015 の staticstr2)。
///
/// `ginit()` は多次元を平らに畳んでから要素を読む。畳んだ後の要素型は
/// char であって配列ではないので，`cc15q` が `ginit1()` に入れた
/// 「対象が配列なら実体を並べる」が効かない。要素 1 つに 4 バイトの
/// 再配置が書かれて中身がアドレスになっていた。
///
/// ## 直し: 畳んでよいかを，初期化子の書かれ方で決める
///
/// 平らに畳むのは `int a[2][2] = {1,2,3,4}` のような書き方のための
/// 近道である。`{"ab","cd"}` の波括弧の中の 1 つ 1 つは **行 (char[8]) を
/// 初期化する**のであって，畳んだ後の要素 (char) を初期化するのでは
/// ない。
///
/// 見分け方は「`{` の次が `{` か文字列か」——そうなら下位の対象そのものを
/// 初期化しているので**畳まない**。そうでなければ平らな並びなので今まで
/// どおり畳む。**畳む道のバイト列は 1 バイトも変わらない。**
///
/// 畳まなくなった分，`ginit1()` に**配列を波括弧で受ける道**が要る。
/// 要素型で読み，宣言した幅に足りない分を 0 で埋める。`{ "ab" }` の形は
/// 波括弧が無いときと同じに実体を並べる (C89 6.5.7)。
///
/// ついでに 2 つ。埋める量を「要素の大きさぶん」に直した (1 / 2 / 4 を
/// 並べる書き方だと char[8] のような要素で 4 バイトしか埋まらない。
/// 1 / 2 / 4 のときの出るバイト列は前と同じ)。`char a[8] = { "ab" }` も
/// 通るようにした。
///
/// ## 族を 12 の形でまとめて測った
///
/// | | 形 | cc15r | cc15s |
/// |---|---|---|---|
/// | 1 | `char m[2][8] = {"ab","cd"}` | **誤** | 正 |
/// | 2 | `char m[][8] = {...}` | **拒む** | 正 |
/// | 3 | `char m[3][8] = {"ab","cd"}` (埋め) | **誤** | 正 |
/// | 4 | `char m[2][8] = {{'a','b'},{'c'}}` | **誤** | 正 |
/// | 5 | `char b[8] = { "ab" }` | **誤** | 正 |
/// | 6 | `char p[8] = "ab"` | 正 | 正 |
/// | 7 | `int a[2][2] = {1,2,3,4}` (畳む道) | 正 | 正 |
/// | 8 | `int a[2][2] = {{1,2},{3,4}}` | 正 | 正 |
/// | 9 | `struct { char a[8]; } s = {"xy"}` | 正 | 正 |
/// | 10 | `char *p[2] = {"gh","ij"}` | 正 | 正 |
///
/// **1 つの形で確かめて族ぜんぶが無事だと思ったのが 3 度の誤りの元
/// だった** (docs/stage017-cc.md 27.4 / 31.4)。4 度目は測ってから言う。
///
/// **鎖の成果物は変わらない。** 既存のソースにも tcc にも多次元の char
/// 配列を文字列で初期化する形は無い (探して 0 件)。stage017 まで作り直しても
/// 全段が cached である。
///
/// ---- 以下は cc15r から引き継いだ来歴 ----
///
/// @brief C コンパイラ 第 15 世代 その 18。cc15q との差は**1 か所 4 行**。
///
/// ## 文字列リテラルが配列であることを型として持っていなかった
///
/// `sizeof "abc"` は C では **4** (char[4]) だが，我々は **4** を返して
/// いた —— 偶然合っているだけで，中身は「ポインタの大きさ」である。
/// `sizeof "!<arch>\n"` なら 9 でなければならないのに 4 を返す。
///
/// estr2() は `ety = 65536` (char *) だけを置き，earr / esz / eaty を
/// 立てていなかった。sizeofn() は `if (earr) n = esz;` で配列を見分ける
/// ので，文字列リテラルは常にポインタとして数えられていた。
///
/// 直しは estr2() で earr / esz / eaty を立てるだけ。式の中での退化
/// (先頭要素へのポインタ) は ety = 65536 のままなので変わらない。
/// 配列型 eaty は単項 & が使う (`&"abc"` は char(*)[4])。
///
/// **これは cc15q で直したものと同じ根である。** cc15q の 2 か所は
/// どちらも「静的な初期化子の中の文字列リテラルが配列として扱われて
/// いない」だった。あのとき初期化子の側だけを直し，式の側 (sizeof と &)
/// を見なかった。docs/stage017-cc.md 27.4 に書いた「族を言語の規則では
/// なく振る舞いで切った」を，**3 度目に踏んだ**ことになる。
/// 今回は規則の側 —— 「文字列リテラルの型は char[n+1] である」——
/// から直している。
///
/// ## 表に出た形
///
/// tcc が我々の OS の上で書庫 (.a) を読めなかった。tccelf.c は
///
///     file_offset = sizeof ARMAG - 1;      ARMAG = "!<arch>\n"
///
/// で最初の見出しの位置を求める。8 のはずが 3 になり，見出しでない
/// 場所を読んで "invalid archive" と言っていた
/// (docs/stage017-cc.md 31 章)。
///
/// **鎖の成果物は変わらない** —— 既存のソースは文字列リテラルの
/// sizeof を使っていない。使うのは tcc の作業場だけである。
///
/// ---- 以下は cc15q から引き継いだ来歴 ----
///
/// @brief C コンパイラ 第 15 世代 その 17。cc15p との差は**2 か所**。
///
/// どちらも「静的な器の初期化子に文字列リテラルが出てくると値が壊れる」
/// という 1 つの誤りの，別々の現れ方である。**通ってしまうが値が違う**
/// という，我々が台帳で bad と呼ぶ状態にあたる
/// (docs/stage017-cc.md 24〜27 章)。
///
/// ## 1 か所目 —— 関数内 static がポインタで受けるとき
///
/// `static char *p = "abc";` の形。予約したローカルシンボルの位置
/// (lsoff) が誰にも埋められず，ポインタが別の場所を指していた。
///
/// 文字列プールの flush は 2 系統ある。関数本体側は spfn の分だけ，
/// gstrflush() は gspn の分だけ lsoff を埋める。ところが gstrflush() は
/// 呼び出し 3 か所すべてが `if (!ginfn)` で守られているので，関数内 static
/// のときは一度も呼ばれない。**任せた先が受け取っていなかった。**
///
/// 直しは本体側の flush の後ろで gspn の分も埋めるだけ (6 行)。静的の
/// 文字列は plocal() で先に積まれるので同じプールの中にあり，gspofs は
/// そのまま使える。
///
/// ## 2 か所目 —— 構造体の中の char 配列で受けるとき
///
/// `static struct { char a[8]; } s = { "abc" };` の形。ginit1() は対象の
/// 型を見ずに t_str をポインタとして扱っていたので，**配列の中身が字の
/// 実体ではなくアドレスになっていた**。関数内かどうかに関係なく，大域の
/// 静的でも同じに壊れる。
///
/// 直しは ginit1() で「対象が配列なら実体を並べる」を t_str の分岐より
/// 前に置くだけ (13 行)。並べ方は ginit() の `char s[] = "abc"` と同じで，
/// 宣言した幅ぶん並べ，残りを 0 で埋める。
///
/// なお `char s[3][8] = { "ab", "cd" }` の形は **まだ直っていない**。
/// ginit() が多次元を平らに畳んだ後は要素型が char になっていて，
/// 「配列」として見えなくなるためである (27 章)。
///
/// **鎖の成果物は 1 バイトも変わらない。** 既存のソースはどちらの形も
/// 使っていない。tcc の tccgen.c と tcctools.c が使うので表に出た。
///
/// ---- さらに以下は cc15p から引き継いだ来歴 ----
///
/// @brief C コンパイラ。ELF リロケータブルオブジェクトを出力する。
///
/// 設計は docs/stage010-c89.md，出力形式は docs/stage008-elf-ld.md，
/// 入力言語の基礎は docs/stage005-sc.md 2 章。補遺の cc11.sc を
/// 出発点とし，**配列に単項 & を適用できるようにした** ものである
/// (Stage 10 の補遺 2)。
///
/// @section layout 第 6 部の 5: 構造体の配置を C89 に揃える
/// cc15o まで，構造体の配置に 2 つの誤りがあった。どちらも我々の世界の
/// 中では辻褄が合う (すべて同じ規則で置く) ので表に出なかったが，
/// **外と混ぜた瞬間に壊れる**類である (docs/stage015-tcc.md 14 章)。
///
///   1. ビットフィールドの記憶単位が常に unsigned (4 バイト) だった。
///      C の規則では**宣言された型**が記憶単位で，unsigned short なら
///      2 バイト，unsigned char なら 1 バイトである
///   2. long long / double の整列が 4 バイトだった。ilp32 (RV32 / tcc /
///      gcc) では 8 である
///
/// あわせて，構造体の大きさを「常に 4 の倍数」から「**自身の整列の
/// 倍数**」へ直した。sizeof(struct { char c; }) は 4 ではなく 1 である。
/// 構造体ごとの整列は salign に持ち，メンバの整列の最大を取る。
///
/// この 3 つめの変更で「構造体の大きさは 4 の倍数」という前提が崩れる。
/// 影響するのは scopy (語ごとの複写) で，端数を半語・バイトで始末する
/// ようにした。書き過ぎると隣を壊すためである。
///
/// @section strinit 第 6 部の 4: 局所の構造体を式で初期化する
/// cc15n まで，`struct S d = 式;` という**局所**の宣言は複写のコードを
/// 出していなかった。linit に構造体の経路が無く，末尾の stval へ落ちて
/// 4 バイトしか書いていない。変数は未初期化のまま残る (黙って誤る)。
///
///   struct P d = 大域;   struct P d = *p;   struct P d = f();
///   union  U d = 大域;
///
/// のどれも落ちていた。波括弧の初期化子 (`= { 1, 2 }`) とスカラは別の
/// 経路なので正しかった。代入 (`d = 式;`) には scopy による正しい経路が
/// あるので，初期化もそれに合わせる。
///
/// これは tcc の自己ホストを塞いでいた誤りである。tcc は宣言の後に
/// 定義が来たとき `struct FuncAttr f = sym->type.ref->f;` で属性を
/// 退避する。ここがゴミになると func_noreturn が偶然立ち，tcc は
/// 「noreturn の呼出しの後」としてコード生成を止めてしまう
/// (docs/stage015-tcc.md 12.22)。
///
/// @section ampfix 補遺 2 の背景
/// 式の中の配列は「先頭要素へのポインタ」へ退化し，退化した式は左辺値で
/// なくなるため，&a が一律にエラー 5 となっていた。C89 では &a は
/// 合法で，値は先頭要素のアドレスと同じ，型が「配列へのポインタ」になる。
/// Stage 11 の stddef.h で offsetof が配列メンバに適用できない
/// (&(((t *)0)->m) の m が配列だと通らない) ことにより判明した。
///
/// 退化の際に **退化前の配列型を eaty へ保存しておき**，& はそれを 1 段
/// ポインタ型にして返す。値は退化した式そのままでよい (配列のアドレスは
/// 先頭要素のアドレスに等しい)。&a + 1 が配列 1 個ぶん進むこと，
/// sizeof(*&a) が配列全体の大きさになることは，既存のポインタ演算と
/// 配列退化の処理で成立する。
///
/// @section escfix 補遺 (cc11) で入ったもの
/// エスケープが \n \t \0 \\ \' \" の 6 種しかなく，C89 7.1.3 が
/// 定める \a \b \f \r \v \? と，8 進・16 進のエスケープが無かった。
/// Stage 11 の libc を書き始めて isspace が \f \r \v を必要とし，そこで
/// 判明した。libc 側で 12 や 13 と数値で書けば通るが，処理系の実装漏れへの
/// 対処を利用側に求めることになるため，処理系を修正する。
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

char src[4194304];         ///< 入力ソース全体 (EOT 0x04 まで読み込む)
char ob[4194304];          ///< 生成バイナリ。後埋め (backpatch) するため一旦ここに溜める

char gname[524288];        ///< 大域記号の名前 (64 バイト固定スロット x 8192)
int gkind[8192];          ///< 種別: 0 = 変数, 1 = 関数
int gty[8192];            ///< 型 (変数は自身の型，関数は返却型)
int gval[8192];           ///< 変数: 絶対アドレス / 関数: 定義済みならコード位置，未定義なら未解決呼出しリストの先頭
int gdef[8192];           ///< 1 = 定義済み。0 のまま入力が終われば未解決の前方参照 (エラー 2)
int garr[8192];           ///< 1 = 配列。式中では先頭要素へのポインタに退化する
int gna[8192];            ///< 引数個数。-1 = 未知 (前方参照で個数がまだ判らない)
int gsz[8192];            ///< 大域変数の大きさ (バイト)。ELF シンボルの st_size になる
int gsta[8192];           ///< 1 = static。ELF のローカルシンボルとして出す
int gtxt[8192];           ///< 1 = 初期値を持ち，実体が .text にある
int gvar[8192];           ///< 1 = 可変長引数を取る関数 (gna は名前つきの個数)
int gllm[8192];           ///< 仮引数の 64 bit 位置のビット表 (bit k = 語 k から
                          ///  64 bit の仮引数が始まる)。実引数の格上げに使う。
                          ///  32 語を超える位置は表せないので宣言時に拒む
int gdbm[8192];           ///< 同じく double の位置 (2 語)
int gflm[8192];           ///< 同じく float の位置 (1 語)
int gused[8192];          ///< 1 = 個数が判らないまま呼出しを出した (前方参照)
int gidx[8192];           ///< 大域記号 -> ELF シンボル番号
int nsta;                 ///< ローカル側へ寄せた static 記号の数
int gcnt;                 ///< 大域記号の登録数

char lname[65536];         ///< ローカル記号の名前 (64 バイト x 1024)。関数ごとに作り直す
int lty[1024];             ///< 型
int loff[1024];            ///< フレームポインタ x8 からのオフセット
int lsg[1024];             ///< 関数内 static: 実体の大域記号番号 (-1 = 普通の局所)
int slocnt;               ///< 関数内 static の通し番号 (別名の一意性はこれが担う)
int larr[1024];            ///< 1 = 配列
int lsz[1024];             ///< 大きさ (バイト)。sizeof が配列全体を返すために要る
int lcnt;                 ///< ローカル記号の登録数
int lblk;                 ///< 現在のブロックの先頭の局所番号。
                          ///< これ未満は外側の宣言で，遮蔽してよい

char sname[131072];         ///< 構造体名 (64 バイト x 2048)
int ssize[2048];           ///< 構造体のサイズ (自身の整列の倍数へ切り上げ済み)
int salign[2048];          ///< 構造体の整列 (メンバの整列の最大。最小 1)
int sunion[2048];          ///< 1 = union。メンバの位置を 0 に固定し，大きさは最大値
int scnt;                 ///< 構造体の登録数。union も同じ表に入る

// typedef は「名前 -> 型」の対応にすぎない。型そのものを増やすわけではないので
// 別表を 1 つ持てば済む。
char tdname[131072];        ///< typedef 名 (64 バイト x 2048)
int tdty[2048];            ///< 対応する型
int tdcnt;                ///< 登録数

// enum 定数も「名前 -> 値」の対応にすぎない。型は int である。
// タグ (enum e { ... } の e) は型の区別を生まないので表に持たない。
char ecname[524288];        ///< 列挙定数の名前 (64 バイト x 8192)
int ecval[8192];           ///< その値
int eccnt;                ///< 登録数

// 配列型。これまでの型は「ポインタの深さ << 16 | 基底」という平坦な表現で，
// 基底は char / int / 構造体 / void しか無かった。多次元配列は「型から型を
// 作る」ので平坦には表せない。基底の t_arr 番以降をこの表の添字に割り当てる。
int aelem[4096];           ///< 要素の型
int acnt[4096];            ///< 要素数
int arrcnt;               ///< 登録数

// 関数型。配列型と同じ考え方で，基底の t_fn 番以降を添字に割り当てる。
// 仮引数の型までは持たない (呼出しの検査は個数だけで行う)。
int frty[1024];            ///< 返却型
int fncnt;                ///< 登録数

char mname[1048576];        ///< メンバ名 (64 バイト x 16384)。全構造体のメンバを 1 本の表に持つ
int msid[16384];           ///< 所属する構造体の番号。探索はこれで絞り込む
int mty[16384];            ///< メンバの型
int mbfw[16384];           ///< ビットフィールドの幅 (0 = 普通のメンバ)
int mbfo[16384];           ///< ビットフィールドのビット位置 (語内)
int moff[16384];           ///< 構造体先頭からのオフセット
int marr[16384];           ///< 1 = 配列メンバ
int msz[16384];            ///< 大きさ (バイト)。sizeof 用
int mcnt;                 ///< メンバの登録数

char tname[72];           ///< 直近に読んだ識別子。記号表の探索はすべてこれを鍵にする
char snam[64];            ///< struct 名の退避先 (tname はメンバ名の解析で上書きされるため)
char sbuf[32768];          ///< 文字列リテラルの組立てバッファ
                          ///< (tcc の予約語表は連結して約 10 KB)
int slen;                 ///< sbuf の有効長

int pos;                  ///< src 内の読取り位置
int tok;                  ///< 現在のトークン種別
int tval;                 ///< 現在のトークンの値 (数値・文字リテラル)。64 bit なら下位語
int tvalhi;               ///< 数値リテラルの上位語 (32 bit に収まるなら 0 か -1)
int fpexd;                ///< 仮数に入り切らず読み捨てた整数部の桁数
                          ///< (10 の冪で lexfp の指数へ戻す)
int coty;                 ///< 定数アドレス式 (cofs) が畳んだ式の型
int tvalfp;               ///< 数値リテラルの種類 (0 = 整数, 1 = double, 2 = float)。
                          ///  1 のとき tvalhi:tval が binary64，2 のとき tval が binary32
int cchi;                 ///< 定数式評価器: 直前の値の上位語 (ccll のときだけ有効)
int ccll;                 ///< 定数式評価器: 1 = 直前の値は 64 bit リテラル由来
int tvalll;               ///< 1 = そのリテラルは 64 bit (LL 接尾辞または 32 bit に収まらない)
int tvalu;                ///< 1 = そのリテラルの型は符号なし
                          ///< (U 接尾辞，または符号つきに収まらない値)
int svtvhi; int svtvll;   ///< lsave / lrest が退避する 64 bit の印
int svtvfp;               ///< lsave / lrest が退避する浮動小数点の印
int outp;                 ///< ob への書込み位置 (= 生成コードのオフセット)
int bssp;                 ///< .bss の割付けポインタ (オブジェクト内オフセット。0 から上向き)

// ---- ELF 出力 ----
// ob は .text の内容だけを保持し，ELF ファイル全体は eb に組み立てて出力する。
// 再配置は「.text 内の位置」「対象シンボル」「種別」「加数」の 4 本の並行配列。
// 対象シンボルの符号化: 0 以上なら大域記号の番号 e，
// 負なら文字列リテラル用のローカルシンボル k を -1 - k で表す。
char eb[8388608];         ///< ELF ファイル全体の組立てバッファ
int ep;                   ///< eb への書込み位置
// 再配置の表は 16384 件 (cc15g で拡張。cc15g.sc の注記を見よ)
int rof[262144];           ///< 再配置: .text 内オフセット
int rsy[262144];           ///< 再配置: 対象シンボル (上記の符号化)
int rty[262144];           ///< 再配置: 種別 (R_RISCV_*)
int rad[262144];           ///< 再配置: 加数
int rcnt;                 ///< 再配置の件数
int lsoff[8192];          ///< 文字列リテラルのローカルシンボル: .text 内オフセット
int nlsym;                ///< ローカルシンボルの数
char stt[524288];          ///< .strtab の内容
int stp;                  ///< stt の有効長
int r_32; int r_jal; int r_hi20; int r_lo12i;   ///< 再配置種別の番号

int ety;                  ///< 直前に解析した式の型
int elv;                  ///< 1 = その式は左辺値 (値ではなくアドレスが手元にある)
int earr;                 ///< 1 = その式は配列オブジェクト (sizeof と単項 & が見る)
int esz;
int ebfw;                 ///< 式: 左辺値がビットフィールドならその幅 (0 = 違う)
int ehi;                  ///< 式: 64 bit の値なら上位語の値番号 (0 = 32 bit の式)。
                          ///  左辺値のときは使わない (上位語は アドレス + 4 にある)
int ebfo;                 ///< 式: そのビット位置
int bfword;               ///< strudef: 詰めている途中の語の moff (-1 = 無い)
int bfbit;                ///< strudef: その単位の次の空きビット
int bfunit;               ///< strudef: 詰めている記憶単位の大きさ (1/2/4)
int eaty;                 ///< earr が 1 のときの退化前の配列型 (単項 & が使う)
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
int swval[2048];           ///< case のラベル値
int swlab[2048];           ///< 対応する IR ラベル番号
int swn;                  ///< 控えた数 (入れ子の switch は同じ配列を積んで使う)
int swdep;                ///< switch の入れ子の深さ。0 なら case / default は誤り
int swdflt;               ///< 現在の switch の default のラベル。-1 = 無し

// goto のラベル表 (関数ごとに作り直す)
char glname[4096];        ///< ラベル名 (64 バイトスロット x 64)
int gllab[64];            ///< 対応する IR ラベル番号
int gldef[64];            ///< 1 = 定義済み (「名前:」が現れた)
int glcnt;                ///< 登録数

// 字句解析器の退避先。1 トークン先を覗いて戻すために使う
// (「x:」がラベルか式か，「(」の次が型かキャストか)
int svpos; int svtok; int svtval;
char svname[64];
int cloff;                ///< 解析中の関数のローカル割付けポインタ
int cmax;                 ///< cloff の最大値。ブロックを抜けると cloff は戻るが，
                          ///< フレームの大きさは最大値で決まる
int cext;                 ///< 1 = この宣言に extern が付いている
int cstat;                ///< 1 = この宣言に static が付いている
int cna;                  ///< 解析中の関数の引数個数 (名前つきのみ)
int cllm;                 ///< 解析中の仮引数並びの 64 bit 位置 (gllm へ写す)
int cdbm;                 ///< 同じく double の位置 (gdbm へ写す)
int cflm;                 ///< 同じく float の位置 (gflm へ写す)
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

int iop[131072];            ///< 命令種別 (c_* のいずれか。c_bin 以上は二項演算)
int ia[131072];             ///< 第 1 オペランド (意味は命令種別による。上表参照)
int ib[131072];             ///< 第 2 オペランド (同上)
int icnt;                 ///< 命令数

int lastu[131072];          ///< 値が最後に使われる命令位置。-1 = 一度も使われない
int vreg[131072];           ///< 割付け結果: >= 0 レジスタ番号 / -1 未割付 / -2-n スピルスロット n
int live[131072];           ///< dce の結果: 1 = 生存 (出力する)
int iret[131072];           ///< CALL の側情報: 構造体を返す呼出しの引取り先
                          ///< (フレームオフセット。0 = 構造体を返さない)

int labpos[16384];         ///< ラベル番号 -> 出力オフセット。-1 = 未確定 (まだ現れていない)
int labcnt;               ///< ラベル数
int lfix[32768];           ///< 前方ラベル参照の後埋め: 命令を書いた出力位置
int lflab[32768];          ///< 同上: 参照先のラベル番号
int lfixn;                ///< 後埋め待ちの件数

char spool[65536];         ///< 文字列リテラルの実体 (関数単位。本体の後ろへ出力する)
int spcnt;                ///< spool の有効長
int spfix[2048];           ///< 文字列アドレスの後埋め: li を書いた出力位置 (lui 側)
int spofs[2048];           ///< 同上: spool 内オフセット
int spfn;                 ///< 後埋め待ちの件数
int gspsym[4096];          ///< 大域初期化子の文字列: 予約したローカルシンボル番号
int gspofs[4096];          ///< 同上: spool 内オフセット
int gspn;                 ///< 同上の件数
int ginfn;                ///< 1 = 関数本体の解析中に ginit を呼んでいる (関数内 static の
                          ///  初期化子)。文字列の flush は関数末尾に任せる (spool は関数単位)

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
int k_register; int k_auto;   ///< 記憶域クラス。読み捨てる (docs/stage014-external.md 5.2)
int k_unsigned; int k_signed; int k_short; int k_long;
int k_float; int k_double;
int t_void;               ///< void の型番号 (構造体の 2..257 と離した値)
int t_arr;                ///< 配列型の基底番号の起点 (t_arr + k が配列型 k)
int t_fn;                 ///< 関数型の基底番号の起点 (t_fn + k が関数型 k)
int t_schar;              ///< signed char (素の char は符号なしなので別番号)
int t_short; int t_ushort; int t_uint;
int t_llong; int t_ullong;   ///< 64 bit 整数 (第 2 部。上位・下位に分解して扱う)
int t_float; int t_double;   ///< 浮動小数点 (第 3 部)。値は bit の並びとして運ぶ
int o_asn; int o_lt; int o_gt; int o_add; int o_sub; int o_mul; int o_div; int o_mod;
int o_amp; int o_or; int o_xor; int o_not; int o_lp; int o_rp; int o_lb; int o_rb;
int o_lc; int o_rc; int o_semi; int o_comma; int o_dot;
int o_eq; int o_ne; int o_le; int o_ge; int o_shl; int o_shr; int o_aa; int o_oo; int o_arrow;
int o_que; int o_col; int o_inc; int o_dec; int o_ellip; int o_tilde;
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
int b_mulhu;              ///< 符号なし乗算の上位語 (64 bit の乗算に要る)
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
  k_register = 92; k_auto = 93;
  k_float = 94; k_double = 95;   // 92/93 は register/auto が先に使っている
  o_asn = 30; o_lt = 31; o_gt = 32; o_add = 33; o_sub = 34; o_mul = 35; o_div = 36; o_mod = 37;
  o_amp = 38; o_or = 39; o_xor = 40; o_not = 41; o_lp = 42; o_rp = 43; o_lb = 44; o_rb = 45;
  o_lc = 46; o_rc = 47; o_semi = 48; o_comma = 49; o_dot = 50;
  o_eq = 51; o_ne = 52; o_le = 53; o_ge = 54; o_shl = 55; o_shr = 56; o_aa = 57; o_oo = 58; o_arrow = 59;
  o_que = 60; o_col = 61; o_inc = 62; o_dec = 63; o_ellip = 64; o_tilde = 65;
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
  b_mulhu = 23;
  b_ult = 19; b_ugt = 20; b_ule = 21; b_uge = 22;
  // RISC-V ELF psABI の再配置種別
  r_32 = 1; r_jal = 17; r_hi20 = 26; r_lo12i = 27;
  t_void = 300;
  t_arr = 1024;
  t_schar = 301; t_short = 302; t_ushort = 303; t_uint = 304;
  t_llong = 305; t_ullong = 306;
  t_float = 307; t_double = 308;
  t_fn = 2048;
  return 0;
}

// ---- 名前操作 ----
// 記号表の名前は 64 バイト固定スロットに 0 詰めで格納する。可変長にすると
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
/// @param s 置く名前 (64 バイト未満)
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
/// @brief 名前スロットの複写 (常に 64 バイト固定)。
/// @param d 複写先スロット
/// @param s 複写元スロット
/// @return 常に 0
int copyn(char *d, char *s) {
  int i;
  i = 0;
  while (i < 64) { d[i] = s[i]; i = i + 1; }
  return 0;
}

// ---- 字句解析 (scc と同一) ----
// 1 文字先読みのみで済む単純な字句。トークンは tok / tval / tname に入る。

/// @brief 現在位置の 1 文字を返す (消費しない)。
int getch() { return src[pos]; }
/// @brief 読取り位置を 1 進める。
int adv() { pos = pos + 1; return 0; }

/// @brief 空白か (SP TAB CR LF)。
// C89 5.2.1 の空白 6 種 (space HT CR LF VT FF)。stab.def が FF を区切りに使う
int isws(int c) {
  return c == 32 || c == 9 || c == 13 || c == 10 || c == 11 || c == 12;
}
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
/// @brief 16 進数字か (大文字・小文字とも)。
int ishex(int c) { return isdig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/// @brief 16 進数字を数値へ。
/// @param c '0'..'9' または 'a'..'f' または 'A'..'F'
/// @return 0..15。'a' は 97 なので 87 を引くと 10，'A' は 65 なので 55 を引くと 10 になる
int hexv(int c) {
  if (c >= 'a') return c - 87;
  if (c > '9') return c - 55;
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

/// @brief v * 10 の上位 32 bit を求める (v は符号なしとみなす)。
/// @note cc 自身に 64 bit が無いので，16 bit ずつに割って組む。
int mulhi10(int v) {
  int lo; int hi;
  lo = (v & 0xffff) * 10;
  hi = ((v >> 16) & 0xffff) * 10 + ((lo >> 16) & 0xffff);
  return (hi >> 16) & 0xffff;
}

/// @brief 符号なしとみなした大小比較 (a <u b)。
/// @note 桁上げの検出に要る。符号つきで比べると 2^31 をまたぐ値で誤る。
int ultc(int a, int b) {
  unsigned x; unsigned y;
  x = a;
  y = b;
  return x < y;
}


// ---- コンパイル時のソフト浮動小数点 (第 3 部) ----
// 10 進の浮動小数点リテラルを IEEE 754 binary64 の bit の並びへ変換する。
// 実行時支援 (rtfp.c) と同じ演算だが，**これはコンパイラ自身に埋め込む**。
// リテラルの変換はコンパイル時の仕事だからである。
//
// 64 bit の / と % をここで使ってはならない (cc の実行形式は rt64.o を
// リンクしないので，未定義シンボルになる)。除算は引き算とシフトで組む。
//
// 10 の冪は表で持つ (それぞれ正しく丸めた bit の並び)。掛けるか割るかの
// 1 回だけ丸めが入るので，リテラル変換の誤差は最大でも 1.5 ulp 程度に
// 収まる。ホストの strtod (0.5 ulp) とは末尾 bit が違いうる。

unsigned long long p10tab[309] = {
  0x3ff0000000000000ULL, 0x4024000000000000ULL, 0x4059000000000000ULL, 0x408f400000000000ULL,
  0x40c3880000000000ULL, 0x40f86a0000000000ULL, 0x412e848000000000ULL, 0x416312d000000000ULL,
  0x4197d78400000000ULL, 0x41cdcd6500000000ULL, 0x4202a05f20000000ULL, 0x42374876e8000000ULL,
  0x426d1a94a2000000ULL, 0x42a2309ce5400000ULL, 0x42d6bcc41e900000ULL, 0x430c6bf526340000ULL,
  0x4341c37937e08000ULL, 0x4376345785d8a000ULL, 0x43abc16d674ec800ULL, 0x43e158e460913d00ULL,
  0x4415af1d78b58c40ULL, 0x444b1ae4d6e2ef50ULL, 0x4480f0cf064dd592ULL, 0x44b52d02c7e14af6ULL,
  0x44ea784379d99db4ULL, 0x45208b2a2c280291ULL, 0x4554adf4b7320335ULL, 0x4589d971e4fe8402ULL,
  0x45c027e72f1f1281ULL, 0x45f431e0fae6d721ULL, 0x46293e5939a08ceaULL, 0x465f8def8808b024ULL,
  0x4693b8b5b5056e17ULL, 0x46c8a6e32246c99cULL, 0x46fed09bead87c03ULL, 0x4733426172c74d82ULL,
  0x476812f9cf7920e3ULL, 0x479e17b84357691bULL, 0x47d2ced32a16a1b1ULL, 0x48078287f49c4a1dULL,
  0x483d6329f1c35ca5ULL, 0x48725dfa371a19e7ULL, 0x48a6f578c4e0a061ULL, 0x48dcb2d6f618c879ULL,
  0x4911efc659cf7d4cULL, 0x49466bb7f0435c9eULL, 0x497c06a5ec5433c6ULL, 0x49b18427b3b4a05cULL,
  0x49e5e531a0a1c873ULL, 0x4a1b5e7e08ca3a8fULL, 0x4a511b0ec57e649aULL, 0x4a8561d276ddfdc0ULL,
  0x4ababa4714957d30ULL, 0x4af0b46c6cdd6e3eULL, 0x4b24e1878814c9ceULL, 0x4b5a19e96a19fc41ULL,
  0x4b905031e2503da9ULL, 0x4bc4643e5ae44d13ULL, 0x4bf97d4df19d6057ULL, 0x4c2fdca16e04b86dULL,
  0x4c63e9e4e4c2f344ULL, 0x4c98e45e1df3b015ULL, 0x4ccf1d75a5709c1bULL, 0x4d03726987666191ULL,
  0x4d384f03e93ff9f5ULL, 0x4d6e62c4e38ff872ULL, 0x4da2fdbb0e39fb47ULL, 0x4dd7bd29d1c87a19ULL,
  0x4e0dac74463a989fULL, 0x4e428bc8abe49f64ULL, 0x4e772ebad6ddc73dULL, 0x4eacfa698c95390cULL,
  0x4ee21c81f7dd43a7ULL, 0x4f16a3a275d49491ULL, 0x4f4c4c8b1349b9b5ULL, 0x4f81afd6ec0e1411ULL,
  0x4fb61bcca7119916ULL, 0x4feba2bfd0d5ff5bULL, 0x502145b7e285bf99ULL, 0x50559725db272f7fULL,
  0x508afcef51f0fb5fULL, 0x50c0de1593369d1bULL, 0x50f5159af8044462ULL, 0x512a5b01b605557bULL,
  0x516078e111c3556dULL, 0x5194971956342ac8ULL, 0x51c9bcdfabc1357aULL, 0x5200160bcb58c16cULL,
  0x52341b8ebe2ef1c7ULL, 0x526922726dbaae39ULL, 0x529f6b0f092959c7ULL, 0x52d3a2e965b9d81dULL,
  0x53088ba3bf284e24ULL, 0x533eae8caef261adULL, 0x53732d17ed577d0cULL, 0x53a7f85de8ad5c4fULL,
  0x53ddf67562d8b363ULL, 0x5412ba095dc7701eULL, 0x5447688bb5394c25ULL, 0x547d42aea2879f2eULL,
  0x54b249ad2594c37dULL, 0x54e6dc186ef9f45cULL, 0x551c931e8ab87173ULL, 0x5551dbf316b346e8ULL,
  0x558652efdc6018a2ULL, 0x55bbe7abd3781ecaULL, 0x55f170cb642b133fULL, 0x5625ccfe3d35d80eULL,
  0x565b403dcc834e12ULL, 0x569108269fd210cbULL, 0x56c54a3047c694feULL, 0x56fa9cbc59b83a3dULL,
  0x5730a1f5b8132466ULL, 0x5764ca732617ed80ULL, 0x5799fd0fef9de8e0ULL, 0x57d03e29f5c2b18cULL,
  0x58044db473335defULL, 0x583961219000356bULL, 0x586fb969f40042c5ULL, 0x58a3d3e2388029bbULL,
  0x58d8c8dac6a0342aULL, 0x590efb1178484135ULL, 0x59435ceaeb2d28c1ULL, 0x59783425a5f872f1ULL,
  0x59ae412f0f768fadULL, 0x59e2e8bd69aa19ccULL, 0x5a17a2ecc414a03fULL, 0x5a4d8ba7f519c84fULL,
  0x5a827748f9301d32ULL, 0x5ab7151b377c247eULL, 0x5aecda62055b2d9eULL, 0x5b22087d4358fc82ULL,
  0x5b568a9c942f3ba3ULL, 0x5b8c2d43b93b0a8cULL, 0x5bc19c4a53c4e697ULL, 0x5bf6035ce8b6203dULL,
  0x5c2b843422e3a84dULL, 0x5c6132a095ce4930ULL, 0x5c957f48bb41db7cULL, 0x5ccadf1aea12525bULL,
  0x5d00cb70d24b7379ULL, 0x5d34fe4d06de5057ULL, 0x5d6a3de04895e46dULL, 0x5da066ac2d5daec4ULL,
  0x5dd4805738b51a75ULL, 0x5e09a06d06e26112ULL, 0x5e400444244d7cabULL, 0x5e7405552d60dbd6ULL,
  0x5ea906aa78b912ccULL, 0x5edf485516e7577fULL, 0x5f138d352e5096afULL, 0x5f48708279e4bc5bULL,
  0x5f7e8ca3185deb72ULL, 0x5fb317e5ef3ab327ULL, 0x5fe7dddf6b095ff1ULL, 0x601dd55745cbb7edULL,
  0x6052a5568b9f52f4ULL, 0x60874eac2e8727b1ULL, 0x60bd22573a28f19dULL, 0x60f2357684599702ULL,
  0x6126c2d4256ffcc3ULL, 0x615c73892ecbfbf4ULL, 0x6191c835bd3f7d78ULL, 0x61c63a432c8f5cd6ULL,
  0x61fbc8d3f7b3340cULL, 0x62315d847ad00087ULL, 0x6265b4e5998400a9ULL, 0x629b221effe500d4ULL,
  0x62d0f5535fef2084ULL, 0x630532a837eae8a5ULL, 0x633a7f5245e5a2cfULL, 0x63708f936baf85c1ULL,
  0x63a4b378469b6732ULL, 0x63d9e056584240feULL, 0x64102c35f729689fULL, 0x6444374374f3c2c6ULL,
  0x647945145230b378ULL, 0x64af965966bce056ULL, 0x64e3bdf7e0360c36ULL, 0x6518ad75d8438f43ULL,
  0x654ed8d34e547314ULL, 0x6583478410f4c7ecULL, 0x65b819651531f9e8ULL, 0x65ee1fbe5a7e7861ULL,
  0x6622d3d6f88f0b3dULL, 0x665788ccb6b2ce0cULL, 0x668d6affe45f818fULL, 0x66c262dfeebbb0f9ULL,
  0x66f6fb97ea6a9d38ULL, 0x672cba7de5054486ULL, 0x6761f48eaf234ad4ULL, 0x679671b25aec1d89ULL,
  0x67cc0e1ef1a724ebULL, 0x680188d357087713ULL, 0x6835eb082cca94d7ULL, 0x686b65ca37fd3a0dULL,
  0x68a11f9e62fe4448ULL, 0x68d56785fbbdd55aULL, 0x690ac1677aad4ab1ULL, 0x6940b8e0acac4eafULL,
  0x6974e718d7d7625aULL, 0x69aa20df0dcd3af1ULL, 0x69e0548b68a044d6ULL, 0x6a1469ae42c8560cULL,
  0x6a498419d37a6b8fULL, 0x6a7fe52048590673ULL, 0x6ab3ef342d37a408ULL, 0x6ae8eb0138858d0aULL,
  0x6b1f25c186a6f04cULL, 0x6b537798f4285630ULL, 0x6b88557f31326bbbULL, 0x6bbe6adefd7f06aaULL,
  0x6bf302cb5e6f642aULL, 0x6c27c37e360b3d35ULL, 0x6c5db45dc38e0c82ULL, 0x6c9290ba9a38c7d1ULL,
  0x6cc734e940c6f9c6ULL, 0x6cfd022390f8b837ULL, 0x6d3221563a9b7323ULL, 0x6d66a9abc9424febULL,
  0x6d9c5416bb92e3e6ULL, 0x6dd1b48e353bce70ULL, 0x6e0621b1c28ac20cULL, 0x6e3baa1e332d728fULL,
  0x6e714a52dffc6799ULL, 0x6ea59ce797fb817fULL, 0x6edb04217dfa61dfULL, 0x6f10e294eebc7d2cULL,
  0x6f451b3a2a6b9c76ULL, 0x6f7a6208b5068394ULL, 0x6fb07d457124123dULL, 0x6fe49c96cd6d16ccULL,
  0x7019c3bc80c85c7fULL, 0x70501a55d07d39cfULL, 0x708420eb449c8843ULL, 0x70b9292615c3aa54ULL,
  0x70ef736f9b3494e9ULL, 0x7123a825c100dd11ULL, 0x7158922f31411456ULL, 0x718eb6bafd91596bULL,
  0x71c33234de7ad7e3ULL, 0x71f7fec216198ddcULL, 0x722dfe729b9ff153ULL, 0x7262bf07a143f6d4ULL,
  0x72976ec98994f489ULL, 0x72cd4a7bebfa31abULL, 0x73024e8d737c5f0bULL, 0x7336e230d05b76cdULL,
  0x736c9abd04725481ULL, 0x73a1e0b622c774d0ULL, 0x73d658e3ab795204ULL, 0x740bef1c9657a686ULL,
  0x74417571ddf6c814ULL, 0x7475d2ce55747a18ULL, 0x74ab4781ead1989eULL, 0x74e10cb132c2ff63ULL,
  0x75154fdd7f73bf3cULL, 0x754aa3d4df50af0bULL, 0x7580a6650b926d67ULL, 0x75b4cffe4e7708c0ULL,
  0x75ea03fde214caf1ULL, 0x7620427ead4cfed6ULL, 0x7654531e58a03e8cULL, 0x768967e5eec84e2fULL,
  0x76bfc1df6a7a61bbULL, 0x76f3d92ba28c7d15ULL, 0x7728cf768b2f9c5aULL, 0x775f03542dfb8370ULL,
  0x779362149cbd3226ULL, 0x77c83a99c3ec7eb0ULL, 0x77fe494034e79e5cULL, 0x7832edc82110c2f9ULL,
  0x7867a93a2954f3b8ULL, 0x789d9388b3aa30a5ULL, 0x78d27c35704a5e67ULL, 0x79071b42cc5cf601ULL,
  0x793ce2137f743382ULL, 0x79720d4c2fa8a031ULL, 0x79a6909f3b92c83dULL, 0x79dc34c70a777a4dULL,
  0x7a11a0fc668aac70ULL, 0x7a46093b802d578cULL, 0x7a7b8b8a6038ad6fULL, 0x7ab137367c236c65ULL,
  0x7ae585041b2c477fULL, 0x7b1ae64521f7595eULL, 0x7b50cfeb353a97dbULL, 0x7b8503e602893dd2ULL,
  0x7bba44df832b8d46ULL, 0x7bf06b0bb1fb384cULL, 0x7c2485ce9e7a065fULL, 0x7c59a742461887f6ULL,
  0x7c9008896bcf54faULL, 0x7cc40aabc6c32a38ULL, 0x7cf90d56b873f4c7ULL, 0x7d2f50ac6690f1f8ULL,
  0x7d63926bc01a973bULL, 0x7d987706b0213d0aULL, 0x7dce94c85c298c4cULL, 0x7e031cfd3999f7b0ULL,
  0x7e37e43c8800759cULL, 0x7e6ddd4baa009303ULL, 0x7ea2aa4f4a405be2ULL, 0x7ed754e31cd072daULL,
  0x7f0d2a1be4048f90ULL, 0x7f423a516e82d9baULL, 0x7f76c8e5ca239029ULL, 0x7fac7b1f3cac7433ULL,
  0x7fe1ccf385ebc8a0ULL,
};

int cnlz(unsigned long long x) {
  int n;
  n = 0;
  if ((x >> 32) == 0) { n = n + 32; x = x << 32; }
  if ((x >> 48) == 0) { n = n + 16; x = x << 16; }
  if ((x >> 56) == 0) { n = n + 8; x = x << 8; }
  if ((x >> 60) == 0) { n = n + 4; x = x << 4; }
  if ((x >> 62) == 0) { n = n + 2; x = x << 2; }
  if ((x >> 63) == 0) { n = n + 1; }
  return n;
}

/// @brief 仮数 sig (0 でない) と 2 の冪 e から binary64 を組む (最近接偶数)。
unsigned long long cdnorm(int sg, unsigned long long sig, int e) {
  int sh; int d; int be;
  unsigned long long rem; unsigned long long half;
  if (sig == 0ULL) return ((unsigned long long)(unsigned)sg) << 63;
  sh = 63 - cnlz(sig);
  be = e + sh + 1075 - 52;
  if (be <= 0) {
    d = 0 - 1074 - e;
    if (d <= 0) {
      if (0 - d >= 64) return ((unsigned long long)(unsigned)sg) << 63;
      return (((unsigned long long)(unsigned)sg) << 63) | (sig << (0 - d));
    }
    if (d > 64) return ((unsigned long long)(unsigned)sg) << 63;
    if (d == 64) {
      if (sig > (1ULL << 63))
        return (((unsigned long long)(unsigned)sg) << 63) | 1ULL;
      return ((unsigned long long)(unsigned)sg) << 63;
    }
    half = 1ULL << (d - 1);
    rem = sig & ((half << 1) - 1ULL);
    sig = sig >> d;
    if (rem > half) sig = sig + 1ULL;
    else if (rem == half) sig = sig + (sig & 1ULL);
    if ((sig >> 52) != 0ULL)
      return (((unsigned long long)(unsigned)sg) << 63) | (1ULL << 52);
    return (((unsigned long long)(unsigned)sg) << 63) | sig;
  }
  if (sh > 52) {
    d = sh - 52;
    half = 1ULL << (d - 1);
    rem = sig & ((half << 1) - 1ULL);
    sig = sig >> d;
    e = e + d;
    if (rem > half) sig = sig + 1ULL;
    else if (rem == half) sig = sig + (sig & 1ULL);
    if ((sig >> 53) != 0ULL) { sig = sig >> 1; e = e + 1; }
  } else if (sh < 52) {
    d = 52 - sh;
    sig = sig << d;
    e = e - d;
  }
  be = e + 1075;
  if (be >= 2047)
    return (((unsigned long long)(unsigned)sg) << 63) | (2047ULL << 52);
  return (((unsigned long long)(unsigned)sg) << 63)
         | (((unsigned long long)(unsigned)be) << 52)
         | (sig & 0xfffffffffffffULL);
}

/// @brief 64 bit 整数 (符号なし) から binary64 へ。
unsigned long long cu2d(unsigned long long v) {
  if (v == 0ULL) return 0ULL;
  return cdnorm(0, v, 0);
}

/// @brief binary64 どうしの乗算 (最近接偶数)。normal どうし専用。
unsigned long long cdmul(unsigned long long a, unsigned long long b) {
  int sg; int ea; int eb; int drop;
  unsigned long long ma; unsigned long long mb;
  unsigned long long a0; unsigned long long a1;
  unsigned long long b0; unsigned long long b1;
  unsigned long long p00; unsigned long long p01;
  unsigned long long p10; unsigned long long p11;
  unsigned long long mid; unsigned long long lo; unsigned long long hi;
  unsigned long long st;
  if ((a & 0x7fffffffffffffffULL) == 0ULL) return 0ULL;
  if ((b & 0x7fffffffffffffffULL) == 0ULL) return 0ULL;
  sg = (int)((a >> 63) ^ (b >> 63));
  ea = (int)((a >> 52) & 2047ULL);
  eb = (int)((b >> 52) & 2047ULL);
  if (ea == 2047) return (((unsigned long long)(unsigned)sg) << 63) | (2047ULL << 52);
  if (eb == 2047) return (((unsigned long long)(unsigned)sg) << 63) | (2047ULL << 52);
  ma = a & 0xfffffffffffffULL;
  mb = b & 0xfffffffffffffULL;
  if (ea == 0) ea = 0 - 1074; else { ma = ma | (1ULL << 52); ea = ea - 1075; }
  if (eb == 0) eb = 0 - 1074; else { mb = mb | (1ULL << 52); eb = eb - 1075; }
  a0 = ma & 0xffffffffULL; a1 = ma >> 32;
  b0 = mb & 0xffffffffULL; b1 = mb >> 32;
  p00 = a0 * b0; p01 = a0 * b1; p10 = a1 * b0; p11 = a1 * b1;
  mid = (p00 >> 32) + (p01 & 0xffffffffULL) + (p10 & 0xffffffffULL);
  hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  lo = (mid << 32) | (p00 & 0xffffffffULL);
  if (hi != 0ULL) {
    drop = 64 - cnlz(hi);
    st = lo & ((1ULL << drop) - 1ULL);
    hi = (hi << (64 - drop)) | (lo >> drop);
    if (st != 0ULL) hi = hi | 1ULL;
    return cdnorm(sg, hi, ea + eb + drop);
  }
  return cdnorm(sg, lo, ea + eb);
}

/// @brief binary64 どうしの除算 (最近接偶数)。normal どうし専用。
unsigned long long cddiv(unsigned long long a, unsigned long long b) {
  int sg; int ea; int eb; int i; int sh;
  unsigned long long ma; unsigned long long mb;
  unsigned long long q; unsigned long long r;
  if ((a & 0x7fffffffffffffffULL) == 0ULL) return 0ULL;
  sg = (int)((a >> 63) ^ (b >> 63));
  ea = (int)((a >> 52) & 2047ULL);
  eb = (int)((b >> 52) & 2047ULL);
  ma = a & 0xfffffffffffffULL;
  mb = b & 0xfffffffffffffULL;
  if (ea == 0) ea = 0 - 1074; else { ma = ma | (1ULL << 52); ea = ea - 1075; }
  if (eb == 0) eb = 0 - 1074; else { mb = mb | (1ULL << 52); eb = eb - 1075; }
  sh = 52 - (63 - cnlz(ma));
  if (sh > 0) { ma = ma << sh; ea = ea - sh; }
  sh = 52 - (63 - cnlz(mb));
  if (sh > 0) { mb = mb << sh; eb = eb - sh; }
  q = 0ULL; r = ma;
  i = 0;
  while (i < 56) {
    q = q << 1;
    if (r >= mb) { r = r - mb; q = q | 1ULL; }
    r = r << 1;
    i = i + 1;
  }
  if (r != 0ULL) q = q | 1ULL;
  return cdnorm(sg, q, ea - eb - 55);
}

/// @brief binary64 から binary32 (bit の並び) へ (最近接偶数)。
int cd2f(unsigned long long a) {
  int sg; int e; int sh; int d; int be;
  unsigned long long m;
  unsigned long long rem; unsigned long long half;
  sg = (int)(a >> 63);
  e = (int)((a >> 52) & 2047ULL);
  m = a & 0xfffffffffffffULL;
  if (e == 2047) {
    if (m != 0ULL) return 0x7fc00000;
    return (sg << 31) | 0x7f800000;
  }
  if (e == 0) { if (m == 0ULL) return sg << 31; e = 0 - 1074; }
  else { m = m | (1ULL << 52); e = e - 1075; }
  sh = 63 - cnlz(m);
  be = e + sh - 23 + 150;
  if (be <= 0) {
    d = 0 - 149 - e;
    if (d <= 0) {
      if (0 - d >= 32) return sg << 31;
      return (sg << 31) | (int)(m << (0 - d));
    }
    if (d > 64) return sg << 31;
    if (d == 64) {
      if (m > (1ULL << 63)) return (sg << 31) | 1;
      return sg << 31;
    }
    half = 1ULL << (d - 1);
    rem = m & ((half << 1) - 1ULL);
    m = m >> d;
    if (rem > half) m = m + 1ULL;
    else if (rem == half) m = m + (m & 1ULL);
    if ((m >> 23) != 0ULL) return (sg << 31) | 0x00800000;
    return (sg << 31) | (int)m;
  }
  if (sh > 23) {
    d = sh - 23;
    half = 1ULL << (d - 1);
    rem = m & ((half << 1) - 1ULL);
    m = m >> d;
    e = e + d;
    if (rem > half) m = m + 1ULL;
    else if (rem == half) m = m + (m & 1ULL);
    if ((m >> 24) != 0ULL) { m = m >> 1; e = e + 1; }
  } else if (sh < 23) {
    d = 23 - sh;
    m = m << d;
    e = e - d;
  }
  be = e + 150;
  if (be >= 255) return (sg << 31) | 0x7f800000;
  return (sg << 31) | (be << 23) | ((int)m & 0x7fffff);
}

/// @brief 10 進の仮数 sig と 10 の冪 dexp から binary64 を作る。
unsigned long long cdec2d(unsigned long long sig, int dexp) {
  unsigned long long d;
  if (sig == 0ULL) return 0ULL;
  d = cu2d(sig);
  if (dexp > 0) {
    if (dexp > 308) return 0x7ff0000000000000ULL;   // 表の外は無限大
    return cdmul(d, p10tab[dexp]);
  }
  if (dexp < 0) {
    if (dexp < 0 - 308) {
      // 表の外。2 回に分けて割る (それでも消えるなら 0 になる)
      d = cddiv(d, p10tab[308]);
      dexp = dexp + 308;
      if (dexp < 0 - 308) return 0ULL;
    }
    return cddiv(d, p10tab[0 - dexp]);
  }
  return d;
}

/// @brief 整数 v (非負) を binary64 にして tvalhi:tval へ置く (ginit1 用)。
int lexi2d(int v) {
  unsigned long long d;
  d = cdec2d((unsigned long long)(unsigned)v, 0);
  tval = (int)d;
  tvalhi = (int)(d >> 32);
  tvalfp = 1;
  return 0;
}

/// @brief 浮動小数点リテラルの続き (小数部・指数・接尾辞) を読む。
/// @return 常に 0
/// @note 呼ばれた時点で整数部が tvalhi:tval にある。10 進を binary64 へ
///       変換し，tvalhi:tval を bit の並びに置き換える (tvalfp = 1)。
///       f 接尾辞なら binary32 に落とす (tvalfp = 2)。
///       仮数は 64 bit ぶんまで読み，それより下の桁は捨てる (1 ulp 未満)。
int lexfp() {
  int d; int fdig; int xv; int xneg;
  unsigned long long sig;
  sig = ((unsigned long long)(unsigned)tvalhi << 32)
        | (unsigned long long)(unsigned)tval;
  fdig = 0;
  if (getch() == '.') {
    adv();
    while (isdig(getch())) {
      d = getch() - '0';
      if (sig < 1844674407370955161ULL) {   // (2^64-1)/10 を超えない範囲で読む
        sig = sig * 10ULL + (unsigned long long)d;
        fdig = fdig + 1;
      }
      adv();
    }
  }
  xv = 0;
  xneg = 0;
  if (getch() == 'e' || getch() == 'E') {
    adv();
    if (getch() == '+') adv();
    else if (getch() == '-') { xneg = 1; adv(); }
    if (!isdig(getch())) exit(1);
    while (isdig(getch())) {
      if (xv < 10000) xv = xv * 10 + (getch() - '0');
      adv();
    }
    if (xneg) xv = 0 - xv;
  }
  sig = cdec2d(sig, xv - fdig + fpexd);
  tvalfp = 1;
  if (getch() == 'f' || getch() == 'F') {
    adv();
    tval = cd2f(sig);
    tvalhi = 0;
    tvalfp = 2;
  } else {
    if (getch() == 'l' || getch() == 'L') adv();   // long double = double
    tval = (int)sig;
    tvalhi = (int)(sig >> 32);
  }
  tvalll = 0;
  tvalu = 0;
  tok = t_num;
  return 0;
}

/// @brief 整数リテラルを読み tval へ入れる (10 進 / 0x 16 進)。
/// @return 常に 0
/// @note 10 進に '.' か指数が続けば浮動小数点リテラル (lexfp へ)。
int lexnum() {
  int nl; int d; int ov; int p0;
  unsigned long long s;
  tval = 0;
  tvalhi = 0;
  tvalll = 0;
  tvalu = 0;
  tvalfp = 0;
  fpexd = 0;
  ov = 0;                    // 1 = 32 bit をあふれた (上位語が要る)
  if (getch() == '0' && src[pos + 1] == 'x') {
    adv(); adv();
    if (!ishex(getch())) exit(1);
    while (ishex(getch())) {
      d = hexv(getch());
      // 16 倍は 4 bit の左シフト。あふれた 4 bit が上位語へ入る
      if (tval & 0xf0000000) ov = 1;
      tvalhi = (tvalhi << 4) | ((tval >> 28) & 15);
      tval = (tval << 4) | d;
      adv();
    }
  } else {
    // 10 進。64 bit に収まる範囲で読み，あふれる桁は読み捨てて数だけ
    // 数える (浮動小数点なら 10 の冪で戻す)。2^96 のような大きな
    // リテラル (tccpp の基数変換の重み) は仮数の頭 60 bit 強が残れば
    // double の丸めに足りる
    p0 = pos;
    s = 0ULL;
    while (isdig(getch())) {
      d = getch() - '0';
      if (s >= 1844674407370955161ULL) fpexd = fpexd + 1;
      else s = s * 10ULL + (unsigned long long)d;
      adv();
    }
    if (getch() != '.' && getch() != 'e' && getch() != 'E'
        && src[p0] == '0' && pos - p0 > 1) {
      // 8 進 (先頭 0 で 2 桁以上)。'.' や指数が続くときだけ 10 進と
      // 読む (C89 3.1.3.2)。10 進として読んだ値は捨てて読み直す
      s = 0ULL;
      while (p0 < pos) {
        d = src[p0] - '0';
        if (d > 7) exit(1);
        s = (s << 3) | (unsigned long long)d;
        p0 = p0 + 1;
      }
      fpexd = 0;
    }
    tval = (int)s;
    tvalhi = (int)(s >> 32);
    if (tvalhi) ov = 1;
  }
  // 10 進の仮数に '.' か指数が続けば浮動小数点リテラルである。
  // ここまでに読んだ整数部 (tvalhi:tval) をそのまま仮数の頭として使う
  if (getch() == '.' || getch() == 'e' || getch() == 'E') return lexfp();
  if (ov) tvalll = 1;
  // 接尾辞。l は 1 個なら int と同じ幅，2 個なら 64 bit
  nl = 0;
  while (getch() == 'u' || getch() == 'U' || getch() == 'l' || getch() == 'L') {
    if (getch() == 'l' || getch() == 'L') nl = nl + 1;
    if (getch() == 'u' || getch() == 'U') tvalu = 1;
    adv();
  }
  if (nl > 1) tvalll = 1;
  // 整数定数の型は「値が収まる最初のもの」で決まる (C89 6.1.3.2)。
  // int に収まらない値は unsigned int になる。**64 bit へ広げるときに
  // 符号拡張と零拡張の違いが出る**ので，ここを誤ると値が黙って変わる
  // (第 6 部の実測: tcc の value64 が 0x80000000 で狂っていた)
  if (tvalll) {
    if (tvalhi & 0x80000000) tvalu = 1;
  } else {
    if (tval & 0x80000000) tvalu = 1;
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
    if (n == 63) exit(1);
    tname[n] = getch();
    n = n + 1;
    adv();
  }
  while (n < 64) { tname[n] = 0; n = n + 1; }
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
  if (streq(tname, "register")) { tok = k_register; return 0; }
  if (streq(tname, "auto")) { tok = k_auto; return 0; }
  // inline は記憶域クラスと同じ扱いで読み捨てる (tcc が 57 箇所で使う)
  if (streq(tname, "inline")) { tok = k_auto; return 0; }
  if (streq(tname, "unsigned")) { tok = k_unsigned; return 0; }
  if (streq(tname, "signed")) { tok = k_signed; return 0; }
  if (streq(tname, "short")) { tok = k_short; return 0; }
  if (streq(tname, "long")) { tok = k_long; return 0; }
  if (streq(tname, "float")) { tok = k_float; return 0; }
  if (streq(tname, "double")) { tok = k_double; return 0; }
  tok = t_id;
  return 0;
}

/// @brief 文字リテラルを読み，その文字コードを tval へ入れる。
/// @return 常に 0
int lexchr() {
  adv();
  // 64 bit と浮動小数点の印は数値リテラルが立てる。文字リテラルでは
  // 必ず消す (消し忘れると直前のリテラルの印が残る)
  tvalhi = 0;
  tvalfp = 0;
  tvalll = 0;
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
  slen = 0;
  // 隣接する文字列リテラルは 1 本に連結する (C89 3.1.4)。
  // 閉じ引用符の後ろの空白と行コメントを飛ばし，次も引用符なら続けて読む
  while (1) {
    adv();
    while (getch() != 34) {
      if (getch() == eot) exit(1);
      if (slen == 32767) exit(6);
      if (getch() == 92) { adv(); c = escv(); }
      else { c = getch(); adv(); }
      sbuf[slen] = c;
      slen = slen + 1;
    }
    adv();
    skipwc();
    if (getch() != 34) break;
  }
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
  if (c == '~') { tok = o_tilde; return 0; }
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
  svtvhi = tvalhi; svtvll = tvalll; svtvfp = tvalfp;
  copyn(svname, tname);
  return 0;
}
/// @brief lsave で退避した状態へ戻す。
/// @return 常に 0
int lrest() {
  pos = svpos; tok = svtok; tval = svtval;
  tvalhi = svtvhi; tvalll = svtvll; tvalfp = svtvfp;
  copyn(tname, svname);
  return 0;
}

/// @brief トークンを 1 個読み進める。結果は tok / tval / tname に入る。
/// @return 常に 0
int next() {
  int c;
  int n;
  skipwc();
  c = getch();
  if (c == eot) { tok = t_eof; return 0; }
  if (isdig(c)) return lexnum();
  if (isidh(c)) {
    lexid();
    // __attribute__ (( ... )) は字句の段で丸ごと捨てる。宣言の前にも
    // 後にも付くので，構文の側で受けるより 1 箇所で消すほうが確実
    if (tok == t_id && streq(tname, "__attribute__")) {
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
    }
    return 0;
  }
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

/// @brief フレーム底 x8 + off のアドレスをレジスタへ作る。
/// @param rd 書込み先レジスタ番号 (作業用に上書きしてよいこと)
/// @param off フレームオフセット (非負)
/// @note addi の即値に収まれば 1 語。2048 以上の大きなフレームでは
///       liw + add の 3 語で作る (docs/stage014-external.md 第 8 部)。
///       フレーム上限は emitfn の検査が別に抑える。
int laddr(int rd, int off) {
  if (off < 2048) { outw(iw3(0x13, rd, 8, off)); return 0; }
  liw(rd, off);
  outw(0x33 | (rd << 7) | (rd << 15) | (8 << 20));
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
    if (streq(gname + i * 64, tname)) return i;
    i = i + 1;
  }
  return -1;
}
/// @brief tname で大域記号を新規登録する (名前のみ設定。属性は呼び手が埋める)。
/// @return 新しいエントリ添字。表が満杯なら終了コード 6 で停止する
int gnew() {
  int e;
  if (gcnt > 8191) exit(6);
  e = gcnt;
  gcnt = gcnt + 1;
  copyn(gname + e * 64, tname);
  gsz[e] = 0;
  gvar[e] = 0;
  gused[e] = 0;
  gllm[e] = 0;
  gdbm[e] = 0;
  gflm[e] = 0;
  return e;
}
/// @brief tname と一致するローカル記号 (引数・ローカル変数) を探す。
/// @return エントリ添字。見つからなければ -1
/// @note ローカルを先に引き，無ければ大域を引く。これが名前の遮蔽になる。
int lfind() {
  int i;
  // 内側の宣言 (後から登録された方) を先に見つける。これが遮蔽になる
  i = lcnt - 1;
  while (i >= 0) {
    if (streq(lname + i * 64, tname)) return i;
    i = i - 1;
  }
  return -1;
}
/// @brief tname でローカル記号を新規登録する。
/// @return 新しいエントリ添字。表が満杯なら終了コード 6 で停止する
int lnew() {
  int e;
  if (lcnt > 1023) exit(6);
  e = lcnt;
  lcnt = lcnt + 1;
  copyn(lname + e * 64, tname);
  lsg[e] = 0 - 1;
  return e;
}
/// @brief tname と一致する構造体を探す。
/// @return 構造体番号。見つからなければ -1
int sfind() {
  int i;
  i = 0;
  while (i < scnt) {
    if (streq(sname + i * 64, tname)) return i;
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
    if (streq(sname + i * 64, snam)) return i;
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
      if (streq(mname + i * 64, tname)) return i;
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
  if (rcnt > 262143) exit(6);
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
    if (streq(tdname + i * 64, tname)) return i;
  return -1;
}

/// @brief 列挙定数を tname で引く。
/// @return 表の添字。無ければ -1
int ecfind() {
  int i;
  for (i = 0; i < eccnt; i++)
    if (streq(ecname + i * 64, tname)) return i;
  return -1;
}

/// @brief 型指定子の始まりか。
/// @note 識別子が typedef 名かどうかで宣言か式かが決まる。C の構文が
///       文脈自由でない有名な箇所で，字句だけでは判断できない。
int istype() {
  if (tok == k_int || tok == k_char || tok == k_void) return 1;
  if (tok == k_unsigned || tok == k_signed || tok == k_short || tok == k_long) return 1;
  if (tok == k_float || tok == k_double) return 1;
  if (tok == k_struct || tok == k_union || tok == k_enum) return 1;
  if (tok == k_const || tok == k_volatile) return 1;
  if (tok == k_register || tok == k_auto || tok == k_static) return 1;
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
    if (eccnt > 8191) exit(6);
    i = eccnt;
    eccnt++;
    copyn(ecname + i * 64, tname);
    next();
    if (tok == o_asn) {
      // 右辺は整数定数式 (TOK_LAST = 256 - 1 など)。既に登録した列挙
      // 定数も引ける
      next();
      v = ccond();
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
  int lng;
  char sv[64];
  while (tok == k_const || tok == k_volatile) next();
  if (tok == k_void) { next(); k = t_void; }
  else if (tok == k_struct || tok == k_union) {
    u = 0;
    if (tok == k_union) u = 1;
    next();
    if (tok == t_id) { copyn(snam, tname); next(); }
    else anonnam();
    // snam は大域バッファであり，strudef の中でメンバの型が struct を
    // 持つと (無名 struct・struct タグ参照・前方参照の登録)，入れ子の
    // ptype が snam を上書きする。ここで手元に控え，strudef から戻って
    // から復元して引く。これを怠ると sfind2 が内側の struct を返し，
    // typedef 形式の定義が誤った番号を記録する
    // (docs/stage014-external.md 13.2 の opaqueptr)
    copyn(sv, snam);
    if (tok == o_lc) strudef(u);
    copyn(snam, sv);
    k = sfind2();
    if (k < 0) {
      // タグの前方参照 (typedef struct node Node; など)。不完全型として
      // 登録し，本体は後の struct node { ... } が埋める。埋まるまで
      // 使えるのはポインタだけである (docs/stage014-external.md 8.1)
      if (scnt > 2047) exit(6);
      k = scnt;
      scnt = scnt + 1;
      copyn(sname + k * 64, snam);
      ssize[k] = 0;
      sunion[k] = u;
    }
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
  } else if (tok == k_float) {
    next();
    k = t_float;
  } else if (tok == k_double) {
    next();
    k = t_double;
  } else if (tok == k_unsigned || tok == k_signed || tok == k_short
             || tok == k_long || tok == k_int || tok == k_char) {
    // 整数型の修飾子。順序は自由なので一通り読んでから決める
    uns = 0;
    sgn = 0;
    sht = 0;
    bas = -1;
    lng = 0;
    while (1) {
      if (tok == k_unsigned) { uns = 1; next(); }
      else if (tok == k_signed) { sgn = 1; next(); }
      else if (tok == k_short) { sht = 1; next(); }
      else if (tok == k_long) { lng = lng + 1; next(); }   // long は int と同じ幅
      else if (tok == k_double) {
        // long double は double と同じ 8 バイトにする (tcc の RV32 と揃える)
        next();
        return t_double;
      }
      else if (tok == k_register || tok == k_auto) { next(); }
      else if (tok == k_int) { bas = 1; next(); }
      else if (tok == k_char) { bas = 0; next(); }
      else break;
    }
    // long long は 64 bit の型。上位・下位の 2 語に分解して扱う
    // (docs/stage015-tcc.md 6 章)
    if (lng > 1) {
      if (uns) k = t_ullong;
      else k = t_llong;
    } else if (sht) {
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
  // * の後ろの const / volatile (char * const p など) は読み飛ばす。
  // 本処理系は修飾子を検査に使わないため，先頭と同じ扱いでよい
  while (tok == o_mul || tok == k_const || tok == k_volatile) {
    if (tok == o_mul) b = b + 65536;
    next();
  }
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
  if (is2w(t)) return 2;       // 下位語・上位語の 2 語 (double も同じ運び方)
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
  if (fncnt > 1023) exit(6);
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

/// @brief その配列の要素が文字型か (char / signed char)。
/// @note 文字列リテラルが**実体として並ぶのは文字型の配列だけ**である
///       (C89 3.5.7)。`char *a[1]` のようなポインタの配列に並べると，
///       ポインタの枠に字が入って参照先が不正になる。配列かどうかだけで
///       見分けてはいけない。
int ischararr(int t) {
  int e;
  if (!isarr(t)) return 0;
  e = aelem[t - t_arr];
  return e == 0 || e == t_schar;
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
  if (arrcnt > 4095) exit(6);
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
  return t == t_ushort || t == t_uint || t == t_ullong;
}

/// @brief 64 bit 整数の型か (long long / unsigned long long)。
/// @note ポインタは 32 bit なので，深さが 0 のときだけ見る。
int isll(int t) {
  if ((t >> 16) != 0) return 0;
  return t == t_llong || t == t_ullong;
}

/// @brief 浮動小数点の型か (float / double)。
int isfp(int t) {
  if ((t >> 16) != 0) return 0;
  return t == t_float || t == t_double;
}

/// @brief 記憶域が 2 語 (8 バイト) の型か。64 bit 整数と double。
/// @note 読み書き・引数・返却・フレーム確保は語数だけで決まるので，
///       この述語で扱いを揃える。**演算の分岐には使わない** (整数の
///       演算は isll，浮動小数点は isfp で分ける)。
int is2w(int t) {
  if ((t >> 16) != 0) return 0;
  return t == t_llong || t == t_ullong || t == t_double;
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
  if (t == t_llong || t == t_ullong) return 8;
  if (t == t_float) return 4;
  if (t == t_double) return 8;
  if (t == t_void) return 1;
  if (isarr(t)) return acnt[t - t_arr] * tsize(aelem[t - t_arr]);
  if (isfn(t)) return 4;
  return ssize[t - 2];
}

/// @brief 型の整列 (バイト)。
/// @note C89 は整列を規定しないが，**外の世界と混ぜるには合わせるしかない**。
///       ilp32 (RV32 / tcc / gcc) に合わせる: ポインタ・int は 4，
///       long long と double は 8，short は 2，char は 1。配列は要素，
///       構造体は自身の salign である (docs/stage015-tcc.md 14 章)。
int talign(int t) {
  if ((t >> 16) != 0) return 4;         // ポインタ
  if (isarr(t)) return talign(abase(t));
  if (isstru(t)) return salign[t - 2];
  if (isfn(t)) return 4;
  return tsize(t);                      // スカラは大きさ = 整列
}

/// @brief v を a の倍数へ切り上げる (a は 2 の冪)。
int roundup(int v, int a) { return (v + a - 1) / a * a; }

/// @brief 算術の結果の型 (整数の格上げと通常の算術変換)。
int arith2(int a, int b) {
  if (isuar(a) || isuar(b)) return t_uint;
  return 1;
}

/// @brief 64 bit が絡む算術の結果の型。
/// @note 片方でも符号なし 64 bit なら符号なしになる。狭いほうは伸ばされる。
int llar2(int a, int b) {
  if (a == t_ullong || b == t_ullong) return t_ullong;
  return t_llong;
}

/// @brief その型の演算を符号なしで行うか (64 bit を含む)。
int isull(int t) { return t == t_ullong; }

char fpnam[64];           ///< 関数ポインタの宣言子から取り出した名前
char fnname[64];          ///< 定義中の関数の名前 (__FUNCTION__ が返す)

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

// ---- 宣言子の定数式 ----
//
// 配列の大きさ [n] に書ける整数定数式。値をその場で畳んで返す。
// 現実の C は #define した式 (「(2 + (900000 / 50))」など) を大きさに
// 書くので，リテラル 1 個では外部ソースが通らない
// (docs/stage014-external.md 第 8 部)。
// C89 の整数定数式のうち sizeof とキャストを除く演算子と列挙定数を受ける。
// 会った例が無いため sizeof は保留し，必要になった世代で足す。

/// @brief 定数式の二項演算の優先順位。0 = 二項演算子ではない。
int cprec(int t) {
  if (t == o_oo) return 1;
  if (t == o_aa) return 2;
  if (t == o_or) return 3;
  if (t == o_xor) return 4;
  if (t == o_amp) return 5;
  if (t == o_eq || t == o_ne) return 6;
  if (t == o_lt || t == o_gt || t == o_le || t == o_ge) return 7;
  if (t == o_shl || t == o_shr) return 8;
  if (t == o_add || t == o_sub) return 9;
  if (t == o_mul || t == o_div || t == o_mod) return 10;
  return 0;
}

/// @brief 定数アドレス式のオフセットを畳む (offsetof の展開の中身)。
/// @return 先頭からのバイト数。畳んだ式の型は coty に残す
/// @note 受ける形は「括弧・(型 *) のキャスト・整数・-> と . の連鎖」だけ
///       である。一般のアドレス定数 (大域変数のアドレス) は扱わない。
int cofs() {
  int v; int t; int m;
  if (tok == o_lp) {
    next();
    if (istype()) {
      t = pstars(ptype());
      if (tok != o_rp) exit(1);
      next();
      v = cofs();
      coty = t;                  // キャストが型を決める
    } else {
      v = cofs();
      if (tok != o_rp) exit(1);
      next();
    }
  } else if (tok == t_num) {
    v = tval;
    coty = 1;
    next();
  } else exit(1);
  while (tok == o_arrow || tok == o_dot) {
    t = coty;
    if (tok == o_arrow) {
      if ((t >> 16) == 0) exit(5);
      t = t - 65536;
    }
    if (!isstru(t)) exit(5);
    next();
    if (tok != t_id) exit(1);
    m = mfind(t - 2);
    if (m < 0) exit(2);
    v = v + moff[m];
    coty = mty[m];
    next();
  }
  return v;
}

/// @brief 定数式: 単項。リテラル・列挙定数・括弧と前置の - + ! ~。
int cuna() {
  int v; int t;
  if (tok == o_sub) {
    // 64 bit の否定: 下位が 0 でなければ上位から桁借りが 1 つ入る
    next();
    v = 0 - cuna();
    if (ccll) {
      if (v != 0) cchi = (0 - cchi) - 1;
      else cchi = 0 - cchi;
    }
    return v;
  }
  if (tok == o_add) { next(); return cuna(); }
  if (tok == o_not) { next(); v = !cuna(); if (ccll) exit(5); return v; }
  if (tok == o_tilde) {
    next();
    v = cuna() ^ (0 - 1);
    if (ccll) cchi = cchi ^ (0 - 1);
    return v;
  }
  if (tok == k_sizeof) {
    // 定数式の sizeof (tcc の tab[(sizeof(long double)+3)/4] など)
    v = sizeofn();
    cchi = 0;
    ccll = 0;
    return v;
  }
  if (tok == o_amp) {
    // offsetof の展開 ((size_t)&(((T *)0)->m))。定数式の & で受ける形は
    // これだけである
    next();
    v = cofs();
    cchi = 0;
    ccll = 0;
    return v;
  }
  if (tok == t_num) {
    v = tval;
    cchi = tvalhi;
    ccll = tvalll;
    next();
    return v;
  }
  if (tok == t_id) {
    v = ecfind();
    if (v < 0) exit(2);
    next();
    ccll = 0;
    return ecval[v];
  }
  if (tok == o_lp) {
    next();
    if (istype()) {
      // 定数式のキャスト。幅の狭い整数型だけ値を刻み，あとは素通し
      t = pstars(ptype());
      if (tok != o_rp) exit(1);
      next();
      v = cuna();
      if (ccll) { ccll = 0; cchi = 0; }
      if ((t >> 16) == 0) {
        if (t == 0) v = v & 255;
        else if (t == t_schar) { v = v & 255; if (v & 128) v = v - 256; }
        else if (t == t_ushort) v = v & 65535;
        else if (t == t_short) { v = v & 65535; if (v & 32768) v = v - 65536; }
      }
      return v;
    }
    v = ccond();
    if (tok != o_rp) exit(1);
    next();
    return v;
  }
  exit(1);
  return 0;
}

/// @brief 定数式: 二項演算。優先順位のぼり方式で 1 関数に収める。
/// @param minp この呼出しで結合してよい最小の優先順位
int cbin(int minp) {
  int v; int w; int t; int p; int lft;
  v = cuna();
  while (cprec(tok) >= minp && cprec(tok) > 0) {
    t = tok;
    p = cprec(t);
    next();
    lft = ccll;
    w = cbin(p + 1);
    // 64 bit リテラルを含む二項の定数式はまだ畳めない。黙って 32 bit で
    // 畳むと値が壊れるので拒む (終了コード 5)
    if (lft || ccll) exit(5);
    if (t == o_oo) { if (v || w) v = 1; else v = 0; }
    if (t == o_aa) { if (v && w) v = 1; else v = 0; }
    if (t == o_or) v = v | w;
    if (t == o_xor) v = v ^ w;
    if (t == o_amp) v = v & w;
    if (t == o_eq) v = v == w;
    if (t == o_ne) v = v != w;
    if (t == o_lt) v = v < w;
    if (t == o_gt) v = v > w;
    if (t == o_le) v = v <= w;
    if (t == o_ge) v = v >= w;
    if (t == o_shl) v = v << w;
    if (t == o_shr) v = v >> w;
    if (t == o_add) v = v + w;
    if (t == o_sub) v = v - w;
    if (t == o_mul) v = v * w;
    if (t == o_div) { if (w == 0) exit(1); v = v / w; }
    if (t == o_mod) { if (w == 0) exit(1); v = v % w; }
  }
  return v;
}

/// @brief 定数式: 条件 (?:) まで。宣言子の大きさを読む入口。
int ccond() {
  int v; int a; int b;
  v = cbin(1);
  if (tok == o_que) {
    next();
    a = ccond();
    if (ccll) exit(5);      // ?: の枝の 64 bit リテラルは選ばれなかった側の
    if (tok != o_col) exit(1);
    next();
    b = ccond();
    if (ccll) exit(5);      // 上位語が残ってしまうので，どちらの枝でも拒む
    if (v) return a;
    return b;
  }
  return v;
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
  if (tok != o_rb) n = ccond();
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
  if (icnt > 131071) exit(6);
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
  if (labcnt > 16383) exit(6);
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
/// @brief 左辺値を値へ変える (64 bit を許す版)。
/// @note 64 bit を扱えると判っている経路 (代入の右辺・初期化子) だけが
///       これを呼ぶ。それ以外は rv() を使い，64 bit は拒まれる。
///       **「教えていない経路では黙って壊れるより拒む」** ための区別で
///       ある (docs/stage015-tcc.md 6.1)。
int rvany(int v) {
  int msk;
  if (elv) {
    // 構造体の値は 1 語に載らない。実体のアドレスをそのまま値とする
    // (@section struval)。型は構造体のままなので，メンバ参照は変わらない
    if (isstru(ety)) { elv = 0; return v; }
    if (ebfw) {
      // ビットフィールドの取り出し: 語を読み，右へ寄せ，幅でマスクする。
      // マスクが上位を落とすので，右シフトの種類 (算術/論理) には
      // 依存しない
      msk = (1 << ebfw) - 1;
      v = ldval(v, 1);
      if (ebfo) v = emit(c_bin + b_srl, v, emit(c_const, ebfo, 0));
      v = emit(c_bin + b_and, v, emit(c_const, msk, 0));
      ebfw = 0;
      elv = 0;
      return v;
    }
    if (is2w(ety)) {
      // 2 語の型 (64 bit 整数と double)。上位語は アドレス + 4 にある
      ehi = emit(c_loadw, emit(c_bin + b_add, v, emit(c_const, 4, 0)), 0);
      v = emit(c_loadw, v, 0);
      elv = 0;
      return v;
    }
    v = ldval(v, ety);
    elv = 0;
    ehi = 0;
  }
  return v;
}

/// @brief 左辺値を値へ変える (32 bit 専用)。
/// @note 64 bit の値が来たら拒む (終了コード 5)。第 2 部の途中の世代では
///       演算がまだ無いので，通してしまうと下位語だけで計算して黙って
///       値を壊す。実装した経路が増えるたびに rvany へ移していく。
int rv(int v) {
  v = rvany(v);
  if (ehi) exit(5);
  return v;
}

/// @brief 左辺値を「条件の値」へ変える。
/// @note 64 bit は上下の語を or して 1 語にする (0 かどうかしか見ないので
///       これで足りる)。rv と違い 64 bit を拒まない。
int rvc(int v) {
  v = rvany(v);
  if (isfp(ety)) {
    // d != 0.0 と同じ。__dcmp は -0.0 と +0.0 を等しいと言い，NaN (2) は
    // 0 でないので真になる (C の規則どおり)
    v = fplift(v, ehi, ety);
    v = rtc41("__dcmp", 1, v, ehi, emit(c_const, 0, 0), emit(c_const, 0, 0));
    v = emit(c_bin + b_sne, v, emit(c_const, 0, 0));
    ehi = 0;
    ety = 1;
    return v;
  }
  if (ehi) {
    v = emit(c_bin + b_or, v, ehi);
    ehi = 0;
    ety = 1;
  }
  return v;
}

/// @brief 文字列リテラルを文字列プールへ積み，その先頭アドレスを表す値を作る。
/// @return 値番号 (型は char *)
/// @note 実体は関数本体の後ろにまとめて出力する。アドレスはその時点まで
///       決まらないので，ここでは GSTR にプール内オフセットだけ持たせ，
///       emitfn の末尾で実アドレスへ書き換える。
int estr2() {
  int p; int a; int i; int n;
  // **文字列リテラルは char[n+1] である** (C89 6.1.4)。式の中では
  // 先頭要素へのポインタに退化するが，sizeof と単項 & は退化前を見る。
  // 長さは next() が sbuf / slen を上書きする前に控える
  n = slen;
  p = (slen + 4) & 0xfffffffc;
  if (spcnt + p > 65535) exit(6);
  a = spcnt;
  i = 0;
  while (i < p) { spool[spcnt] = sbuf[i]; spcnt = spcnt + 1; i = i + 1; }
  ety = 65536;
  elv = 0;
  earr = 1;
  eaty = atype(0, n + 1);
  esz = n + 1;
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
  if (marr[m]) { ety = adecay(mty[m]); elv = 0; earr = 1; eaty = mty[m]; }
  else { ety = mty[m]; elv = 1; earr = 0; }
  // ビットフィールドなら，語のアドレスに幅と位置を添えて返す。
  // 取り出しは rv が，書込みは assign が行う
  if (mbfw[m]) { ebfw = mbfw[m]; ebfo = mbfo[m]; }
  else ebfw = 0;
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
int pusharg(int v, int w, int hi) {
  int k; int a;
  if (w == 1) { emit(c_arg, v, 0); return 0; }
  if (hi) {
    // 64 bit。下位語・上位語の順に積むと，呼ばれた側のフレーム上で
    // 記憶域と同じ並び (下位語が低い方) になる
    emit(c_arg, v, 0);
    emit(c_arg, hi, 0);
    return 0;
  }
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
  int np; int n; int i; int k; int off;
  int av[32];
  int aw[32];
  int ah[32];                  // 64 bit の実引数の上位語 (0 = 32 bit)
  int at[32];                  // 実引数の型 (格上げの符号の判定に使う)
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
      av[np] = rvany(assign());
      ah[np] = ehi;
      aw[np] = nwords(ety);
      at[np] = ety;
      n = n + aw[np];
      np = np + 1;
      if (tok != o_comma) break;
      next();
    }
  }
  if (tok != o_rp) exit(1);
  next();
  k = gna[e];
  // 実引数を宣言された仮引数の幅へ揃える (C の通常の変換)。
  //   32 bit -> 64 bit の仮引数: 符号に応じて上位語を作る (格上げ)
  //   64 bit -> 32 bit の仮引数: 下位語だけ渡す (切詰め)
  // 宣言が判らない (k < 0，関数ポインタ) ときは何もしない。可変部
  // (offset >= k) も C の既定の格上げのままでよいので触らない
  if (k >= 0) {
    ehi = 0;
    off = 0;
    i = 0;
    while (i < np) {
      if (off < k && off < 32) {
        if ((gllm[e] >> off) & 1) {
          // 64 bit 整数の仮引数
          if (isfp(at[i])) {
            // 浮動小数点 -> 64 bit 整数 (切捨て)
            if (aw[i] == 1) n = n + 1;
            av[i] = fplift(av[i], ah[i], at[i]);
            av[i] = rtc22("__d2ll", t_llong, av[i], ehi);
            ah[i] = ehi;
            aw[i] = 2;
          } else if (aw[i] == 1 && !isstru(at[i])) {
            ah[i] = ext32(av[i], at[i]);
            aw[i] = 2;
            n = n + 1;
          }
        } else if ((gdbm[e] >> off) & 1) {
          // double の仮引数。型が違えば持ち上げる
          if (at[i] != t_double) {
            if (isstru(at[i])) exit(5);
            if (aw[i] == 1) n = n + 1;
            av[i] = fplift(av[i], ah[i], at[i]);
            ah[i] = ehi;
            aw[i] = 2;
          }
        } else if ((gflm[e] >> off) & 1) {
          // float の仮引数。double を経由して丸める
          if (at[i] != t_float) {
            if (isstru(at[i])) exit(5);
            if (aw[i] == 2) n = n - 1;
            av[i] = fplift(av[i], ah[i], at[i]);
            av[i] = rtc21("__d2f", t_float, av[i], ehi);
            ah[i] = 0;
            aw[i] = 1;
          }
        } else if (aw[i] == 2 && isll(at[i])) {
          ah[i] = 0;
          aw[i] = 1;
          n = n - 1;
        } else if (isfp(at[i])) {
          // 浮動小数点 -> 32 bit 整数の仮引数 (切捨て)
          if (aw[i] == 2) n = n - 1;
          av[i] = fplift(av[i], ah[i], at[i]);
          av[i] = rtc21("__d2i", 1, av[i], ehi);
          ah[i] = 0;
          aw[i] = 1;
        }
      }
      off = off + aw[i];
      i = i + 1;
    }
  }
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
      // 可変部の 2 語の値 (long long / double)。下位語が低い番地に
      // 来るように**上位語を先に**積む (積んだ順の逆がメモリの昇順)。
      // va_arg 側は語数ぶん進めて読む (stage015 の stdarg.h)。
      // float は C の既定の実引数拡張で double に格上げして積む。
      // 構造体は従来どおり名前つきの側にしか置けない
      if (isstru(at[i])) exit(5);
      if (at[i] == t_float) {
        av[i] = fplift(av[i], 0, t_float);
        ah[i] = ehi;
        aw[i] = 2;
        n = n + 1;
      }
      if (aw[i] == 2) emit(c_arg, ah[i], 0);
      else if (aw[i] != 1) exit(5);
      emit(c_arg, av[i], 0);
    }
    i = 0;
    while (argofs(aw, i) < k) { pusharg(av[i], aw[i], ah[i]); i = i + 1; }
  } else {
    if (k >= 0 && k != n) exit(5);
    if (k < 0) gused[e] = 1;
    i = 0;
    while (i < np) { pusharg(av[i], aw[i], ah[i]); i = i + 1; }
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
  if (is2w(gty[e])) {
    // 2 語の返却 (64 bit 整数と double)。下位語は呼出しの値そのもの，上位語はその 1 つ上に
    // 積まれて来る。構造体と同じ仕組みでフレームの一時領域へ引き取り，
    // そこから読み直して ehi にする (docs/stage015-tcc.md 6.2 の項目 11)
    iret[i] = frame1(4);
    ehi = emit(c_loadw, emit(c_laddr, iret[i], 0), 0);
    ety = gty[e];
    elv = 0;
    earr = 0;
    return i;
  }
  ety = gty[e];
  elv = 0;
  earr = 0;
  ehi = 0;
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
  ebfw = 0;
  e = lfind();
  if (e >= 0) {
    if (lsg[e] >= 0) {
      // 関数内 static。実体は .bss にあり，大域と同じ形で参照する
      e = lsg[e];
      if (garr[e]) { ety = adecay(gty[e]); elv = 0; earr = 1; eaty = gty[e]; }
      else { ety = gty[e]; elv = 1; earr = 0; }
      esz = gsz[e];
      next();
      return emit(c_gaddr, e, 0);
    }
    if (larr[e]) { ety = adecay(lty[e]); elv = 0; earr = 1; eaty = lty[e]; }
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
    if (garr[e]) { ety = adecay(gty[e]); elv = 0; earr = 1; eaty = gty[e]; }
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
  // ehi は大域なので，前の式の値が残っていると次の式が 64 bit に見える。
  // 式は必ず一次式から始まるので，ここで消せば取り残しが無い
  ehi = 0;
  if (tok == t_num) {
    if (tvalfp == 1) {
      // double リテラル。bit の並びを 2 語の定数で運ぶ
      v = emit(c_const, tval, 0);
      ehi = emit(c_const, tvalhi, 0);
      elv = 0; ety = t_double; earr = 0;
      next();
      return v;
    }
    if (tvalfp == 2) {
      // float リテラル。binary32 の bit の並び 1 語
      v = emit(c_const, tval, 0);
      elv = 0; ety = t_float; ehi = 0; earr = 0;
      next();
      return v;
    }
    v = emit(c_const, tval, 0);
    elv = 0; ety = 1; ehi = 0;
    if (tvalu) ety = t_uint;
    if (tvalll) {
      // 64 bit のリテラル。上位語を別の値として持つ (docs/stage015-tcc.md 6.1)
      ehi = emit(c_const, tvalhi, 0);
      if (tvalu) ety = t_ullong;
      else ety = t_llong;
    }
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
  if (tok == t_id) {
    if (streq(tname, "__FUNCTION__")) {
      // 現在の関数名の文字列 (gcc の書き方。tcc のエラー報告が使う)
      slen = 0;
      while (fnname[slen]) { sbuf[slen] = fnname[slen]; slen = slen + 1; }
      sbuf[slen] = 0;
      sbuf[slen + 1] = 0;
      sbuf[slen + 2] = 0;
      sbuf[slen + 3] = 0;
      return estr2();
    }
    return eident();
  }
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
      a = rvany(assign());
      w = nwords(ety);
      pusharg(a, w, ehi);
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
        eaty = ety;
        ety = adecay(ety);
        elv = 0;
        earr = 1;
      } else {
        elv = 1;
        earr = 0;
        ebfw = 0;
      }
    } else if (tok == o_dot) {
      // 構造体の値はどれも実体のアドレスなので，左辺値でなくても
      // メンバは取れる (mk().x のような呼出しの返却値)。ただしそれは
      // 一時領域なので，書き込みは erv を検査して拒否する
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

/// @brief 32 bit の値を 64 bit へ広げたときの上位語を作る。
/// @param v 下位語になる値番号
/// @param t v の型
/// @return 上位語の値番号
/// @note 符号ありなら符号ビットを 31 回の算術右シフトで伸ばし，
///       符号なしなら 0 を置く (docs/stage015-tcc.md 6.2 の項目 10)。
int ext32(int v, int t) {
  if (isuty(t) || (t >> 16) != 0) return emit(c_const, 0, 0);
  return emit(c_bin + b_sra, v, emit(c_const, 31, 0));
}

// ---- 64 bit の演算 (docs/stage015-tcc.md 6.2 の項目 6・7) ----
//
// 値は「下位語 + 上位語」の 2 つに分けて持つ (ehi)。ここの関数はどれも
// 下位語を返し，上位語を ehi へ置く。命令種別は 1 つも足していない。

/// @brief 左辺・右辺を 64 bit に揃える。狭いほうを符号 (0) で伸ばす。
/// @param a 左辺の下位語 / @param ah 左辺の上位語 (0 = 32 bit)
/// @param at 左辺の型
/// @return 揃えた左辺の上位語
int wide1(int a, int ah, int at) {
  if (ah) return ah;
  return ext32(a, at);
}

/// @brief 64 bit の加算・減算。
/// @param op 0 = 加算, 1 = 減算
/// @return 下位語 (上位語は ehi へ置く)
/// @note 桁上げ (借り) は符号なし比較で作る。加算では「和が左辺より
///       小さければ桁上げ」，減算では「左辺が右辺より小さければ借り」。
int ll_addsub(int al, int ah, int bl, int bh, int op) {
  int lo; int c;
  if (op) {
    lo = emit(c_bin + b_sub, al, bl);
    c = emit(c_bin + b_ult, al, bl);
    ehi = emit(c_bin + b_sub, emit(c_bin + b_sub, ah, bh), c);
  } else {
    lo = emit(c_bin + b_add, al, bl);
    c = emit(c_bin + b_ult, lo, al);
    ehi = emit(c_bin + b_add, emit(c_bin + b_add, ah, bh), c);
  }
  return lo;
}

/// @brief 64 bit のビット演算 (語ごとに独立なので上下を別々に行う)。
int ll_bit(int al, int ah, int bl, int bh, int bop) {
  ehi = emit(c_bin + bop, ah, bh);
  return emit(c_bin + bop, al, bl);
}

/// @brief 64 bit の等値比較。上下を xor して or を取り，0 かどうかを見る。
/// @param ne 0 = ==, 1 = !=
/// @return 1 / 0 の 32 bit 値
int ll_eq(int al, int ah, int bl, int bh, int ne) {
  int d;
  d = emit(c_bin + b_or, emit(c_bin + b_xor, al, bl),
                         emit(c_bin + b_xor, ah, bh));
  ehi = 0;
  if (ne) return emit(c_bin + b_sne, d, emit(c_const, 0, 0));
  return emit(c_bin + b_seq, d, emit(c_const, 0, 0));
}

/// @brief 64 bit の大小比較 (a < b)。
/// @param uns 1 = 符号なし
/// @return 1 / 0 の 32 bit 値
/// @note 上位語が違えばそれで決まり，同じなら下位語を符号なしで比べる。
///       「上位が小さい」または「上位が等しくかつ下位が小さい」を
///       ビット演算で組む (分岐を作らずに済む)。
int ll_lt(int al, int ah, int bl, int bh, int uns) {
  int hlt; int heq; int llt;
  if (uns) hlt = emit(c_bin + b_ult, ah, bh);
  else hlt = emit(c_bin + b_slt, ah, bh);
  heq = emit(c_bin + b_seq, ah, bh);
  llt = emit(c_bin + b_ult, al, bl);
  ehi = 0;
  return emit(c_bin + b_or, hlt, emit(c_bin + b_and, heq, llt));
}

/// @brief 名前で大域記号を引く。
/// @note 記号表の鍵は tname なので，一時的に書き換えて引き，元へ戻す。
///       戻さないと，このあと解析する識別子の名前が失われる。
int gfindnm(char *nm) {
  char sv[64];
  int i; int e;
  copyn(sv, tname);
  i = 0;
  while (nm[i]) { tname[i] = nm[i]; i = i + 1; }
  while (i < 32) { tname[i] = 0; i = i + 1; }
  e = gfind();
  copyn(tname, sv);
  return e;
}

/// @brief 64 bit の除算・剰余を実行時支援の呼出しへ落とす。
/// @param nm 呼ぶ関数の名前
/// @return 下位語 (上位語は ehi へ置く)
/// @note 引数は下位語・上位語の順に 2 つぶん積む (呼出し規約は 64 bit の
///       実引数と同じ)。返却も 64 bit の返却と同じ形で受け取る。
/// @brief 実行時支援の呼出し: 引数 1 語，返却 2 語 (__i2d など)。
int rtc12(char *nm, int ty, int a) {
  int e; int i;
  e = gfindnm(nm);
  if (e < 0) e = biadd2(nm, 1, ty);
  emit(c_arg, a, 0);
  i = emit(c_call, e, 1);
  iret[i] = frame1(4);
  ehi = emit(c_loadw, emit(c_laddr, iret[i], 0), 0);
  return i;
}

/// @brief 実行時支援の呼出し: 引数 2 語，返却 1 語 (__d2f など)。
int rtc21(char *nm, int ty, int al, int ah) {
  int e; int i;
  e = gfindnm(nm);
  if (e < 0) e = biadd2(nm, 2, ty);
  emit(c_arg, al, 0);
  emit(c_arg, ah, 0);
  i = emit(c_call, e, 2);
  ehi = 0;
  return i;
}

/// @brief 実行時支援の呼出し: 引数 2 語，返却 2 語 (__ll2d など)。
int rtc22(char *nm, int ty, int al, int ah) {
  int e; int i;
  e = gfindnm(nm);
  if (e < 0) e = biadd2(nm, 2, ty);
  emit(c_arg, al, 0);
  emit(c_arg, ah, 0);
  i = emit(c_call, e, 2);
  iret[i] = frame1(4);
  ehi = emit(c_loadw, emit(c_laddr, iret[i], 0), 0);
  return i;
}

/// @brief いまの値 (v, ehi, ety) を型 t へ変換する (浮動小数点が絡むもの)。
/// @return 変換後の値番号。ehi と ety も更新する
/// @note 整数どうしの変換は呼ぶ側の既存の規則 (ext32 / narrow) が行う。
///       ここへ来るのは t か ety の少なくとも一方が浮動小数点のときだけ。
///       実行時支援 (rtfp.c) の呼出しに落とす。float の変換は double を
///       経由する (docs/stage015-tcc.md 10.3 と同じ理由で二重丸めは
///       加減乗除では起きないが，int -> float は起きうる。既知の妥協)
int cvtto(int v, int t) {
  int ft;
  ft = ety;
  if ((t >> 16) != 0 || (ft >> 16) != 0) exit(5);   // ポインタとの変換は無い
  if (isstru(t) || isstru(ft)) exit(5);
  if (t == ft) return v;
  if (t == t_double) {
    if (ft == t_float) v = rtc12("__f2d", t, v);
    else if (isll(ft)) {
      if (isull(ft)) v = rtc22("__ull2d", t, v, ehi);
      else v = rtc22("__ll2d", t, v, ehi);
    }
    else if (isuty(ft)) v = rtc12("__u2d", t, v);
    else v = rtc12("__i2d", t, v);
    ety = t; elv = 0; earr = 0;
    return v;
  }
  if (t == t_float) {
    if (ft != t_double) v = cvtto(v, t_double);
    v = rtc21("__d2f", t, v, ehi);
    ety = t; elv = 0; earr = 0;
    return v;
  }
  // 浮動小数点から整数へ (0 方向への切捨て)
  if (ft == t_float) { v = rtc12("__f2d", t_double, v); ft = t_double; }
  if (isll(t)) {
    if (isull(t)) v = rtc22("__d2ull", t, v, ehi);
    else v = rtc22("__d2ll", t, v, ehi);
  } else {
    if (isuty(t)) v = rtc21("__d2u", t, v, ehi);
    else v = rtc21("__d2i", t, v, ehi);
    v = narrow(v, t);
  }
  ety = t; elv = 0; earr = 0;
  return v;
}

/// @brief 実行時支援の呼出し: 引数 4 語，返却 1 語 (__dcmp)。
int rtc41(char *nm, int ty, int al, int ah, int bl, int bh) {
  int e; int i;
  e = gfindnm(nm);
  if (e < 0) e = biadd2(nm, 4, ty);
  emit(c_arg, al, 0);
  emit(c_arg, ah, 0);
  emit(c_arg, bl, 0);
  emit(c_arg, bh, 0);
  i = emit(c_call, e, 4);
  ehi = 0;
  return i;
}

/// @brief 値 (v, h, t) を double へ持ち上げ，下位語を返す (上位語は ehi)。
/// @note 二項演算の左辺は右辺の解析で ehi が上書きされるため，控えた
///       上位語 h を明示的に受け取る。cvtto と違い ety を見ない。
int fplift(int v, int h, int t) {
  if (t == t_double) { ehi = h; return v; }
  if (t == t_float) return rtc12("__f2d", t_double, v);
  if (isll(t)) {
    if (isull(t)) return rtc22("__ull2d", t_double, v, h);
    return rtc22("__ll2d", t_double, v, h);
  }
  if ((t >> 16) != 0) exit(5);      // ポインタと浮動小数点の演算は無い
  if (isuty(t)) return rtc12("__u2d", t_double, v);
  return rtc12("__i2d", t_double, v);
}

/// @brief 浮動小数点の二項算術の結果の型 (C の通常の算術変換)。
/// @note どちらかが double なら double。そうでなければ float (ここへ
///       来るのは少なくとも片方が浮動小数点のときだけなので，残りは
///       float と整数の組合せであり，結果は float)。
///       計算は常に double で行う。float の結果は最後に 1 回だけ丸め
///       直すが，加減乗除では二重丸めが結果を変えないことが知られている
///       (docs/stage015-tcc.md 10.3 と同じ性質)
int fpar2(int a, int b) {
  if (a == t_double || b == t_double) return t_double;
  return t_float;
}

/// @brief __dcmp の返り値 (-1/0/1/2。2 = NaN) を比較演算子の真偽に直す。
/// @param c __dcmp の値番号
/// @param op o_lt などのトークン
/// @note NaN との比較は == と < の類がすべて偽，!= だけが真。
///       -1/0/1/2 との突き合わせで自然にそうなる。
int fpcmpres(int c, int op) {
  int a; int b;
  if (op == o_eq) return emit(c_bin + b_seq, c, emit(c_const, 0, 0));
  if (op == o_ne) return emit(c_bin + b_sne, c, emit(c_const, 0, 0));
  if (op == o_lt) return emit(c_bin + b_seq, c, emit(c_const, 0 - 1, 0));
  if (op == o_gt) return emit(c_bin + b_seq, c, emit(c_const, 1, 0));
  if (op == o_le) {
    a = emit(c_bin + b_seq, c, emit(c_const, 0 - 1, 0));
    b = emit(c_bin + b_seq, c, emit(c_const, 0, 0));
    return emit(c_bin + b_or, a, b);
  }
  a = emit(c_bin + b_seq, c, emit(c_const, 1, 0));
  b = emit(c_bin + b_seq, c, emit(c_const, 0, 0));
  return emit(c_bin + b_or, a, b);
}

int ll_call2(char *nm, int ty, int al, int ah, int bl, int bh) {
  int e; int i;
  e = gfindnm(nm);
  if (e < 0) e = biadd2(nm, 4, ty);
  emit(c_arg, al, 0);
  emit(c_arg, ah, 0);
  emit(c_arg, bl, 0);
  emit(c_arg, bh, 0);
  i = emit(c_call, e, 4);
  iret[i] = frame1(4);
  ehi = emit(c_loadw, emit(c_laddr, iret[i], 0), 0);
  return i;
}

/// @brief 64 bit の乗算。
/// @note 下位語どうしの積が 64 bit を作り，そこへ交差項を上位語として足す。
///       a.hi * b.hi は 2^64 を超えるので捨てる。
int ll_mul(int al, int ah, int bl, int bh) {
  int lo; int hi;
  lo = emit(c_bin + b_mul, al, bl);
  hi = emit(c_bin + b_mulhu, al, bl);
  hi = emit(c_bin + b_add, hi, emit(c_bin + b_mul, al, bh));
  hi = emit(c_bin + b_add, hi, emit(c_bin + b_mul, ah, bl));
  ehi = hi;
  return lo;
}

/// @brief 64 bit の左シフト (桁数は 32 bit の値)。
/// @note 桁数が 32 以上かどうかで結果が変わる。分岐を作らず，
///       「32 以上なら 1 になる印」を作って両方を計算し，選ぶ。
///       選択は印から 0 / 全 1 のマスクを作って and / or で行う。
int ll_shl(int al, int ah, int c) {
  int big; int m; int n; int r32; int hi1; int hi2; int lo1;
  c = emit(c_bin + b_and, c, emit(c_const, 63, 0));
  n = emit(c_bin + b_and, c, emit(c_const, 31, 0));
  big = emit(c_bin + b_ugt, c, emit(c_const, 31, 0));      // c >= 32
  m = emit(c_neg, big, 0);                                 // 0 or 全 1
  // c < 32 の場合: hi = (ah << n) | (al >>u (32 - n)) ただし n = 0 なら後半は 0
  r32 = emit(c_bin + b_sub, emit(c_const, 32, 0), n);
  hi1 = emit(c_bin + b_sll, ah, n);
  lo1 = emit(c_bin + b_srl, al, emit(c_bin + b_and, r32, emit(c_const, 31, 0)));
  // n == 0 のとき (32 - n) は 32 でシフト量として不正なので，印で消す
  lo1 = emit(c_bin + b_and, lo1,
             emit(c_neg, emit(c_bin + b_sne, n, emit(c_const, 0, 0)), 0));
  hi1 = emit(c_bin + b_or, hi1, lo1);
  // c >= 32 の場合: hi = al << n, lo = 0
  hi2 = emit(c_bin + b_sll, al, n);
  ehi = emit(c_bin + b_or, emit(c_bin + b_and, hi2, m),
                           emit(c_bin + b_and, hi1, emit(c_bin + b_xor, m, emit(c_const, 0 - 1, 0))));
  return emit(c_bin + b_and, emit(c_bin + b_sll, al, n), emit(c_bin + b_xor, m, emit(c_const, 0 - 1, 0)));
}

/// @brief 64 bit の右シフト。
/// @param ar 1 = 算術 (符号あり)
int ll_shr(int al, int ah, int c, int ar) {
  int big; int m; int n; int r32; int lo1; int hi1; int lo2; int hi2; int sgn;
  c = emit(c_bin + b_and, c, emit(c_const, 63, 0));
  n = emit(c_bin + b_and, c, emit(c_const, 31, 0));
  big = emit(c_bin + b_ugt, c, emit(c_const, 31, 0));
  m = emit(c_neg, big, 0);
  r32 = emit(c_bin + b_sub, emit(c_const, 32, 0), n);
  // c < 32: lo = (al >>u n) | (ah << (32 - n)), hi = ah >> n
  lo1 = emit(c_bin + b_srl, al, n);
  hi1 = emit(c_bin + b_sll, ah, emit(c_bin + b_and, r32, emit(c_const, 31, 0)));
  hi1 = emit(c_bin + b_and, hi1,
             emit(c_neg, emit(c_bin + b_sne, n, emit(c_const, 0, 0)), 0));
  lo1 = emit(c_bin + b_or, lo1, hi1);
  if (ar) hi1 = emit(c_bin + b_sra, ah, n);
  else hi1 = emit(c_bin + b_srl, ah, n);
  // c >= 32: lo = ah >> n, hi = 符号 (算術なら ah >> 31，論理なら 0)
  if (ar) {
    lo2 = emit(c_bin + b_sra, ah, n);
    sgn = emit(c_bin + b_sra, ah, emit(c_const, 31, 0));
  } else {
    lo2 = emit(c_bin + b_srl, ah, n);
    sgn = emit(c_const, 0, 0);
  }
  hi2 = sgn;
  ehi = emit(c_bin + b_or, emit(c_bin + b_and, hi2, m),
                           emit(c_bin + b_and, hi1, emit(c_bin + b_xor, m, emit(c_const, 0 - 1, 0))));
  return emit(c_bin + b_or, emit(c_bin + b_and, lo2, m),
                            emit(c_bin + b_and, lo1, emit(c_bin + b_xor, m, emit(c_const, 0 - 1, 0))));
}

/// @brief sizeof を解析する (「sizeof (型)」と「sizeof 単項式」)。
/// @return 大きさを表す定数の値番号
/// @note C は式形式の被演算子を評価しない。IR は配列と個数で持っているので，
///       解析前に個数を保存しておき，型を得た後に巻き戻すことで「出さない」を
///       実現する。配列に対しては配列全体の大きさを返す (earr / esz)。
/// @brief sizeof の値 (バイト数) を求める。定数式からも使う。
/// @note 式の形は euna で型だけ得て IR を巻き戻す (値は要らない)。
int sizeofn() {
  int t; int n; int si; int sl; int sh; int ss;
  next();
  if (tok == o_lp) {
    lsave();
    next();
    if (istype()) {
      t = pstars(ptype());
      if (tok != o_rp) exit(1);
      next();
      return tsize(t);
    }
    lrest();
  }
  si = icnt; sl = labcnt; sh = cloff; ss = spcnt;
  euna();
  n = tsize(ety);
  if (earr) n = esz;
  icnt = si; labcnt = sl; cloff = sh; spcnt = ss;
  return n;
}

int esizeof() {
  int n;
  n = sizeofn();
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
      v = rvany(euna());
      if (isfp(t) || isfp(ety)) {
        // 浮動小数点が絡む変換は実行時支援へ落とす
        v = cvtto(v, t);
        ety = t; elv = 0; earr = 0;
        return v;
      }
      if (isll(t)) {
        // 狭い型から 64 bit へ: 符号 (0) で伸ばす
        if (!isll(ety)) ehi = ext32(v, ety);
        ety = t; elv = 0; earr = 0;
        return v;
      }
      // 64 bit から狭い型へ: 下位語だけを残す
      if (isll(ety)) ehi = 0;
      // 幅の狭い型へのキャストは値を切り詰める
      v = narrow(v, t);
      ety = t; elv = 0; earr = 0;
      return v;
    }
    lrest();
  }
  if (tok == o_sub) {
    next();
    v = rvany(euna());
    if (isfp(ety)) {
      // IEEE 754 の否定は符号 bit の反転そのもの
      t = ety;
      if (ety == t_double)
        ehi = emit(c_bin + b_xor, ehi, emit(c_const, 0x80000000, 0));
      else
        v = emit(c_bin + b_xor, v, emit(c_const, 0x80000000, 0));
      ety = t; earr = 0; elv = 0;
      return v;
    }
    if (isll(ety)) {
      // 0 - x として 2 語で引く
      t = ety;
      v = ll_addsub(emit(c_const, 0, 0), emit(c_const, 0, 0), v, ehi, 1);
      ety = t; earr = 0;
      return v;
    }
    ety = 1; earr = 0;
    return emit(c_neg, v, 0);
  }
  if (tok == o_not) {
    next();
    v = rvc(euna());
    ety = 1; earr = 0;
    return emit(c_not, v, 0);
  }
  if (tok == o_tilde) {
    // ~x は x ^ -1 と同値。命令種別を足さずに済ませる
    next();
    v = rvany(euna());
    if (isfp(ety)) exit(5);
    if (isll(ety)) {
      t = ety;
      v = ll_bit(v, ehi, emit(c_const, 0 - 1, 0), emit(c_const, 0 - 1, 0), b_xor);
      ety = t; earr = 0;
      return v;
    }
    ety = 1; earr = 0;
    return emit(c_bin + b_xor, v, emit(c_const, 0 - 1, 0));
  }
  if (tok == o_mul) {
    next();
    v = rv(euna());
    if ((ety >> 16) == 0) exit(5);
    ety = ety - 65536;
    if (isarr(ety)) {
      esz = tsize(ety);
      eaty = ety;
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
      ebfw = 0;
    }
    return v;
  }
  if (tok == o_amp) {
    next();
    v = euna();
    if (elv == 0) {
      // 関数のアドレスは関数そのものと同じ値である (&f と f は等価)
      if ((ety >> 16) == 1 && isfn(ety & 65535)) return v;
      // 配列のアドレスは先頭要素のアドレスと同じ値で，型だけが
      // 「配列へのポインタ」になる (&a + 1 は配列 1 個ぶん進む)
      if (earr) { ety = eaty + 65536; earr = 0; return v; }
      exit(5);
    }
    if (ebfw) exit(5);
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
/// @param n 大きさ (バイト)
/// @return 常に 0
/// @note IR の段で語ごとの load / store に展開する。出力段・dce・
///       レジスタ割付けに手を入れずに済むのが利点で，代わりに大きな
///       構造体では IR が長くなる。上限を設けて超えたら領域超過 (6)。
///
///       cc15p で構造体の大きさが 4 の倍数とは限らなくなった
///       (sizeof(struct { char c; }) は 1 である)。**書き過ぎると隣を
///       壊す**ので，語で写せるところまで写し，端数は半語・バイトで
///       始末する。
int scopy(int d, int s, int n) {
  int k; int da; int sa;
  if (n > 1024) exit(6);
  k = 0;
  while (k + 4 <= n) {
    da = emit(c_bin + b_add, d, emit(c_const, k, 0));
    sa = emit(c_bin + b_add, s, emit(c_const, k, 0));
    emit(c_stw, da, emit(c_loadw, sa, 0));
    k = k + 4;
  }
  if (k + 2 <= n) {
    da = emit(c_bin + b_add, d, emit(c_const, k, 0));
    sa = emit(c_bin + b_add, s, emit(c_const, k, 0));
    emit(c_sth, da, emit(c_loadhu, sa, 0));
    k = k + 2;
  }
  while (k < n) {
    da = emit(c_bin + b_add, d, emit(c_const, k, 0));
    sa = emit(c_bin + b_add, s, emit(c_const, k, 0));
    emit(c_stb, da, emit(c_loadb, sa, 0));
    k = k + 1;
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
  // ビットフィールドは読み・修正・書きが要る。黙って語ごと動かすと
  // 隣のフィールドを壊すので，対応するまで拒む
  if (ebfw) exit(5);
  if (isfp(t)) exit(5);      // 浮動小数点の ++/-- は cc15h で
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
  int v; int r; int op; int lt; int rt; int lh; int ah; int bh; int u;
  v = euna();
  while (tok == o_mul || tok == o_div || tok == o_mod) {
    op = tok;
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    r = rvany(euna());
    rt = ety;
    if (isfp(lt) || isfp(rt)) {
      // 浮動小数点の乗除算。double へ持ち上げて実行時支援を呼ぶ
      if (op == o_mod) exit(1);          // % は浮動小数点に無い (C の規則)
      bh = ehi;
      r = fplift(r, bh, rt);
      bh = ehi;
      v = fplift(v, lh, lt);
      ah = ehi;
      if (op == o_mul) v = ll_call2("__dmul", t_double, v, ah, r, bh);
      else v = ll_call2("__ddiv", t_double, v, ah, r, bh);
      ety = fpar2(lt, rt);
      if (ety == t_float) v = rtc21("__d2f", t_float, v, ehi);
      elv = 0; earr = 0;
      continue;
    }
    if (isll(lt) || isll(rt)) {
      ah = wide1(v, lh, lt);
      bh = wide1(r, ehi, rt);
      u = isull(lt) || isull(rt);
      if (op == o_mul) v = ll_mul(v, ah, r, bh);
      else if (op == o_div) {
        if (u) v = ll_call2("__udiv64", t_ullong, v, ah, r, bh);
        else v = ll_call2("__div64", t_llong, v, ah, r, bh);
      } else {
        if (u) v = ll_call2("__umod64", t_ullong, v, ah, r, bh);
        else v = ll_call2("__mod64", t_llong, v, ah, r, bh);
      }
      ety = llar2(lt, rt); elv = 0; earr = 0;
      continue;
    }
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
  int v; int r; int op; int lt; int lh; int ah; int bh; int lt2;
  v = emul();
  while (tok == o_add || tok == o_sub) {
    op = tok;
    v = rvany(v);
    lt = ety;
    lh = ehi;                  // 右辺の評価で ehi が上書きされるので控える
    next();
    r = rvany(emul());
    if (isfp(lt) || isfp(ety)) {
      // 浮動小数点の加減算。double へ持ち上げて実行時支援を呼ぶ
      bh = ehi;
      r = fplift(r, bh, ety);
      bh = ehi;
      lt2 = ety;
      v = fplift(v, lh, lt);
      ah = ehi;
      if (op == o_add) v = ll_call2("__dadd", t_double, v, ah, r, bh);
      else v = ll_call2("__dsub", t_double, v, ah, r, bh);
      ety = fpar2(lt, lt2);
      if (ety == t_float) v = rtc21("__d2f", t_float, v, ehi);
      elv = 0; earr = 0;
      continue;
    }
    if ((isll(lt) || isll(ety)) && (lt >> 16) == 0 && (ety >> 16) == 0) {
      // 64 bit の加減算。狭いほうを伸ばしてから 2 語で計算する
      ah = wide1(v, lh, lt);
      bh = wide1(r, ehi, ety);
      if (op == o_add) v = ll_addsub(v, ah, r, bh, 0);
      else v = ll_addsub(v, ah, r, bh, 1);
      ety = llar2(lt, ety);
      elv = 0;
      earr = 0;
      continue;
    }
    // ポインタ ± 64 bit 整数 (data + vtop->c.i)。ILP32 のアドレスは
    // 32 bit なので，整数側を下位語へ切り詰めて普通のポインタ演算にする
    if (isll(lt)) lt = 1;
    if (isll(ety)) ety = 1;
    ehi = 0;
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
  int v; int r; int op; int lt; int lh;
  v = eadd();
  while (tok == o_shl || tok == o_shr) {
    op = tok;
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    // 桁数は下位語の 32 bit で足りる (64 bit の桁数に意味は無い)
    r = rvany(eadd());
    if (isll(ety)) ehi = 0;
    else if (isfp(ety)) exit(5);
    if (isfp(lt) || isfp(ety)) exit(5);
    if (isll(lt)) {
      if (op == o_shl) v = ll_shl(v, wide1(v, lh, lt), r);
      else v = ll_shr(v, wide1(v, lh, lt), r, !isull(lt));
      ety = lt; elv = 0; earr = 0;
      continue;
    }
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
  int v; int r; int op; int lt; int lh; int ah; int bh; int u;
  v = eshift();
  while (tok == o_lt || tok == o_gt || tok == o_le || tok == o_ge) {
    op = tok;
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    r = rvany(eshift());
    if (isfp(lt) || isfp(ety)) {
      // 浮動小数点の比較。__dcmp (-1/0/1/2。2 = NaN) の値を突き合わせる
      bh = ehi;
      r = fplift(r, bh, ety);
      bh = ehi;
      v = fplift(v, lh, lt);
      ah = ehi;
      v = fpcmpres(rtc41("__dcmp", 1, v, ah, r, bh), op);
      ety = 1; elv = 0; earr = 0; ehi = 0;
      continue;
    }
    if (isll(lt) || isll(ety)) {
      ah = wide1(v, lh, lt);
      bh = wide1(r, ehi, ety);
      u = isull(lt) || isull(ety);
      // < と > は左右を入れ替えるだけ。<= と >= は逆の < を反転する
      if (op == o_lt) v = ll_lt(v, ah, r, bh, u);
      else if (op == o_gt) v = ll_lt(r, bh, v, ah, u);
      else if (op == o_le) v = emit(c_bin + b_xor, ll_lt(r, bh, v, ah, u),
                                    emit(c_const, 1, 0));
      else v = emit(c_bin + b_xor, ll_lt(v, ah, r, bh, u), emit(c_const, 1, 0));
      ety = 1; elv = 0; earr = 0; ehi = 0;
      continue;
    }
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
  int v; int r; int op; int lt; int lh; int bh;
  v = erel();
  while (tok == o_eq || tok == o_ne) {
    op = tok;
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    r = rvany(erel());
    if (isfp(lt) || isfp(ety)) {
      bh = ehi;
      r = fplift(r, bh, ety);
      bh = ehi;
      v = fplift(v, lh, lt);
      v = fpcmpres(rtc41("__dcmp", 1, v, ehi, r, bh), op);
      ety = 1; elv = 0; earr = 0; ehi = 0;
      continue;
    }
    if (isll(lt) || isll(ety)) {
      v = ll_eq(v, wide1(v, lh, lt), r, wide1(r, ehi, ety), op == o_ne);
      ety = 1; elv = 0; earr = 0;
      continue;
    }
    if (op == o_eq) v = emit(c_bin + b_seq, v, r);
    else v = emit(c_bin + b_sne, v, r);
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief ビット AND (&) を解析する。
/// @return 値番号
int eband() {
  int v; int r; int lt; int lh;
  v = eeq();
  while (tok == o_amp) {
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    r = rvany(eeq());
    if (isfp(lt) || isfp(ety)) exit(5);
    if (isll(lt) || isll(ety)) {
      // ビット演算は語ごとに独立なので，上下をそのまま行う
      v = ll_bit(v, wide1(v, lh, lt), r, wide1(r, ehi, ety), b_and);
      ety = llar2(lt, ety); elv = 0; earr = 0;
      continue;
    }
    v = emit(c_bin + b_and, v, r);
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief ビット XOR (^) を解析する。
/// @return 値番号
int exor() {
  int v; int r; int lt; int lh;
  v = eband();
  while (tok == o_xor) {
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    r = rvany(eband());
    if (isfp(lt) || isfp(ety)) exit(5);
    if (isll(lt) || isll(ety)) {
      // ビット演算は語ごとに独立なので，上下をそのまま行う
      v = ll_bit(v, wide1(v, lh, lt), r, wide1(r, ehi, ety), b_xor);
      ety = llar2(lt, ety); elv = 0; earr = 0;
      continue;
    }
    v = emit(c_bin + b_xor, v, r);
    ety = 1; elv = 0; earr = 0;
  }
  return v;
}

/// @brief ビット OR (|) を解析する。
/// @return 値番号
int ebor() {
  int v; int r; int lt; int lh;
  v = exor();
  while (tok == o_or) {
    v = rvany(v);
    lt = ety;
    lh = ehi;
    next();
    r = rvany(exor());
    if (isfp(lt) || isfp(ety)) exit(5);
    if (isll(lt) || isll(ety)) {
      // ビット演算は語ごとに独立なので，上下をそのまま行う
      v = ll_bit(v, wide1(v, lh, lt), r, wide1(r, ehi, ety), b_or);
      ety = llar2(lt, ety); elv = 0; earr = 0;
      continue;
    }
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
    v = rvc(v);
    off = hslot();
    l1 = newlab();
    l2 = newlab();
    emit(c_bz, v, l1);
    next();
    r = rvc(ebor());
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
    v = rvc(v);
    off = hslot();
    l1 = newlab();
    l2 = newlab();
    emit(c_bnz, v, l1);
    next();
    r = rvc(ecand());
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
  int w2; int off2;
  v = ecor();
  if (tok != o_que) return v;
  v = rvc(v);
  // && / || と同じ理由で隠しスロットを使う。この IR は値を「定義した命令の
  // 番号」で指すので，2 つの経路から届く値を 1 つの値番号では表せない。
  // 2 語の値 (long long / double) はスロットを 2 つ使う
  off = hslot();
  off2 = hslot();
  l1 = newlab();
  l2 = newlab();
  emit(c_bz, v, l1);
  next();
  a = rvany(expr());
  t = ety;
  w2 = is2w(t);
  emit(c_stw, emit(c_laddr, off, 0), a);
  if (w2) emit(c_stw, emit(c_laddr, off2, 0), ehi);
  emit(c_jmp, l2, 0);
  emit(c_lab, l1, 0);
  if (tok != o_col) exit(1);
  next();
  b = rvany(econd());
  if (w2 && !is2w(ety)) {
    // then が 2 語で else が 1 語ならそろえる (c ? 1LL : 0 など)
    if (isfp(t)) b = cvtto(b, t);
    else ehi = ext32(b, ety);
  } else if (!w2 && is2w(ety)) exit(5);   // 逆向きは会ってから
  emit(c_stw, emit(c_laddr, off, 0), b);
  if (w2) emit(c_stw, emit(c_laddr, off2, 0), ehi);
  emit(c_lab, l2, 0);
  v = emit(c_loadw, emit(c_laddr, off, 0), 0);
  if (w2) ehi = emit(c_loadw, emit(c_laddr, off2, 0), 0);
  else ehi = 0;
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
  int bw; int bo; int msk;
  int ch; int rh;
  v = econd();
  if (tok == o_asn) {
    // 返却されたばかりの構造体 (とそのメンバ) は一時領域にある。
    // 代入を許すと捨てられる領域へ書くだけになるので拒否する
    if (elv == 0 || erv) exit(5);
    t = ety;
    bw = ebfw; bo = ebfo;
    ebfw = 0;
    elv = 0;
    next();
    r = rvany(assign());
    if (isstru(t)) {
      // 同じ構造体型どうしでなければ代入できない。大きさが同じでも
      // 別の型なら別物である
      if (ety != t) exit(5);
      scopy(v, r, tsize(t));
    } else if (bw) {
      // ビットフィールドへの代入は読み・修正・書きになる。
      // 語を読み，そのフィールドのビットを消し，値をはめて書き戻す
      msk = (1 << bw) - 1;
      cur = ldval(v, 1);
      cur = emit(c_bin + b_and, cur,
                 emit(c_const, (msk << bo) ^ 0xffffffff, 0));
      res = emit(c_bin + b_and, r, emit(c_const, msk, 0));
      if (bo) res = emit(c_bin + b_sll, res, emit(c_const, bo, 0));
      res = emit(c_bin + b_or, cur, res);
      emit(c_stw, v, res);
    } else if (is2w(t)) {
      // 2 語の型は 2 語書く。右辺の型が違えば変換する
      if (isfp(t) || isfp(ety)) r = cvtto(r, t);
      else if (!isll(ety)) ehi = ext32(r, ety);
      emit(c_stw, v, r);
      emit(c_stw, emit(c_bin + b_add, v, emit(c_const, 4, 0)), ehi);
    } else {
      if (isfp(t) || isfp(ety)) r = cvtto(r, t);
      // 64 bit を狭い型へ入れるときは下位語だけを書く
      else if (isll(ety)) ehi = 0;
      stval(v, r, t);
    }
    ety = t; earr = 0;
    return r;
  }
  if (tok < o_asnb) return v;
  if (tok > o_asnb + 9) return v;
  if (elv == 0) exit(5);
  bw = ebfw; bo = ebfo;
  ebfw = 0;
  t = ety;
  op = tok - o_asnb;
  elv = 0;
  next();
  if (isll(t)) {
    // 64 bit の複合代入 (retval |= x)。2 語で読み，二項演算の既存の
    // 64 bit 経路で計算し，2 語書き戻す
    cur = emit(c_loadw, v, 0);
    ch = emit(c_loadw, emit(c_bin + b_add, v, emit(c_const, 4, 0)), 0);
    r = rvany(assign());
    if (isfp(ety)) exit(5);
    if (op == b_sll || op == b_srl) {
      // 桁数は下位語の 32 bit
      if (isll(ety)) ehi = 0;
      if (op == b_sll) res = ll_shl(cur, ch, r);
      else res = ll_shr(cur, ch, r, !isull(t));
    } else {
      rh = wide1(r, ehi, ety);
      if (op == b_add) res = ll_addsub(cur, ch, r, rh, 0);
      else if (op == b_sub) res = ll_addsub(cur, ch, r, rh, 1);
      else if (op == b_mul) res = ll_mul(cur, ch, r, rh);
      else if (op == b_div) {
        if (isull(t)) res = ll_call2("__udiv64", t_ullong, cur, ch, r, rh);
        else res = ll_call2("__div64", t_llong, cur, ch, r, rh);
      } else if (op == b_rem) {
        if (isull(t)) res = ll_call2("__umod64", t_ullong, cur, ch, r, rh);
        else res = ll_call2("__mod64", t_llong, cur, ch, r, rh);
      } else res = ll_bit(cur, ch, r, rh, op);
    }
    emit(c_stw, v, res);
    emit(c_stw, emit(c_bin + b_add, v, emit(c_const, 4, 0)), ehi);
    ety = t; elv = 0; earr = 0;
    return res;
  }
  if (isfp(t)) {
    // 浮動小数点の複合代入 (f += g)。読み，double へ持ち上げて演算し，
    // 目的の型へ戻して書く。演算は二項演算と同じ実行時支援を呼ぶ
    if (op != b_add && op != b_sub && op != b_mul && op != b_div) exit(5);
    if (t == t_double) {
      cur = emit(c_loadw, v, 0);
      ch = emit(c_loadw, emit(c_bin + b_add, v, emit(c_const, 4, 0)), 0);
    } else {
      cur = fplift(ldval(v, t), 0, t_float);
      ch = ehi;
    }
    r = rvany(assign());
    r = fplift(r, ehi, ety);
    rh = ehi;
    if (op == b_add) res = ll_call2("__dadd", t_double, cur, ch, r, rh);
    else if (op == b_sub) res = ll_call2("__dsub", t_double, cur, ch, r, rh);
    else if (op == b_mul) res = ll_call2("__dmul", t_double, cur, ch, r, rh);
    else res = ll_call2("__ddiv", t_double, cur, ch, r, rh);
    if (t == t_float) {
      res = rtc21("__d2f", t_float, res, ehi);
      stval(v, res, t);
      ety = t; elv = 0; earr = 0;
      return res;
    }
    emit(c_stw, v, res);
    emit(c_stw, emit(c_bin + b_add, v, emit(c_const, 4, 0)), ehi);
    ety = t; elv = 0; earr = 0;
    return res;
  }
  if (bw) {
    // ビットフィールドの複合代入 (sa->packed |= x)。語を読み，値を
    // 取り出し，演算し，はめ込んで書き戻す。取出しは符号なし
    msk = (1 << bw) - 1;
    cur = ldval(v, 1);
    ch = cur;
    if (bo) ch = emit(c_bin + b_srl, ch, emit(c_const, bo, 0));
    ch = emit(c_bin + b_and, ch, emit(c_const, msk, 0));
    r = rv(assign());
    res = emit(c_bin + b_and, emit(c_bin + op, ch, r), emit(c_const, msk, 0));
    rh = res;
    if (bo) rh = emit(c_bin + b_sll, rh, emit(c_const, bo, 0));
    cur = emit(c_bin + b_and, cur, emit(c_const, (msk << bo) ^ 0xffffffff, 0));
    emit(c_stw, v, emit(c_bin + b_or, cur, rh));
    ety = 1; elv = 0; earr = 0;
    return res;
  }
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
    if (streq(glname + i * 64, tname)) return i;
    i = i + 1;
  }
  if (glcnt > 63) exit(6);
  i = glcnt;
  glcnt = glcnt + 1;
  copyn(glname + i * 64, tname);
  gllab[i] = newlab();
  gldef[i] = 0;
  return i;
}

/// @brief case のラベル値を読む。
/// @return 値
/// @note 整数定数と文字定数に限る (先頭の - は許す)。enum 定数を含む
///       定数式は第 2 部で enum と一緒に扱う。
int caseval() {
  // case の値は整数定数式である (case LENS: や case 1 + 1:)。
  // 宣言子と同じ畳込み評価器で読む (docs/stage014-external.md 13.2)
  return ccond();
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
    d = lblk;
    lblk = lcnt;            // ここから内側。外側と同名を宣言してよい
    while (istype()) plocal();
    while (tok != o_rc) stmt();
    next();
    lcnt = n;
    cloff = b0;
    lblk = d;
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
      // ラベルつき文は「ラベル + 文」で 1 つの文である (C89 6.6.1)。
      // ここで返ると if (c) lab: s; の s が if の外の文になってしまい，
      // 条件に関わらず走る (第 6 部の実測: tcc の parse_define)
      return stmt();
    }
  }
  if (tok == k_if) {
    next();
    if (tok != o_lp) exit(1);
    next();
    c = rvc(expr());
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
    c = rvc(expr());
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
    c = rvc(expr());
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
      c = rvc(expr());
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
    if (swn > 2047) exit(6);
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
      c = rvany(expr());
      if (is2w(cretty)) {
        // 2 語の返却。上位語を先に積むと，下位語 (c_ret が積む) の
        // 1 つ上に来る
        if (isfp(cretty) || isfp(ety)) c = cvtto(c, cretty);
        else if (!isll(ety)) ehi = ext32(c, ety);
        emit(c_arg, ehi, 0);
      } else if (isfp(cretty) || isfp(ety)) {
        c = cvtto(c, cretty);   // float の返却は 1 語
        ehi = 0;
      } else if (ehi) {
        ehi = 0;             // 狭い型へ返すときは下位語だけを返す
      }
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
  int o;
  if (vreg[v] >= 0) return vreg[v];
  o = spbase + (0 - 2 - vreg[v]) * 4;
  if (o < 2048) outw(iw3(0x2003, sc2, 8, o));
  else { laddr(sc2, o); outw(iw3(0x2003, sc2, sc2, 0)); }
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
  int o;
  if (vreg[v] >= 0) return 0;
  if (vreg[v] == -1) return 0;
  o = spbase + (0 - 2 - vreg[v]) * 4;
  if (o < 2048) outw(sw3(0x2023, 8, 10, o));
  else {
    // 書きたい値が x10 にあるので，アドレスはもう片方の作業レジスタで作る。
    // x11 のオペランドは結果を作った時点で用済みなので潰してよい
    laddr(11, o);
    outw(sw3(0x2023, 11, 10, 0));
  }
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
    if (lfixn > 32767) exit(6);
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
  if (k == b_mulhu) return 0x02003033;    // mulhu
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
    laddr(d, ia[i]);
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
    if (spfn > 2047) exit(6);
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
    if (iret[i] && is2w(gty[b])) {
      // 2 語の返却 (64 bit 整数と double)。上位語が 1 語だけ 1 つ上に
      // 積まれている。**ここを isll のままにすると double を返す呼出しが
      // 構造体の返却として扱われ，データスタックの位置がずれて後続の
      // 引数コピーが暴走する** (cc15h / cc15i に潜在していた。仮引数の
      // 浮動小数点を入れた検査 fparg が最初に踏んだ)
      outw(iw3(0x2003, 10, 9, 4));
      if (iret[i] < 2048) outw(sw3(0x2023, 8, 10, iret[i]));
      else { laddr(11, iret[i]); outw(sw3(0x2023, 11, 10, 0)); }
      a = 8;
    } else if (iret[i]) {
      // 構造体の返却。1 語目の上に語 0 から順に積まれている。
      // x9 を戻す前にフレームの一時領域へ引き取る (@section strret)。
      // 呼出しの値そのものは使われないので x10 を壊してよい
      k = tsize(gty[b]);
      j = 0;
      while (j < k) {
        outw(iw3(0x2003, 10, 9, 4 + j));
        if (iret[i] + j < 2048) outw(sw3(0x2023, 8, 10, iret[i] + j));
        else { laddr(11, iret[i] + j); outw(sw3(0x2023, 11, 10, 0)); }
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
    if (rused[r]) {
      if (o < 2048) outw(iw3(0x2003, r, 8, o));
      else { laddr(10, o); outw(iw3(0x2003, r, 10, 0)); }
      o = o + 4;
    }
    r = r + 1;
  }
  outw(0x00012083);
  outw(0x00412403);
  // 戻り値はデータスタック (x9) に置いた後なので x10 を作業に使ってよい
  if (fnf < 2048) outw(iw3(0x13, 2, 2, fnf));
  else {
    liw(10, fnf);
    outw(0x33 | (2 << 7) | (2 << 15) | (10 << 20));
  }
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
  if (fnf > 65528) exit(6);
  // 関数アドレス (.text 内オフセット) の確定。呼出し側は再配置で解決される
  gval[e] = outp;
  gdef[e] = 1;
  // プロローグ。フレームが addi の即値に収まらない場合は x10 を作業に使う
  // (x10/x11 は割付けから外してあり，引数はデータスタック経由なので空いている)
  if (fnf < 2048) outw((((0 - fnf) & 4095) << 20) | 0x10113);
  else {
    liw(10, fnf);
    outw(0x40000033 | (2 << 7) | (2 << 15) | (10 << 20));
  }
  outw(0x00112023);
  outw(0x00812223);
  outw(0x00010413);
  o = svbase;
  r = 13;
  while (r < 28) {
    if (rused[r]) {
      if (o < 2048) outw(sw3(0x2023, 8, r, o));
      else { laddr(10, o); outw(sw3(0x2023, 10, r, 0)); }
      o = o + 4;
    }
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
  if (cvaoff >= 0) {
    if (cvaoff < 2048) outw(sw3(0x2023, 8, 9, cvaoff));
    else { laddr(10, cvaoff); outw(sw3(0x2023, 10, 9, 0)); }
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
  // 文字列プールを本体の後ろへ出し，参照ごとにローカルシンボルと再配置を作る
  o = outp;
  i = 0;
  while (i < spcnt) { outbyte(spool[i]); i = i + 1; }
  i = 0;
  while (i < spfn) {
    if (nlsym > 8191) exit(6);
    lsoff[nlsym] = o + spofs[i];     // この文字列の .text 内オフセット
    addrel(spfix[i], 0 - 1 - nlsym, r_hi20, 0);
    addrel(spfix[i] + 4, 0 - 1 - nlsym, r_lo12i, 0);
    nlsym = nlsym + 1;
    i = i + 1;
  }
  // **関数内 static の初期化子が予約した文字列も同じプールに在る。**
  // gstrflush() は ginfn のとき呼ばれない (呼ぶと初期化子の実体が
  // ずれる) ので，ここで埋めないと lsoff が空のまま残り，ポインタが
  // 別の場所を指す (docs/stage017-cc.md 24 章)
  i = 0;
  while (i < gspn) {
    lsoff[gspsym[i]] = o + gspofs[i];
    i = i + 1;
  }
  gspn = 0;
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

/// @brief 無名メンバの展開。内側の構造体 si のメンバを外側 se へ複写する。
/// @return 常に 0
/// @note 内側にさらに無名メンバがあっても，内側の strudef が先に展開を
///       済ませているので 1 段の複写で足りる。配置は「型 si の普通の
///       メンバを置く場所」+ 内側でのオフセットになる。
int msplice(int se, int si) {
  int m; int off0; int mm;
  bfword = 0 - 1;
  if (salign[se] < salign[si]) salign[se] = salign[si];
  if (sunion[se]) off0 = 0;
  else off0 = roundup(ssize[se], salign[si]);
  m = 0;
  mm = mcnt;
  while (m < mm) {
    if (msid[m] == si) {
      if (mcnt > 16383) exit(6);
      copyn(mname + mcnt * 64, mname + m * 64);
      msid[mcnt] = se;
      mty[mcnt] = mty[m];
      mbfw[mcnt] = mbfw[m];
      mbfo[mcnt] = mbfo[m];
      marr[mcnt] = marr[m];
      msz[mcnt] = msz[m];
      moff[mcnt] = off0 + moff[m];
      mcnt = mcnt + 1;
    }
    m = m + 1;
  }
  if (sunion[se]) {
    if (ssize[se] < ssize[si]) ssize[se] = ssize[si];
  } else {
    ssize[se] = off0 + ssize[si];
  }
  return 0;
}

int memb(int se, int t, int fp) {
  int m;
  int sz;
  int al;
  int u;
  // 入れ子のメンバ (struct A { struct B b; }) を許す。自分自身を
  // メンバに持つことはできない (大きさが決まらない)
  if (isstru(t) && t == 2 + se) exit(5);
  // 不完全型 (前方参照だけのタグ) は値で持てない。ポインタは通る
  if (isstru(t) && ssize[t - 2] == 0) exit(5);
  // fp = 1 は関数ポインタのメンバ (int (*fn)(...)。名前は呼び手が
  // fnpdec で読み終えて tname に置いてある。宣言子は消費済みなので
  // 名前の読取りと pdims は行わない
  if (!fp) {
    if (tok != t_id) exit(1);
  }
  if (mfind(se) >= 0) exit(4);
  if (mcnt > 16383) exit(6);
  m = mcnt;
  mcnt = mcnt + 1;
  msid[m] = se;
  copyn(mname + m * 64, tname);
  mty[m] = t;
  if (!fp) {
    next();
    if (tok == o_col) {
      // ビットフィールド (docs/stage014-external.md 9 章)。語 (32 bit) の
      // 中へ下位ビットから詰める。語に収まらない幅は次の語へ送る
      next();
      if (tok != t_num) exit(1);
      if (tval < 1) exit(2);
      if (tval > 31) exit(2);
      if ((t >> 16) != 0) exit(2);
      if (isstru(t)) exit(2);
      mbfw[m] = tval;
      next();
      mty[m] = t;
      marr[m] = 0;
      // 記憶単位は**宣言された型**である (cc15o までは一律 4 バイトで，
      // unsigned short / unsigned char の宣言を無視していた)。
      // 幅がその単位に収まらなければ次の単位へ送る
      u = tsize(t);
      msz[m] = u;
      if (mbfw[m] > u * 8) exit(2);
      if (salign[se] < u) salign[se] = u;
      if (sunion[se]) {
        moff[m] = 0;
        mbfo[m] = 0;
        if (ssize[se] < u) ssize[se] = u;
      } else if (bfword >= 0 && bfunit == u && bfbit + mbfw[m] <= u * 8) {
        moff[m] = bfword;
        mbfo[m] = bfbit;
        bfbit = bfbit + mbfw[m];
      } else {
        moff[m] = roundup(ssize[se], u);
        mbfo[m] = 0;
        bfword = moff[m];
        bfbit = mbfw[m];
        bfunit = u;
        ssize[se] = moff[m] + u;
      }
      return 0;
    }
    // まずメンバの大きさと整列を決め，配置は struct / union で分ける。
    // 整列は「いちばん奥の要素の型」で決まる (char の配列だけ 1 バイト境界)
    t = pdims(t);
  }
  mty[m] = t;
  mbfw[m] = 0;
  // 普通のメンバを挟んだら，ビットフィールドの詰めは次の語からやり直す
  bfword = 0 - 1;
  sz = tsize(t);
  al = talign(t);
  if (isarr(t)) marr[m] = 1;
  // スカラのメンバは 1 語に収まる，という前提で 4 に丸めていた。
  // 64 bit は 2 語なので丸めから外す (丸めると隣のメンバを上位語が壊す)
  else { marr[m] = 0; sz = tsize(t); if (sz > 4 && !isstru(t) && !is2w(t)) sz = 4; }
  msz[m] = sz;
  // 構造体の整列はメンバの整列の最大である
  if (salign[se] < al) salign[se] = al;
  if (sunion[se]) {
    // union のメンバはすべて先頭に重なる。大きさは最大値
    moff[m] = 0;
    if (sz > ssize[se]) ssize[se] = sz;
  } else {
    moff[m] = roundup(ssize[se], al);
    ssize[se] = moff[m] + sz;
  }
  return 0;
}

/// @brief 構造体定義 (struct 名 { ... };) を解析して登録する。
/// @return 常に 0
/// @note 先に空の構造体として登録してからメンバを読む。こうすると
///       メンバに自分自身へのポインタ (連結リストの next など) を書ける。
int strudef(int u) {
  int se;
  int b;
  int bb;
  int done;
  int obw; int obb; int obu;
  obw = bfword; obb = bfbit; obu = bfunit;
  bfword = 0 - 1;
  copyn(tname, snam);
  se = sfind();
  if (se >= 0) {
    // 前方参照で不完全のまま登録されたタグをここで埋める。
    // 既に本体を持つ (ssize != 0) なら重複定義
    if (ssize[se] != 0) exit(4);
    if (sunion[se] != u) exit(4);
  } else {
    if (scnt > 2047) exit(6);
    se = scnt;
    scnt = scnt + 1;
    copyn(sname + se * 64, tname);
    ssize[se] = 0;
    salign[se] = 1;
    sunion[se] = u;
  }
  next();
  while (tok != o_rc) {
    bb = ptype();
    b = pstars(bb);
    if (tok == o_semi && (b >> 16) == 0 && isstru(b)) {
      // 無名メンバ (union { ... }; / struct { ... };。tcc の SValue)。
      // 内側のメンバを外側の表へ展開すると，アクセス名が直に見える
      msplice(se, b - 2);
      next();
    } else {
      // 1 つの宣言に宣言子を複数書ける (int jtrue, jfalse;)。
      // '*' は宣言子ごとに付くので基底型 bb から取り直す
      done = 0;
      while (!done) {
        if (isfnp()) {
          // 関数ポインタのメンバ (docs/stage014-external.md 7.1)
          b = fnpdec(b);
          copyn(tname, fpnam);
          memb(se, b, 1);
        } else {
          memb(se, b, 0);
        }
        if (tok == o_comma) { next(); b = pstars(bb); }
        else done = 1;
      }
      if (tok != o_semi) exit(1);
      next();
    }
  }
  next();
  // 大きさは**自身の整列**の倍数へ丸める (cc15o までは一律 4 だった。
  // C89 6.5.2.1 の「末尾の詰め」で，配列に並べたとき各要素の整列が
  // 保たれるようにするためのものである)
  ssize[se] = roundup(ssize[se], salign[se]);
  // 大きさ 0 は「不完全」の印なので，メンバの無い定義も最小 4 にする
  if (ssize[se] == 0) ssize[se] = 4;
  bfword = obw; bfbit = obb; bfunit = obu;
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
    if (lfind() >= lblk) exit(4);
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
    if (lfind() >= lblk) exit(4);
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
  // 実引数の変換 (ecallseq) のために語の位置を控える。ビット表は
  // 32 語ぶんしかないので，それより深い位置は宣言の時点で拒む
  if (isll(b) || isfp(b)) {
    if (cna > 31) exit(6);
    if (isll(b)) cllm = cllm | (1 << cna);
    else if (b == t_double) cdbm = cdbm | (1 << cna);
    else cflm = cflm | (1 << cna);
  }
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
  int m; int pe;
  t = lty[i];
  next();
  if ((t >> 16) == 0 && isstru(t) && tok == o_lc) {
    // 構造体の局所の初期化子 (CType ct = { x, 0 };)。まず全体を 0 で
    // 埋め，与えられた式を宣言順のメンバへ代入する。無名 union の展開で
    // 重なったメンバ (前のメンバの範囲に入る位置) は C の規則どおり
    // 読み飛ばす (union は最初の腕だけが初期化を受ける)
    n = tsize(t);
    k = 0;
    while (k < n) {
      a = emit(c_bin + b_add, emit(c_laddr, loff[i], 0), emit(c_const, k, 0));
      emit(c_stw, a, emit(c_const, 0, 0));
      k = k + 4;
    }
    next();
    m = 0;
    pe = 0;
    while (m < mcnt && tok != o_rc) {
      if (msid[m] == t - 2 && moff[m] >= pe) {
        // ビットフィールド・配列・入れ子の構造体のメンバは会ってから
        if (mbfw[m] || marr[m] || isstru(mty[m])) exit(5);
        pe = moff[m] + msz[m];
        v = rvany(assign());
        a = emit(c_bin + b_add, emit(c_laddr, loff[i], 0),
                 emit(c_const, moff[m], 0));
        if (is2w(mty[m])) {
          if (isfp(mty[m]) || isfp(ety)) v = cvtto(v, mty[m]);
          else if (!isll(ety)) ehi = ext32(v, ety);
          emit(c_stw, a, v);
          emit(c_stw, emit(c_bin + b_add, a, emit(c_const, 4, 0)), ehi);
        } else {
          if (isfp(mty[m]) || isfp(ety)) v = cvtto(v, mty[m]);
          else if (isll(ety)) ehi = 0;
          stval(a, v, mty[m]);
        }
        if (tok == o_comma) next();
        else if (tok != o_rc) exit(1);
      }
      m = m + 1;
    }
    if (tok != o_rc) exit(1);
    next();
    return 0;
  }
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
  v = rvany(assign());
  if (isstru(t)) {
    // 局所の構造体・共用体を式で初期化する (struct S d = 式;)。
    // 代入と同じく語ごとの複写を出す。cc15n までここが無く，下の
    // stval へ落ちて 4 バイトしか書いていなかった (初期化が黙って
    // 落ちる。docs/stage015-tcc.md 12.22)
    if (ety != t) exit(5);
    scopy(emit(c_laddr, loff[i], 0), v, tsize(t));
    return 0;
  }
  if (is2w(t)) {
    // 2 語の局所変数。右辺の型が違えば変換する
    if (isfp(t) || isfp(ety)) v = cvtto(v, t);
    else if (!isll(ety)) ehi = ext32(v, ety);
    a = emit(c_laddr, loff[i], 0);
    emit(c_stw, a, v);
    emit(c_stw, emit(c_bin + b_add, a, emit(c_const, 4, 0)), ehi);
    return 0;
  }
  if (isfp(t) || isfp(ety)) v = cvtto(v, t);   // float の局所は 1 語
  else if (isll(ety)) ehi = 0;   // 64 bit を狭い型へ: 下位語だけ残す (代入と同じ)
  stval(emit(c_laddr, loff[i], 0), v, t);
  return 0;
}

/// @brief 関数内 static の実体を大域表へ別名で登録する。
/// @param li 局所記号の番号 (名前の元)
/// @return 大域記号の番号
/// @note 別名は「元の名前 + '.' + 通し番号」。'.' は識別子に使えないので
///       利用者の大域と衝突せず，一意性は通し番号だけで決まる (関数が
///       違っても同じ名前の static を持てる)。ELF にはローカルシンボル
///       として出る (gsta)
int slocnew(int li) {
  int e; int n; int c;
  copyn(tname, lname + li * 64);
  n = 0;
  while (n < 58 && tname[n]) n = n + 1;
  tname[n] = '.';
  c = slocnt;
  slocnt = slocnt + 1;
  if (c > 999) exit(6);
  tname[n + 1] = '0' + c / 100;
  tname[n + 2] = '0' + c / 10 % 10;
  tname[n + 3] = '0' + c % 10;
  tname[n + 4] = 0;
  e = gnew();
  return e;
}

int plocal() {
  int base; int b; int i; int fp; int sta; int g;
  // register / auto は読み捨てる (記憶域の割付けは変えない)。
  // static は実体の置き場をフレームから .bss へ変える
  // (docs/stage014-external.md 7 章)
  sta = 0;
  while (tok == k_register || tok == k_auto || tok == k_static) {
    if (tok == k_static) sta = 1;
    next();
  }
  base = ptype();
  // 1 つの宣言に宣言子を複数書ける (int a, *p, v[4];)。
  // '*' と '[n]' は宣言子ごとに付くので，基底型 base から取り直す
  while (1) {
    b = pstars(base);
    fp = 0;
    if (isfnp()) {
      b = fnpdec(b);
      copyn(tname, fpnam);
      fp = 1;
    }
    if (tok != t_id && !fp) exit(1);
    if (lfind() >= lblk) exit(4);
    i = lnew();
    loff[i] = cloff;
    if (!fp) {
      next();
      b = pdims(b);
    }
    if (isstru(b) && ssize[b - 2] == 0) exit(5);
    lty[i] = b;
    lsz[i] = tsize(b);
    if (sta) {
      // 関数内 static。実体はフレームではなく .bss (初期化子つきなら
      // .text 内のデータ) に取り，呼出しをまたいで値が残る
      // (docs/stage014-external.md 13.2 の staticinit)
      if (isarr(b)) larr[i] = 1;
      else larr[i] = 0;
      g = slocnew(i);
      gkind[g] = 0;
      gty[g] = b;
      gna[g] = 0;
      gsta[g] = 1;
      gsz[g] = tsize(b);
      if (isarr(b)) garr[g] = 1;
      else garr[g] = 0;
      gdef[g] = 1;
      lsg[i] = g;
      if (tok == o_asn) {
        // 大域と同じ初期化子書き。文字列の flush だけ関数末尾に遅らせる
        gtxt[g] = 1;
        ginfn = 1;
        ginit(g);
        ginfn = 0;
      } else {
        gtxt[g] = 0;
        gval[g] = bssp;
        bssp = bssp + ((gsz[g] + 3) & 0xfffffffc);
      }
    } else if (isarr(b)) {
      larr[i] = 1;
      frame1((lsz[i] + 3) & 0xfffffffc);
    } else {
      larr[i] = 0;
      // 構造体は 1 語に収まらない。実体をフレーム上に取る
      // (構造体の大きさは登録時に 4 の倍数へ丸めてある)
      if (isstru(b)) frame1(lsz[i]);
      else if (is2w(b)) frame1(8);   // 2 語の型は下位語・上位語で 2 語
      else frame1(4);
    }
    if (tok == o_asn) linit(i);
    if (tok != o_comma) break;
    next();
  }
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
/// @brief K&R 形式の仮引数の型宣言を 1 行読む (int a, b; など)。
/// @return 常に 0
/// @note 名前は '(' の並びで登録済みでなければならない。配列は
///       先頭要素へのポインタになる (仮引数の C の規則)
int krdecl() {
  int base; int b; int i;
  base = ptype();
  while (1) {
    b = pstars(base);
    if (tok != t_id) exit(1);
    i = lfind();
    if (i < 0) exit(2);
    next();
    b = pdims(b);
    if (isarr(b)) b = adecay(b);
    lty[i] = b;
    lsz[i] = tsize(b);
    if (tok != o_comma) break;
    next();
  }
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

int funcdef() {
  int e; int i; int krf;
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
  lblk = 0;
  cna = 0;
  cllm = 0;
  cdbm = 0;
  cflm = 0;
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
  krf = 0;
  if (tok == t_id && tdfind() < 0) {
    // K&R 形式: 名前だけの並び (int add(a, b))。型は ')' の後の宣言で
    // 与えられ，与えられなければ int (docs/stage014-external.md 8.2)
    krf = 1;
    while (1) {
      if (tok != t_id) exit(1);
      if (lfind() >= lblk) exit(4);
      i = lnew();
      loff[i] = 8 + cna * 4;
      lty[i] = 1;
      larr[i] = 0;
      lsz[i] = 4;
      cna = cna + 1;
      next();
      if (tok != o_comma) break;
      next();
    }
  } else if (tok != o_rp) {
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
  // K&R 形式の仮引数の型宣言。')' と '{' の間に置かれる
  while (krf && istype()) krdecl();
  // 個数が判らないまま呼出しを出した後で可変長と判ると，既に出した
  // 呼出しの積み方が間違っている。宣言を先に置かせるしかない
  if (cvarg && gused[e]) exit(5);
  if (gna[e] >= 0 && gvar[e] != cvarg) exit(5);
  gvar[e] = cvarg;
  if (tok == o_semi) {
    // プロトタイプ宣言。引数の個数と返却型だけを控え，本体は待つ
    next();
    if (gna[e] >= 0 && gna[e] != cna) exit(5);
    if (gna[e] >= 0 && gllm[e] != cllm) exit(5);
    if (gna[e] >= 0 && (gdbm[e] != cdbm || gflm[e] != cflm)) exit(5);
    gna[e] = cna;
    gllm[e] = cllm;
    gdbm[e] = cdbm;
    gflm[e] = cflm;
    lcnt = 0;
  lblk = 0;
    return 0;
  }
  if (gdef[e]) exit(4);
  gdef[e] = 1;
  gsta[e] = cstat;
  copyn(fnname, gname + e * 64);   // __FUNCTION__ が返す名前
  // 先にプロトタイプがあれば，引数の個数が一致していなければならない
  if (gna[e] >= 0 && gna[e] != cna) exit(5);
  if (gna[e] >= 0 && gllm[e] != cllm) exit(5);
  if (gna[e] >= 0 && (gdbm[e] != cdbm || gflm[e] != cflm)) exit(5);
  gna[e] = cna;
  gllm[e] = cllm;
  gdbm[e] = cdbm;
  gflm[e] = cflm;
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
  int se; int m; int base; int i;
  w = tsize(t);
  // 入れ子の初期化子 (docs/stage014-external.md 6.1)。
  //   構造体   … メンバを順に埋め，隙間と残りは 0 で埋める
  //   それ以外 … 多次元配列を平らにした列の一部とみなし，透過的に読む
  //              (ginit が既に要素型を畳んでいるため)
  if (tok == o_lc) {
    next();
    if (isstru(t)) {
      se = t - 2;
      base = outp;
      m = 0;
      while (m < mcnt) {
        if (msid[m] == se) {
          if (tok == o_rc) break;
          while (outp - base < moff[m]) outbyte(0);
          ginit1(mty[m]);
          if (tok == o_comma) next();
          else if (tok != o_rc) exit(1);
        }
        m = m + 1;
      }
      while (outp - base < tsize(t)) outbyte(0);
    } else if (isarr(t)) {
      // **配列そのものを波括弧で受ける** (cc15s)。ginit が畳まなく
      // なったので，ここへ `char[8]` のような対象が波括弧つきで来る。
      //
      // 要素型で読み，宣言した幅に足りない分を 0 で埋める。下は
      // `char a[8] = { "ab" }` の形 —— 波括弧の中が文字列なら，
      // 波括弧が無いときと同じに実体を並べる (C89 6.5.7)
      base = outp;
      if (tok == t_str && ischararr(t)) {
        i = 0;
        while (i < w) {
          if (i < slen) outbyte(sbuf[i]);
          else outbyte(0);
          i = i + 1;
        }
        next();
      } else {
        se = aelem[t - t_arr];
        while (tok != o_rc) {
          ginit1(se);
          if (tok == o_comma) next();
          else if (tok != o_rc) exit(1);
        }
      }
      while (outp - base < w) outbyte(0);
    } else {
      while (tok != o_rc) {
        ginit1(t);
        if (tok == o_comma) next();
        else if (tok != o_rc) exit(1);
      }
    }
    if (tok != o_rc) exit(1);
    next();
    return 0;
  }
  if (tok == o_lp) {
    // キャスト ((char *)"abc" や (alloc_func)0) は読み飛ばす。書く幅は
    // 対象の型 t が決めるので，キャスト先の型は使わない。
    // 「( 型 )」でなければ括弧つき定数式なので，末尾の畳込みへ落とす
    lsave();
    next();
    if (istype()) {
      pstars(ptype());
      if (tok != o_rp) exit(1);
      next();
      return ginit1(t);
    }
    lrest();
  }
  if (tok == t_str && ischararr(t)) {
    // 構造体の中の char 配列メンバ (struct { char a[8]; } s = { "abc" };)。
    // ここは **字の実体を並べる場所** であって，ポインタを置く場所ではない。
    // ginit の char s[] = "abc" と同じに，宣言した幅ぶん並べ，残りを 0 で
    // 埋める (溢れる分は捨てる)。この判定を t_str の分岐より前に置かないと，
    // 4 バイトの再配置が書かれて配列の中身がアドレスになる。
    //
    // **「配列か」だけで見てはいけない。** char *a[1] もここへ落ちてきて，
    // ポインタの枠に字が入る。そうなると参照先が不正になり，値が違うでは
    // 済まず落ちる。文字型の配列だけを受ける (ischararr)
    i = 0;
    while (i < w) {
      if (i < slen) outbyte(sbuf[i]);
      else outbyte(0);
      i = i + 1;
    }
    next();
    return 0;
  }
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
      // 列挙定数から始まる定数式 (LENS や X | Y)
      if (ecfind() < 0) exit(2);
      ccll = 0;
      v = ccond();
      if (w == 1) outbyte(v & 255);
      else if (w == 2) { outbyte(v & 255); outbyte((v >> 8) & 255); }
      else if (w == 8) {
        outw4(v);
        if (ccll) outw4(cchi);
        else if (v < 0) outw4(0 - 1);
        else outw4(0);
      }
      else outw4(v);
      return 0;
    }
    next();
    addrel(outp, e, r_32, 0);
    outw4(0);
    return 0;
  }
  // 浮動小数点の初期化子はリテラル (符号つき可) だけを受ける。
  // 定数式の畳込みは整数用なので混ぜない
  if (isfp(t)) {
    neg = 0;
    if (tok == o_sub) { neg = 1; next(); }
    if (tok != t_num) exit(5);
    if (tvalfp == 0) {
      // 整数リテラルで初期化する形 (double x = 5;)。コンパイル時に変換
      if (tvalll) exit(5);
      se = tval;
      if (se < 0) { neg = 1 - neg; se = 0 - se; }
      lexi2d(se);
    }
    if (t == t_double) {
      if (tvalfp == 2) exit(5);      // f 付きを double に入れる形は後回し
      if (neg) tvalhi = tvalhi ^ 0x80000000;
      outw4(tval);
      outw4(tvalhi);
    } else {
      if (tvalfp == 1) { tval = cd2f(((unsigned long long)(unsigned)tvalhi << 32) | (unsigned long long)(unsigned)tval); }
      if (neg) tval = tval ^ 0x80000000;
      outw4(tval);
    }
    next();
    return 0;
  }
  // 残りは整数定数式 (数・負号・括弧式・~ ! を含む)。
  // 宣言子と同じ畳込み評価器で読む (docs/stage014-external.md 13.2)
  ccll = 0;
  v = ccond();
  if (w == 1) outbyte(v & 255);
  else if (w == 2) { outbyte(v & 255); outbyte((v >> 8) & 255); }
  else if (w == 8) {
    // 64 bit は下位語・上位語の順で 2 語 (little endian)。64 bit の
    // リテラルは評価器が cchi に上位語を残す。32 bit の値は符号拡張する
    // (unsigned long long x = -1 が全 bit 1 になる C の変換規則)
    outw4(v);
    if (ccll) outw4(cchi);
    else if (v < 0) outw4(0 - 1);
    else outw4(0);
  }
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
  if (spcnt + p > 65535) exit(6);
  if (nlsym > 8191) exit(6);
  if (gspn > 4095) exit(6);
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
  int t; int n; int i; int es; int flat; int ibase; int k; int sub;
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
    // **畳んでよいかを，初期化子の書かれ方で決める。**
    //
    // 多次元を平らに畳むのは `int a[2][2] = {1,2,3,4}` のような
    // 「平らに並べた書き方」のための近道である。ところが
    // `char m[2][8] = {"ab","cd"}` の波括弧の中の 1 つ 1 つは
    // **行 (char[8]) を初期化する**のであって，畳んだ後の要素 (char) を
    // 初期化するのではない。畳んでしまうと ginit1() の「対象が配列なら
    // 実体を並べる」(cc15q) が効かず，要素 1 つに 4 バイトの再配置が
    // 書かれて中身がアドレスになる —— 台帳で bad と呼ぶ状態である。
    //
    // 見分け方は「`{` の次が `{` か文字列か」である。そうなら下位の
    // 対象そのものを初期化しているので**畳まない**。そうでなければ
    // 平らな並びなので今までどおり畳む (出るバイト列は変わらない)。
    sub = 0;
    if (tok == o_lc) {
      lsave();
      next();
      if (tok == o_lc || tok == t_str) sub = 1;
      lrest();
    }
    // `char a[8] = { "ab" }` は `char a[8] = "ab"` と同じ意味である
    // (C89 6.5.7)。**1 次元の文字型配列だけ**がここに当たる ——
    // `char m[2][8]` の要素型は char[8] なので ischararr(t) は立たない。
    // 波括弧を剥がして，下の t_str の道と同じことをする
    if (sub && ischararr(t)) {
      next();                           // '{'
      if (tok != t_str) exit(1);
      i = 0;
      if (n == 0) n = slen + 1;
      while (i < n) {
        if (i < slen) outbyte(sbuf[i]);
        else outbyte(0);
        i = i + 1;
      }
      next();
      if (tok != o_rc) exit(1);
      next();
      while (outp & 3) outbyte(0);
      gty[e] = atype(es, n);
      gsz[e] = n;
      if (!ginfn) gstrflush();
      return 0;
    }
    flat = 0;
    while (isarr(es) && !sub) {
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
      if (!ginfn) gstrflush();
      return 0;
    }
    if (tok != o_lc) exit(1);
    next();
    ibase = outp;
    while (tok != o_rc) {
      ginit1(es);
      if (tok == o_comma) next();
      else if (tok != o_rc) exit(1);
    }
    next();
    // 出したバイト数から要素数を求める。入れ子の初期化子は 1 回の呼出しで
    // 複数の要素を出すので，呼出し回数では数えられない
    i = (outp - ibase) / tsize(es);
    if (n == 0) n = i;
    if (i > n) exit(6);
    // 足りない分は 0 で埋める。**要素の大きさぶん埋める** ——
    // 1 / 2 / 4 を並べる書き方だと，畳まなくなった char[8] のような
    // 要素で 4 バイトしか埋まらない。1 / 2 / 4 のときの出るバイト列は
    // 前と同じである (outw4(0) は 0 を 4 バイト書く)
    while (i < n) {
      k = tsize(es);
      while (k > 0) { outbyte(0); k = k - 1; }
      i = i + 1;
    }
    while (outp & 3) outbyte(0);
    if (!flat) { gty[e] = atype(es, n); gsz[e] = n * tsize(es); }
    if (!ginfn) gstrflush();
    return 0;
  }
  ginit1(t);
  while (outp & 3) outbyte(0);
  gsz[e] = tsize(t);
  if (!ginfn) gstrflush();
  return 0;
}

/// @brief 型を読んだ後の宣言を処理する (関数定義か大域変数かをここで分ける)。
/// @param b 基底型
/// @return 常に 0
/// @note 名前の次が '(' なら関数，そうでなければ大域変数。
///       大域変数には .bss 内のオフセットを与える。実アドレスはリンク時に
///       決まるので，参照側は再配置で解決される。
int dcont(int b) {
  int e; int fp; int skip;
  cty = pstars(b);
  // 「struct s { ... };」「enum { ... };」のように宣言子が無い形もある。
  // 型を登録するだけで，変数は作らない
  if (tok == o_semi) { next(); return 0; }
  // 1 つの宣言に宣言子を複数書ける (int a, *p;)。基底型 b から取り直す。
  // 関数定義は最初の宣言子でしか現れない (定義の後ろに ',' は来ない)
  while (1) {
    fp = 0;
    skip = 0;
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
      // 仮定義の重複 (C89 6.7.2)。ONE_SOURCE では同じ static 宣言が
      // ヘッダと本体の両方に現れる。実体は最初の仮定義が .bss に取って
      // あるので，2 つ目以降は読むだけにする
      if (gdef[e]) skip = 2;
      if (cext) skip = 1;
    } else {
      e = gnew();
      gkind[e] = 0;
      gna[e] = 0;
    }
    if (skip == 2) {
      // 初期化子が付いていればそれが本定義である。実体を .text 側へ
      // 置き直す。仮定義が取った .bss の隙間は残るが，参照はすべて
      // 記号を経由するので害は無い
      skipdims();
      if (tok == o_asn) {
        gtxt[e] = 1;
        ginit(e);
      }
    } else if (skip) {
      // 読むだけで何もしない (extern の重複宣言)
      skipdims();
    } else {
      if (!fp) cty = pdims(cty);
      if (isstru(cty) && ssize[cty - 2] == 0) exit(5);
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
    }
    if (tok != o_comma) break;
    next();
    cty = pstars(b);
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
///       ここでは ';' を食べない。宣言子の区切り (',') は呼び手が見る。
int skipdims() {
  if (tok == o_lb) {
    next();
    if (tok != o_rb) ccond();    // 値は使わない。構文と定数性の検査だけ行う
    if (tok != o_rb) exit(1);
    next();
  }
  return 0;
}

int typedef1() {
  int b;
  int i;
  int n;
  next();
  b = pstars(ptype());
  if (isfnp()) {
    b = fnpdec(b);
    copyn(tname, fpnam);
  } else if (tok != t_id) exit(1);
  if (tdfind() >= 0) exit(4);
  if (tdcnt > 2047) exit(6);
  i = tdcnt;
  tdcnt++;
  copyn(tdname + i * 64, tname);
  if (tok == t_id) next();
  // 関数型の typedef (typedef void *F(void *, int); の形。tcc の
  // TCCReallocFunc など)。型は関数型そのもの (ポインタ 0 段) にして
  // おき，使い手の「F *」が pstars でポインタにする。仮引数は関数
  // ポインタ (fnpdec) と同じく読み飛ばす
  if (tok == o_lp) {
    next();
    n = 1;
    while (n > 0) {
      if (tok == t_eof) exit(1);
      if (tok == o_lp) n = n + 1;
      if (tok == o_rp) n = n - 1;
      next();
    }
    b = ftype(b);
  }
  tdty[i] = b;
  if (tok != o_semi) exit(1);
  next();
  return 0;
}

int topdecl() {
  // 空の宣言 (';' だけ)。マクロが空に展開された残りに付く
  if (tok == o_semi) { next(); return 0; }
  cext = 0;
  cstat = 0;
  // 記憶域クラスは型指定子の前に置かれる
  while (tok == k_extern || tok == k_static || tok == k_register || tok == k_auto) {
    if (tok == k_extern) cext = 1;
    else if (tok == k_static) cstat = 1;
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
/// @brief 返却型を指定して組込みを登録する (64 bit を返す実行時支援用)。
/// @return 登録した大域記号の番号
/// @note **呼ぶときに初めて登録する。** getc / putc / exit のように最初から
///       登録すると，使わない翻訳単位にも未定義シンボルとして記号表に
///       出てしまい，実行時支援 (rt64.o) を並べないものがリンクできなく
///       なる (ld は終了コード 2)。加えて既存の .o のバイト列も変わる。
///       tname は記号表の鍵なので退避して戻す。
int biadd2(char *nm, int na, int ty) {
  char sv[64];
  int e;
  copyn(sv, tname);
  biadd(nm, na);
  copyn(tname, sv);
  e = gcnt - 1;
  gty[e] = ty;
  return e;
}

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
      n = stadd(gname + i * 64);
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
      n = stadd(gname + i * 64);
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
    if (pos >= 4194303) exit(6);
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
