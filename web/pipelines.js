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
        inputLabel: 'hex0 language (hex pairs + comments, terminated by ".")',
        steps: [{ name: 'hex0', bin: 'assets/bin/hex0.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
        note: 'hello.hex is the hand-encoded listing from Stage 0. The converted binary can be run on the spot.',
    },
    stage002: {
        sample: 'files/tests/stage002/labels.hex1',
        inputLabel: 'hex1 language (labels :name / references !name $name &name)',
        steps: [{ name: 'hex1', bin: 'assets/bin/hex1.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
        note: 'No more hand-computed branch offsets. The produced program prints A through E and exits.',
    },
    stage003: {
        sample: 'files/tests/stage003/run.s',
        inputLabel: 'asm language (RV32IM mnemonics + pseudo-instructions)',
        steps: [{ name: 'asm', bin: 'assets/bin/asm.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
    },
    stage004: {
        sample: 'files/tests/stage004/fib.sol',
        inputLabel: 'sol language (stack-oriented, postfix, terminated by ".")',
        steps: [{ name: 'sol', bin: 'assets/bin/sol.bin', src: 'user', term: 'dot' }],
        output: 'bin', run: true,
    },
    stage005: {
        sample: 'files/tests/stage005/upper.sc',
        inputLabel: 'sc language (a C subset; the EOT terminator is appended automatically)',
        steps: [{ name: 'sc', bin: 'assets/bin/sc.bin', src: 'user', term: 'eot' }],
        output: 'bin', run: true, stdin: 'hello World 123.',
        note: 'upper.sc is a filter that uppercases its input. "Runtime input" is fed to the produced program.',
    },
    stage006: {
        sample: 'files/tests/stage005/fib.sc',
        inputLabel: 'sc language (same as Stage 5 — but the compiler is now written in sc itself)',
        steps: [{ name: 'scc', bin: 'assets/bin/scc.bin', src: 'user', term: 'eot' }],
        output: 'bin', run: true,
    },
    stage007: {
        sample: 'files/tests/stage005/fib.sc',
        inputLabel: 'sc language (same language; the internals became IR + optimizations)',
        steps: [{ name: 'occ', bin: 'assets/bin/occ.bin', src: 'user', term: 'eot' }],
        output: 'bin', run: true,
        note: 'Try compiling the same fib.sc on Stage 6 and compare output size and instruction count.',
    },
    stage008: {
        sample: 'files/tests/stage005/fib.sc',
        inputLabel: 'sc language (output is now an ELF object, linked by ld)',
        steps: [
            { name: 'cc8', bin: 'assets/bin/cc8.bin', src: 'user', term: 'eot' },
            { name: 'ld', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul' },
        ],
        output: 'bin', run: true,
        note: 'Two steps: compile, then link. You can peek at the intermediate ELF object too.',
    },
    stage009: {
        sample: 'files/tests/stage009/src/macro.c',
        inputLabel: 'C source (#define / #if / #include available)',
        steps: [{
            name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
            members: [{ user: true, name: 'main.c' }],
        }],
        output: 'text',
        note: 'The output is preprocessed text — you see exactly how macros expanded.',
    },
    stage010: {
        sample: 'files/tests/stage010/src/feat.c',
        inputLabel: 'C89 (// comments only; /* */ is the preprocessor\'s job)',
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
        note: 'Headers are bundled into pp, and libc objects are linked in (same procedure as tests/stage011).',
    },
    stage012: null,     // OS (ELF + sfs + M/U モード)。フィルタ型ではないため対象外
    stage013: null,
    stage014: {
        sample: 'files/tests/stage014/probe/multidecl.c',
        inputLabel: 'Real-world C idioms (try the conformance probes as-is)',
        steps: [
            {
                name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                members: [{ user: true, name: 'probe.c' }],
            },
            { name: 'cc14b', bin: 'assets/bin/cc14b.bin', src: 'prev', term: null },
            { name: 'ld', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul' },
        ],
        output: 'bin', run: true,
        note: 'Multiple declarators like `int a, b;` now pass with the cc14 line (cc.bin = cc10l rejects them).',
    },
};
