// 各 Stage のプレイグラウンド定義。
// steps は web/worker.js の composeInput が解釈する。
//   bin:   実行するチェーン成果物 (assets/bin/)
//   src:   'user' (入力テキスト) | 'bundle' (pp への束ね) | 'prev' (前段の出力)
//   term:  'dot' | 'eot' | 'nul' (終端の規約。docs/dev-notes.md 4 章)
// sample: 初期表示するソース (files/)
// output: 'bin' (実行可能なフラットバイナリ) | 'text'
//
// パイプラインは tools/build.sh・tests/stage*/test.sh と同じ手順の再現である。

const LIBC11 = ['ctype.h', 'limits.h', 'stdarg.h', 'stddef.h',
    'stdlib.h', 'string.h'];

export const PIPELINES = {
    stage001: {
        sample: 'files/tests/stage001/hello.hex',
        inputLabel: 'hex0 言語 (hex ペア + コメント，終端 ".")',
        steps: [{ name: 'hex0', bin: 'assets/bin/hex0.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
        note: 'hello.hex は Stage 0 の手エンコード listing。変換されたバイナリはその場で実行できる',
    },
    stage002: {
        sample: 'files/tests/stage002/labels.hex1',
        inputLabel: 'hex1 言語 (ラベル :name / 参照 !name $name &name)',
        steps: [{ name: 'hex1', bin: 'assets/bin/hex1.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
        note: '分岐オフセットの手計算が消えた。出力プログラムは A〜E を出力して終了する',
    },
    stage003: {
        sample: 'files/tests/stage003/run.s',
        inputLabel: 'asm 言語 (RV32IM ニーモニック + 疑似命令)',
        steps: [{ name: 'asm', bin: 'assets/bin/asm.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
    },
    stage004: {
        sample: 'files/tests/stage004/fib.sol',
        inputLabel: 'sol 言語 (スタック指向・後置記法，終端 ".")',
        steps: [{ name: 'sol', bin: 'assets/bin/sol.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
    },
    stage005: {
        sample: 'files/tests/stage005/upper.sc',
        inputLabel: 'sc 言語 (C サブセット，終端 EOT は自動付加)',
        steps: [{ name: 'sc', bin: 'assets/bin/sc.bin', src: 'user', term: 'eot' }],
        output: 'bin', run: true, stdin: 'hello World 123.',
        note: 'upper.sc は入力を大文字化するフィルタ。「実行時入力」がそのプログラムへ渡る',
    },
    stage006: {
        sample: 'files/tests/stage005/fib.sc',
        inputLabel: 'sc 言語 (Stage 5 と同一。コンパイラが sc 自身で書かれた)',
        steps: [{ name: 'scc', bin: 'assets/bin/scc.bin', src: 'user', term: 'eot' }],
        output: 'bin', run: true,
    },
    stage007: {
        sample: 'files/tests/stage005/fib.sc',
        inputLabel: 'sc 言語 (同一言語。内部が IR + 最適化に変わった)',
        steps: [{ name: 'occ', bin: 'assets/bin/occ.bin', src: 'user', term: 'eot' }],
        output: 'bin', run: true,
        note: '同じ fib.sc を Stage 6 でもコンパイルして，出力サイズと実行命令数を比べてみるとよい',
    },
    stage008: {
        sample: 'files/tests/stage005/fib.sc',
        inputLabel: 'sc 言語 (出力が ELF オブジェクトになり，ld でリンクする)',
        steps: [
            { name: 'cc8', bin: 'assets/bin/cc8.bin', src: 'user', term: 'eot' },
            { name: 'ld', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul' },
        ],
        output: 'bin', run: true,
        note: 'コンパイル → リンクの 2 段。中間の ELF オブジェクトも覗ける',
    },
    stage009: {
        sample: 'files/tests/stage009/src/macro.c',
        inputLabel: 'C ソース (#define / #if / #include が使える)',
        steps: [{
            name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
            members: [{ user: true, name: 'main.c' }],
        }],
        output: 'text',
        note: '出力は前処理済みテキスト。マクロがどう展開されたかがそのまま見える',
    },
    stage010: {
        sample: 'files/tests/stage010/src/feat.c',
        inputLabel: 'C89 (// コメントのみ。/* */ は pp の役割)',
        steps: [
            { name: 'cc', bin: 'assets/bin/cc.bin', src: 'user', term: 'eot' },
            { name: 'ld', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul' },
        ],
        output: 'bin', run: true,
    },
    stage011: {
        sample: 'files/tests/stage011/src/word.c',
        inputLabel: 'C89 + libc (string.h / ctype.h / stdlib.h)',
        steps: [
            {
                name: 'bundle+pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                members: [
                    ...LIBC11.map((h) => ({
                        name: h, asset: `files/stage011/libc/include/${h}`,
                    })),
                    { user: true, name: 'main.c' },
                ],
            },
            { name: 'cc', bin: 'assets/bin/cc.bin', src: 'prev', term: null },
            {
                name: 'ld+libc', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul',
                extra: ['assets/bin/l11_string.o', 'assets/bin/l11_ctype.o',
                    'assets/bin/l11_stdlib.o'],
            },
        ],
        output: 'bin', run: true,
        note: 'ヘッダ一式を束ねて pp へ渡し，libc のオブジェクトをリンクする (tests/stage011 と同じ手順)',
    },
    stage012: null,     // OS (ELF + sfs + M/U モード)。フィルタ型ではないため対象外
    stage013: null,
    stage014: {
        sample: 'files/tests/stage014/probe/multidecl.c',
        inputLabel: '現実の C の書き方 (適合プローブをそのまま試せる)',
        steps: [
            {
                name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                members: [{ user: true, name: 'probe.c' }],
            },
            { name: 'cc14b', bin: 'assets/bin/cc14b.bin', src: 'prev', term: null },
            { name: 'ld', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul' },
        ],
        output: 'bin', run: true,
        note: '`int a, b;` のような複数宣言子は cc14 系で通るようになった (cc.bin = cc10l では拒まれる)',
    },
};
