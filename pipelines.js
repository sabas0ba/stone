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
    stage012: null,     // OS。プレイグラウンドの代わりにターミナル (TERMINALS)
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

// OS 世代のターミナル。kernel + sfs で起動し，UART をそのまま端末へつなぐ。
// files: {name, asset} は成果物をそのまま，{name, build} はその場で
// コンパイルして sfs へ置く (どちらも本物のチェーン成果物で処理する)。
// samples は「入力欄へ先置きする一連のコマンド」(Enter は利用者が押す)。
const LIBC12 = ['ctype.h', 'errno.h', 'fcntl.h', 'limits.h', 'stdarg.h',
    'stddef.h', 'stdio.h', 'stdlib.h', 'string.h', 'unistd.h'];

export const TERMINALS = {
    stage012: {
        kernel: 'assets/bin/kernel.bin',
        mode: 'boot',           // 入力欄 = boot 行。Enter のたびに起動し直す
        imgSize: 4 << 20,
        note: 'No shell yet in this generation — the kernel boots exactly one ELF from the '
            + '`boot` line (program name + argv), runs it in U-mode, and reports its exit code. '
            + 'The demo program `hello` is compiled right here in your browser by pp → cc → ld12 '
            + 'before boot. The shell arrives in Stage 13.',
        files: [{
            name: 'hello',
            build: [
                {
                    name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                    members: [
                        ...LIBC12.map((h) => ({
                            name: h, asset: `files/stage012/libc/include/${h}`,
                        })),
                        { name: 'hello.c', asset: 'files/tests/stage012/user/hello.c' },
                    ],
                },
                { name: 'cc', bin: 'assets/bin/cc.bin', src: 'prev', term: null },
                { name: 'ld12', bin: 'assets/bin/ld12.bin', src: 'prev',
                    prefix: 'E', term: 'nul' },
            ],
        }],
        samples: [
            { cmd: 'hello', note: 'boot the ELF; it prints its argv and exits with code 3' },
            { cmd: 'hello stone os 2026', note: 'the boot line is split into argv' },
        ],
    },
    stage013: {
        kernel: 'assets/bin/kernel13.bin',
        mode: 'tty',            // 入力欄 = UART (シェルの標準入力)
        bootLine: 'sh\n',
        imgSize: 16 << 20,
        note: 'A real session on the homemade OS: the shell, the editor, and the compiler '
            + 'toolchain are all genuine chain artifacts running on the RV32 emulator. '
            + 'Follow the guided tour or type your own commands. `$` is the shell prompt; '
            + '`? N` reports a nonzero exit code.',
        files: [
            { name: 'sh', asset: 'assets/bin/sh13' },
            { name: 'ed', asset: 'assets/bin/ed13' },
            { name: 'bundle', asset: 'assets/bin/bundle13' },
            { name: 'eot', asset: 'assets/bin/eot13' },
            { name: 'ldin', asset: 'assets/bin/ldin13' },
            { name: 'mk', asset: 'assets/bin/mk13' },
            { name: 'pp', asset: 'assets/bin/pp13cmd' },
            { name: 'cc', asset: 'assets/bin/cc13cmd' },
            { name: 'ld', asset: 'assets/bin/ld13cmd' },
            { name: 'cc8.o', asset: 'assets/bin/cc8.o' },
            { name: 'hi.c', asset: 'files/tests/stage013/fixtures/hi.c' },
            { name: 'notes.txt', asset: 'files/tests/stage013/fixtures/notes.txt' },
            { name: 'mkfile', asset: 'files/tests/stage013/fixtures/mkfile' },
            { name: 'pp.sc', asset: 'files/stage009/pp.sc' },
            { name: 'cc12.sc', asset: 'files/stage010/cc12.sc' },
            { name: 'ld13.sc', asset: 'files/stage013/ld13.sc' },
        ],
        samples: [
            { cmd: 'echo hello, stone', note: 'a shell builtin' },
            { cmd: 'ed notes.txt', note: 'open the line editor — it reads the same UART' },
            { cmd: '1,$n', note: 'ed: numbered listing' },
            { cmd: '1s/one/ONE/g', note: 'ed: substitute (literal match, no regex)' },
            { cmd: '2a', note: 'ed: append after line 2 (finish with a lone .)' },
            { cmd: 'typed in your browser', note: 'the appended line' },
            { cmd: '.', note: 'end of append' },
            { cmd: '1,$n', note: 'see the result' },
            { cmd: 'w out.txt', note: 'write to a new file (prints the byte count)' },
            { cmd: 'q', note: 'quit — ed answers `?` once: the buffer still differs from notes.txt' },
            { cmd: 'q', note: 'q again confirms — the shell resumes reading the same input' },
            { cmd: 'bundle hi.c > hi.b', note: 'guest build: bundle the source' },
            { cmd: 'pp < hi.b > hi.i', note: 'preprocess' },
            { cmd: 'cc < hi.i > hi.o', note: 'compile with the real bootstrapped cc' },
            { cmd: 'ldin E hi.o > hi.ld', note: 'assemble the linker input (E = executable)' },
            { cmd: 'ld < hi.ld > hi', note: 'link' },
            { cmd: 'hi', note: 'run what you just built (exits with 7)' },
            { cmd: 'eot < cc12.sc > cc12.in', note: 'now rebuild the compiler itself' },
            { cmd: 'cc < cc12.in > cc12.o', note: 'cc compiles its own 151 KB source (takes a few seconds)' },
            { cmd: 'ldin F cc12.o > cc12.ld', note: 'linker input (F = flat binary)' },
            { cmd: 'ld < cc12.ld > cc10l.bin', note: 'byte-identical to the chain artifact — download it below after exit' },
            { cmd: 'mk -f mkfile all', note: 'optional: mk rebuilds pp / cc / ld from cc8.o (slow — about a minute)' },
            { cmd: 'exit 0', note: 'end the session and harvest the files' },
        ],
    },
};
