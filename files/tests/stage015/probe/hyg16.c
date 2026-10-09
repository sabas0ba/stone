/* pp16 の再帰抑止 (docs/stage015-tcc.md 12.7) の検査。
 *
 * 自己参照マクロを関数形式マクロの実引数に渡すと，pp15 までは抑止が
 * 置換結果の再走査で失われ s1->s1->sec になっていた。gcc と同じ
 * s1->sec になることを見る。前処理結果そのものを検査するので，
 * このファイルはコンパイルしない (pp の出力を突き合わせる)。
 */
#define sec s1->sec
#define d4(s, x) w32(pad((s), 4), (x))

void f(void) { d4(sec, 0); sec = 1; }

/* 入れ子の実引数でも同じ (内側の展開へ印を持ち回る) */
#define outer(a) d4(a, 1)
void g(void) { outer(sec); }
