// 各 Stage のプレイグラウンド定義。
// steps は web/worker.js の composeInput が解釈する。
//   bin:   実行するチェーン成果物 (assets/bin/)
//   src:   'user' (入力テキスト) | 'bundle' (pp へのバンドル) | 'prev' (前段の出力)
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
        // 適合台帳 (tests/stage014/ledger.txt) の probe をそのまま選べる。
        // 手順は tests/stage014/test.sh の probe() と同じ (pp -> cc14g -> ld)
        // 見本の一覧は台帳 (coverage の kind:'ledger' の組) から作る。
        // pipelines.js に名前を複製しないので，台帳を修正すれば選択肢も更新される
        sampleDir: 'files/tests/stage014/probe',
        samplesFromLedger: true,
        inputLabel: 'Real-world C idioms — pick a conformance probe, or write your own',
        steps: [
            {
                name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                members: [
                    { name: 'stdarg.h', asset: 'files/stage013/libc/include/stdarg.h' },
                    { user: true, name: 'probe.c' },
                ],
            },
            { name: 'cc14g', bin: 'assets/bin/cc14g.bin', src: 'prev', term: null },
            { name: 'ld', bin: 'assets/bin/ld.bin', src: 'prev', term: 'nul' },
        ],
        output: 'bin', run: true,
        note: 'Every probe below is a line in the conformance ledger. The ledger records what '
            + 'the measurement **is**, not what we wish it were: `gap` means cc rejects the '
            + 'program on purpose, and you will see it exit nonzero right here.',
    },
    stage015: {
        // tests/stage015/test.sh の probe() と同じ手順。実行時支援
        // (rt64 / rtfp) を必ず並べる —— 64 bit の除算と浮動小数点の
        // 変換がこれを呼ぶ
        sampleDir: 'files/tests/stage015/probe',
        samplesFromLedger: true,
        inputLabel: 'C89 + long long + float / double (the Stage 15 conformance probes)',
        steps: [
            {
                name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                members: [
                    { name: 'stdarg.h', asset: 'files/stage015/libc/include/stdarg.h' },
                    { user: true, name: 'probe.c' },
                ],
            },
            { name: 'cc15p', bin: 'assets/bin/cc15p.bin', src: 'prev', term: null },
            {
                name: 'ld + rt64 + rtfp', bin: 'assets/bin/ld.bin', src: 'prev',
                term: 'nul',
                extra: ['assets/bin/rt64.o', 'assets/bin/rtfp.o'],
            },
        ],
        output: 'bin', run: true,
        note: 'Bare metal has no printf, so the probes print **raw IEEE-754 and 64-bit bit '
            + 'patterns** — that is the point: the ledger compares bits, not rounded text. '
            + 'For `printf("%f")` and `%llu`, boot the OS in the terminal below.',
    },
    stage016: null,     // OS。プレイグラウンドの代わりにターミナル (TERMINALS)
    stage017: null,     // OS の上のツール群。ターミナル (TERMINALS)
    stage018: null,     // nas1 は OS の上のコマンド。ターミナル (TERMINALS)
    // Stage 19 は Linux 5.4 のソース (約 1.2 GB) を入力にした測定であり，
    // 素材を repo に入れない決まり (docs/SOURCES.md) なのでブラウザでは走らせない
    stage019: {
        none: 'Stage 19 has no interactive demo. Its work is measurement against the Linux 5.4.302 '
            + 'tree (which stays outside the repository, see docs/SOURCES.md): which GNU C extensions '
            + 'the kernel needs from our compiler, what Kbuild invokes, and a Kconfig reader (`kconf1`). '
            + 'The compiler generation that answers those measurements is Stage 18 — see the terminal there.',
    },
};

// OS 世代のターミナル。kernel + sfs で起動し，UART をそのまま端末へつなぐ。
// files: {name, asset} は成果物をそのまま，{name, build} はその場で
// コンパイルして sfs へ置く (どちらもビルドチェーンの成果物で処理する)。
// samples は「入力欄へ事前に設定する一連のコマンド」(Enter は利用者が押す)。
const LIBC12 = ['ctype.h', 'errno.h', 'fcntl.h', 'limits.h', 'stdarg.h',
    'stddef.h', 'stdio.h', 'stdlib.h', 'string.h', 'unistd.h'];

// libc15 / libc16 のヘッダ (tools/bundle.sh へ *.h で渡すものと同じ並び)
const LIBC15 = ['assert.h', 'ctype.h', 'errno.h', 'fcntl.h', 'inttypes.h',
    'limits.h', 'math.h', 'setjmp.h', 'stdarg.h', 'stddef.h', 'stdio.h',
    'stdlib.h', 'string.h', 'time.h', 'unistd.h'];
const LIBC16 = ['assert.h', 'ctype.h', 'dirent.h', 'errno.h', 'fcntl.h',
    'inttypes.h', 'limits.h', 'math.h', 'setjmp.h', 'stdarg.h', 'stddef.h',
    'stdio.h', 'stdlib.h', 'string.h', 'time.h', 'unistd.h'];

// libc15 / libc16 の実体 (リンクの並びは tests/stage015・016 と同一)。
// **経路は文字列そのままで書く。** web/build-site.sh は pipelines.js を
// 正規表現で走査して資産を集めるので，テンプレート文字列にすると
// 参照を検出できない
const L15_OBJS = [
    'assets/bin/l15_src_string.o', 'assets/bin/l15_src_stdlib.o',
    'assets/bin/l15_src_misc15.o', 'assets/bin/l15_posix_sys.o',
    'assets/bin/l15_posix_morecore.o', 'assets/bin/l15_posix_stdio.o',
    'assets/bin/l15_posix_assert.o',
    'assets/bin/rt64.o', 'assets/bin/rtfp.o',
];
const L16_OBJS = [
    'assets/bin/l16_src_string.o', 'assets/bin/l16_src_stdlib.o',
    'assets/bin/l16_src_misc15.o', 'assets/bin/l16_posix_sys.o',
    'assets/bin/l16_posix_morecore.o', 'assets/bin/l16_posix_stdio.o',
    'assets/bin/l16_posix_assert.o', 'assets/bin/l16_posix_dir.o',
    'assets/bin/rt64.o', 'assets/bin/rtfp.o',
];

// 「ヘッダをバンドルして pp16 -> cc15p -> ld16 ('E') で実行形式にする」手順。
// tests/stage015・016 が sh で書いているものと同じ並びである
const guestBuild = (headers, incDir, unit, objs, opts = {}) => [
    {
        name: 'pp16', bin: 'assets/bin/pp16.bin', src: 'bundle',
        members: [
            ...headers.map((h) => ({ name: h, asset: `${incDir}/${h}` })),
            ...(opts.extraHeaders || []),
            { name: unit.name, asset: unit.asset },
        ],
    },
    { name: 'cc15p', bin: 'assets/bin/cc15p.bin', src: 'prev', term: null },
    {
        name: 'ld16', bin: 'assets/bin/ld16.bin',
        src: 'prev', prefix: 'E', term: 'nul', extra: objs,
    },
];

// ---- Stage 17 / 18 の素材 ----
// libc18 のヘッダ (tools/bundle.sh へ *.h で渡すものと同じ並び)。sys/ は別
const LIBC18 = ['assert.h', 'ctype.h', 'dirent.h', 'errno.h', 'fcntl.h',
    'inttypes.h', 'limits.h', 'math.h', 'setjmp.h', 'stdarg.h', 'stddef.h',
    'stdio.h', 'stdlib.h', 'string.h', 'time.h', 'unistd.h'];
// libc26 のヘッダ。stage018 の nas1 と tests/stage017 第 12 部が bundle する並び
const LIBC26 = ['alloca.h', 'assert.h', 'ctype.h', 'dirent.h', 'errno.h',
    'fcntl.h', 'float.h', 'inttypes.h', 'limits.h', 'math.h', 'setjmp.h',
    'signal.h', 'stdarg.h', 'stddef.h', 'stdio.h', 'stdlib.h', 'string.h',
    'time.h', 'unistd.h'];
// /lib の 1 揃い (tests/stage017 の mkroot と同じ並び。順番を変えると
// ドライバが組んだものだけ挙動が変わる。docs/stage017-cc.md 3.2)
const L18_LIB = [
    'assets/bin/l18_src_string.o', 'assets/bin/l18_src_stdlib.o',
    'assets/bin/l18_src_misc15.o', 'assets/bin/l18_posix_sys.o',
    'assets/bin/l18_posix_morecore.o', 'assets/bin/l18_posix_stdio.o',
    'assets/bin/l18_posix_assert.o', 'assets/bin/l18_posix_dir.o',
    'assets/bin/rt64.o', 'assets/bin/rtfp.o',
];
// libc26 の部品 (tools/ld18.sh -L tmp/build/l26_*.o rt64.o rtfp.o の並び)
const L26_LIB = [
    'assets/bin/l26_posix_assert.o', 'assets/bin/l26_posix_dir.o',
    'assets/bin/l26_posix_morecore.o', 'assets/bin/l26_posix_signal.o',
    'assets/bin/l26_posix_stdio.o', 'assets/bin/l26_posix_sys.o',
    'assets/bin/l26_posix_sys26.o', 'assets/bin/l26_src_ctype.o',
    'assets/bin/l26_src_misc15.o', 'assets/bin/l26_src_misc26.o',
    'assets/bin/l26_src_morecore.o', 'assets/bin/l26_src_stdlib.o',
    'assets/bin/l26_src_string.o', 'assets/bin/rt64.o', 'assets/bin/rtfp.o',
];
// cc ドライバが呼ぶ 3 段 (/bin)。cc19 はリンカの経路を "/bin/ld16" と
// 埋め込んでいるので名前は世代に関わらず固定である
const TOOLBIN17 = [
    { name: 'bin/pp16', asset: 'assets/bin/pp16cmd' },
    { name: 'bin/cc15p', asset: 'assets/bin/cc15pcmd' },
    { name: 'bin/ld16', asset: 'assets/bin/ld16cmd' },
    ...LIBC18.map((h) => ({ name: `include/${h}`, asset: `files/stage016/libc18/include/${h}` })),
    { name: 'include/sys/stat.h', asset: 'files/stage016/libc18/include/sys/stat.h' },
    { name: 'include/sys/time.h', asset: 'files/stage016/libc18/include/sys/time.h' },
];
// tests/stage017 の mkroot: /bin の 3 段，/include，/lib の 1 揃い，ツール
const ROOT17 = [
    ...TOOLBIN17,
    ...L18_LIB.map((a) => ({ name: `lib/${a.replace(/^.*\//, '')}`, asset: a })),
    { name: 'cc', asset: 'assets/bin/cc17' },
    { name: 'cc18', asset: 'assets/bin/cc18' },
    { name: 'ar', asset: 'assets/bin/ar17' },
    { name: 'mk', asset: 'assets/bin/mk17' },
    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
];
// libc18 のソース (OS の上で libc 自身を書庫にする節が使う)
const LIBC18_SRC = [
    ...['string', 'ctype', 'stdlib', 'morecore', 'misc15'].map((f) =>
        ({ name: `src/${f}.c`, asset: `files/stage016/libc18/src/${f}.c` })),
    ...['sys', 'morecore', 'stdio', 'assert', 'dir'].map((f) =>
        ({ name: `posix/${f}.c`, asset: `files/stage016/libc18/posix/${f}.c` })),
];
// 最前線 (cc15as の遠距離呼出し + ld18 + libc26) で OS の実行形式を組む。
// tests/stage017 第 12 部・tools/build/stage018.sh と同じ手順
const frontlineBuild = (name, asset) => [
    {
        name: 'pp16', bin: 'assets/bin/pp16.bin', src: 'bundle',
        members: [
            ...LIBC26.map((h) => ({ name: h, asset: `files/stage017/libc26/include/${h}` })),
            { name: 'sys/time.h', asset: 'files/stage017/libc26/include/sys/time.h' },
            { name: 'sys/times.h', asset: 'files/stage017/libc26/include/sys/times.h' },
            { name: 'sys/stat.h', asset: 'files/stage017/libc26/include/sys/stat.h' },
            { name: 'sys/types.h', asset: 'files/stage017/libc26/include/sys/types.h' },
            { name, asset },
        ],
    },
    { name: 'cc15as', bin: 'assets/bin/cc15as.bin', src: 'prev', term: null,
        prefix: '#pragma stone far_call\n' },
    { name: 'ld18', bin: 'assets/bin/ld18.bin', kind: 'ld18', lib: L26_LIB },
];
const seq = (c, n) => Array.from({ length: n }, (_, i) => `${c}${i + 1}`).join(' ');
const ARGS_C = '#include <stdio.h>\nint main(int argc, char **argv) {\n'
    + '  printf("argc %d last %s\\n", argc, argv[argc - 1]);\n  return 0;\n}\n';
// GCC 4.7.4 の木の最長 (92 バイト) と深さ 12 の経路 (tests/stage017 第 5 部)
const G92 = 'g'.repeat(92);
const G92B = 'h'.repeat(92);
const DEEP = 'aaaaaaaaaa/bbbbbbbbbb/cccccccccc/dddddddddd/eeeeeeeeee/ffffffffff/'
    + 'gggggggggg/hhhhhhhhhh/iiiiiiiiii/jjjjjjjjjj/kkkkkkkkkk/target.txt';
const HELLO_S = '    .text\n    .globl main\nmain:\n    la a0, msg\n    call puts\n'
    + '    li a0, 0\n    ret\n\n    .section .rodata.str1.1,"aMS",@progbits,1\nmsg:\n'
    + '    .asciz "hello"\n';

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
            { cmd: 'cc < hi.i > hi.o', note: 'compile with cc built by the bootstrap chain' },
            { cmd: 'ldin E hi.o > hi.ld', note: 'assemble the linker input (E = executable)' },
            { cmd: 'ld < hi.ld > hi', note: 'link' },
            { cmd: 'hi', note: 'run what you just built (exits with 7)' },
            { cmd: 'eot < cc12.sc > cc12.in', note: 'now rebuild the compiler itself' },
            { cmd: 'cc < cc12.in > cc12.o', note: 'cc compiles its own 151 KB source (takes a few seconds)' },
            { cmd: 'ldin F cc12.o > cc12.ld', note: 'linker input (F = flat binary)' },
            { cmd: 'ld < cc12.ld > cc10l.bin', note: 'byte-identical to the chain artifact — download it below after exit' },
            { cmd: 'mk -f mkfile all', note: 'optional: mk rebuilds pp / cc / ld from cc8.o (slow — about a minute)' },
            { cmd: 'exit 0', note: 'end the session and list the files for download' },
        ],
    },
    // Stage 15: 64 bit と浮動小数点が **libc の上で** 動作するところを見せる。
    // lib15 は tests/stage015 が kernel15 / kernel16 の両方で走らせる
    // 検査そのもの (期待値は tests/stage015/expected/lib15.txt)
    stage015: {
        kernel: 'assets/bin/kernel16.bin',
        mode: 'boot',
        imgSize: 4 << 20,
        note: 'The playground above prints bit patterns because bare metal has no printf. '
            + 'Here the same numbers go through **libc15 on the OS**: `%llu` on a 64-bit '
            + 'value, `%f` on a double, `snprintf`, `strtod`, `sscanf`, `setjmp` / `longjmp` '
            + 'and `lseek`. The program is compiled in your browser by pp → cc15p → ld14 and '
            + 'linked against the real libc15 objects, then booted on kernel16 — the '
            + 'generation that loads every PT_LOAD segment, which is what tcc\'s own '
            + 'executables need.',
        files: [{
            name: 'lib15',
            build: [
                {
                    name: 'pp', bin: 'assets/bin/pp.bin', src: 'bundle',
                    members: [
                        ...LIBC15.map((h) => ({
                            name: h, asset: `files/stage015/libc/include/${h}`,
                        })),
                        { name: 'lib15.c', asset: 'files/tests/stage015/user/lib15.c' },
                    ],
                },
                { name: 'cc15p', bin: 'assets/bin/cc15p.bin', src: 'prev', term: null },
                {
                    name: 'ld14', bin: 'assets/bin/ld14.bin', src: 'prev',
                    prefix: 'E', term: 'nul', extra: L15_OBJS,
                },
            ],
        }],
        samples: [
            { cmd: 'lib15', note: 'run it — the line it prints is checked byte for byte by tests/stage015' },
        ],
    },
    // Stage 16: 3 つの世代を並べて見せる。**片方だけを見ても
    // 「広がった」ことは言えない** (docs/stage016-os.md 8.6) ので，
    // 記憶域は kernel18 と kernel19 の両方を置く
    stage016: {
        mode: 'boot',
        fs: 2,                  // sfs2 (ディレクトリを持つ)
        note: 'Compare path resolution, directory operations, and memory allocation. Each program is the probe '
            + 'that tests/stage016 runs under QEMU; here it is compiled in your browser by '
            + 'pp16 → cc15p → ld16, packed into an **sfs2** image, and booted. Every line is '
            + '`label expected actual` — so a mismatch is visible without a diff.',
        scenarios: [
            {
                id: 'path',
                label: 'kernel17 · paths',
                kernel: 'assets/bin/kernel17.bin',
                imgSize: 4 << 20,
                maxEntries: 128,
                blurb: 'sfs2 gives entries a **kind** and a **parent**, so a path is walked one '
                    + 'component at a time. Note `/src/one.c` and `/inc/one.c`: the same name in '
                    + 'two directories. sfs2 resolves each path using parent-directory entries.',
                files: [
                    { name: 'top.txt', text: 'TOP\n' },
                    { name: 'src/one.c', text: 'SRC-ONE\n' },
                    { name: 'inc/one.c', text: 'INC-ONE\n' },
                    { name: 'src/a/b/three.c', text: 'THREE\n' },
                    {
                        name: 'pathprobe',
                        build: guestBuild(LIBC15, 'files/stage015/libc/include',
                            { name: 'pathprobe.c',
                                asset: 'files/tests/stage016/user/pathprobe.c' },
                            L15_OBJS),
                    },
                ],
                samples: [{ cmd: 'pathprobe', note: 'absolute, relative, deep, duplicate, missing, and through-a-file' }],
            },
            {
                id: 'dir',
                label: 'kernel18 · dirs',
                kernel: 'assets/bin/kernel18.bin',
                imgSize: 4 << 20,
                maxEntries: 128,
                blurb: 'Listing, creating and moving. It must be linked against **libc16**, not '
                    + 'libc15: libc15\'s `open` strips leading slashes, which was correct while '
                    + 'only the root directory existed, but misinterprets absolute paths after chdir. See '
                    + 'the tree below the terminal — `out/` and `out/f.txt` are made by the program.',
                files: [
                    { name: 'top.txt', text: 'TOP\n' },
                    { name: 'src/one.c', text: 'SRC-ONE\n' },
                    { name: 'src/a/two.c', text: 'A-TWO\n' },
                    { name: 'inc/one.c', text: 'INC-ONE\n' },
                    {
                        name: 'dirprobe',
                        build: guestBuild(LIBC16, 'files/stage016/libc/include',
                            { name: 'dirprobe.c',
                                asset: 'files/tests/stage016/user/dirprobe.c' },
                            L16_OBJS, {
                                extraHeaders: [{
                                    name: 'sys/stat.h',
                                    asset: 'files/stage016/libc/include/sys/stat.h',
                                }],
                            }),
                    },
                ],
                samples: [{ cmd: 'dirprobe', note: 'getdents64 / mkdirat / chdir / getcwd, and . / ..' }],
            },
            {
                id: 'mem',
                label: 'kernel19 · 256 MB',
                kernel: 'assets/bin/kernel19.bin',
                imgSize: 4 << 20,
                maxEntries: 32,
                ramSize: 512 << 20,
                heavy: true,
                blurb: 'The probe takes memory a megabyte at a time, writes a mark at the first '
                    + 'and last byte of each, and reads them all back — because "the allocation '
                    + 'succeeds and the write faults" is the failure worth catching. Expect '
                    + '**255**. The same program on kernel18 answers 13. This one asks your '
                    + 'browser for a 512 MB emulator, so it is the slow one to start.',
                files: [{
                    name: 'memprobe',
                    build: guestBuild(LIBC16, 'files/stage016/libc/include',
                        { name: 'memprobe.c',
                            asset: 'files/tests/stage016/user/memprobe.c' },
                        L16_OBJS),
                }],
                samples: [{ cmd: 'memprobe', note: 'got <MiB> then verify ok — 255 on kernel19' }],
            },
            {
                id: 'mem18',
                label: 'kernel18 · 13 MiB',
                kernel: 'assets/bin/kernel18.bin',
                imgSize: 4 << 20,
                maxEntries: 32,
                blurb: 'The identical binary on the previous generation. One number on its own '
                    + 'proves nothing, which is why the check in tests/stage016 always runs both.',
                files: [{
                    name: 'memprobe',
                    build: guestBuild(LIBC16, 'files/stage016/libc/include',
                        { name: 'memprobe.c',
                            asset: 'files/tests/stage016/user/memprobe.c' },
                        L16_OBJS),
                }],
                samples: [{ cmd: 'memprobe', note: 'got 13 — UBRKMAX - UBASE = 14 MB, less the image and stack' }],
            },
        ],
    },
    // ---- Stage 17: stone の OS の上のツール群 ----
    // 各シナリオは tests/stage017/test.sh の節と同じ根・同じ台本である。
    // sh2 は標準入力を EOF まで読んでから実行するので，対話ではなく
    // **台本 (go.sh) を編集して `sh2 go.sh` で起動する** (mode: 'script')。
    // 世代ごとに sfs の形式と配置が違うので，fs / sfsOffset / ramSize は
    // シナリオが持つ (kernel23: sfs2 @64 MiB / kernel24: sfs3 @64 MiB /
    // kernel25〜28: sfs4 @512 MiB・RAM 1 GiB / kernel29: sfs4 @1.5 GiB・RAM 2 GiB)
    stage017: {
        mode: 'script',
        bootLine: 'sh2 go.sh\n',
        note: 'Each scenario boots a root filesystem assembled from real chain artifacts and runs '
            + 'the script on the left with `sh2 go.sh` — the same way tests/stage017 drives QEMU. '
            + 'Edit the script freely. The guided tour on the right holds the scripts of the '
            + 'test sections; the expected output is in tests/stage017/expected.',
        scenarios: [
            {
                id: 'cc',
                label: 'kernel23 · cc',
                kernel: 'assets/bin/kernel23.bin',
                fs: 2, imgSize: 16 << 20, maxEntries: 256, ramSize: 512 << 20,
                blurb: '`cc` is a driver that runs pp16 → cc15p → ld16 as commands on the OS '
                    + '(the three live in `/bin`, the libc objects in `/lib`). `selfhost.c` goes '
                    + 'through structs, recursion, qsort, sprintf and malloc — every one of them '
                    + 'resolved from `/lib`. kernel23 raised the argv limit from 8 to 64 words and '
                    + 'fails with E2BIG instead of silently dropping the rest.',
                files: [
                    ...ROOT17,
                    { name: 'hello.c', asset: 'files/tests/stage017/user/hello.c' },
                    { name: 'selfhost.c', asset: 'files/tests/stage017/user/selfhost.c' },
                    { name: 'args.c', text: ARGS_C },
                ],
                samples: [
                    { cmd: 'basic', note: 'compile and run hello.c and selfhost.c (tests/stage017/expected/basic.txt)',
                        script: 'cc -o hello hello.c\necho "hello-build $?"\nhello\necho "hello-run $?"\n'
                            + 'cc -o selfhost selfhost.c\necho "selfhost-build $?"\nselfhost\necho "selfhost-run $?"\n' },
                    { cmd: 'args', note: '32 words arrive intact; 96 words fail with E2BIG instead of being truncated',
                        script: 'cc18 -o args args.c\necho "build $?"\n'
                            + `args ${seq('a', 31)}\necho "many $?"\n`
                            + `args ${seq('b', 95)}\necho "toomany $?"\n` },
                ],
            },
            {
                id: 'ar',
                label: 'kernel23 · ar',
                kernel: 'assets/bin/kernel23.bin',
                fs: 2, imgSize: 16 << 20, maxEntries: 256, ramSize: 512 << 20,
                blurb: 'Separate compilation on the OS: `cc18 -c` makes objects, `ar rcs` packs '
                    + 'them into an archive (plain ar format, no symbol index — host `ar t` lists '
                    + 'it), and the final link pulls `addx` / `catx` from the archive and `printf` '
                    + 'from `/lib`. `-I inc` finds the headers, `-DVERSION=7` comes from the command line.',
                files: [
                    ...ROOT17,
                    { name: 'multi.c', asset: 'files/tests/stage017/user/multi.c' },
                    { name: 'mathx.c', asset: 'files/tests/stage017/user/mathx.c' },
                    { name: 'strx.c', asset: 'files/tests/stage017/user/strx.c' },
                    { name: 'inc/mathx.h', asset: 'files/tests/stage017/user/inc/mathx.h' },
                    { name: 'inc/strx.h', asset: 'files/tests/stage017/user/inc/strx.h' },
                ],
                samples: [
                    { cmd: 'multi', note: 'two units → libx.a → link → run (tests/stage017/expected/multi.txt)',
                        script: 'cc18 -c -I inc mathx.c -o mathx.o\necho "compile-mathx $?"\n'
                            + 'cc18 -c -I inc strx.c -o strx.o\necho "compile-strx $?"\n'
                            + 'ar rcs libx.a mathx.o strx.o\necho "ar-rcs $?"\nar t libx.a\n'
                            + 'cc18 -I inc -DVERSION=7 -o multi multi.c libx.a\necho "link $?"\nmulti\necho "run $?"\n' },
                ],
            },
            {
                id: 'mk',
                label: 'kernel23 · mk',
                kernel: 'assets/bin/kernel23.bin',
                fs: 2, imgSize: 32 << 20, maxEntries: 512, ramSize: 512 << 20,
                blurb: 'The OS builds **its own libc** from a Makefile: eight sources become '
                    + 'objects through one pattern rule, `ar` packs `libc.a`, and `uselibc` links '
                    + 'against the archive alone — `/lib` holds only rt64 / rtfp here, so a '
                    + 'successful link proves the symbols came from the archive. `mk19` adds '
                    + 'functions (`patsubst`, `filter`, `foreach`, `call`, …); `fn.mk` derives the '
                    + 'object list instead of spelling it out.',
                files: [
                    ...TOOLBIN17,
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'lib/rt64.o', asset: 'assets/bin/rt64.o' },
                    { name: 'lib/rtfp.o', asset: 'assets/bin/rtfp.o' },
                    ...LIBC18_SRC,
                    { name: 'cc18', asset: 'assets/bin/cc18' },
                    { name: 'ar', asset: 'assets/bin/ar17' },
                    { name: 'mk', asset: 'assets/bin/mk17' },
                    { name: 'mk19', asset: 'assets/bin/mk19' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'uselibc.c', asset: 'files/tests/stage017/user/uselibc.c' },
                    { name: 'Makefile', asset: 'files/tests/stage017/mk/libc.mk' },
                    { name: 'fn.mk', asset: 'files/tests/stage017/mk/fn.mk' },
                ],
                samples: [
                    { cmd: 'mk', note: 'mk17 runs libc.mk: 8 compiles, ar, link, run (expected/mk.txt)',
                        script: 'mk\necho "mk $?"\nuselibc\necho "run $?"\n' },
                    { cmd: 'mk19 -f fn.mk', note: 'mk19: functions derive the object list (expected/fn.txt)',
                        script: 'mk19 -f fn.mk show\nmk19 -f fn.mk\necho "mk $?"\nuselibc\necho "run $?"\n' },
                ],
            },
            {
                id: 'mtime',
                label: 'kernel24 · sfs3 time',
                kernel: 'assets/bin/kernel24.bin',
                fs: 3, imgSize: 4 << 20, maxEntries: 128, ramSize: 512 << 20,
                blurb: 'sfs3 entries carry a modification time in nanoseconds; the kernel reads the '
                    + 'goldfish RTC (here: your browser clock) and stamps every write. `stamp` prints '
                    + 'what the kernel sees — the times packed into the image are deliberately in the '
                    + 'past (2017 / 2020 / 2017) so you can tell them from fresh ones. `mk18` turns '
                    + 'that into incremental builds: the second run does nothing, and after '
                    + '`echo C > b.src` only the part that depends on b.src is remade.',
                files: [
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'stamp', asset: 'assets/bin/stamp' },
                    { name: 'mk', asset: 'assets/bin/mk18' },
                    { name: 'a.src', text: 'A\n', mtime: 1500000000000000000n },
                    { name: 'b.src', text: 'B\n', mtime: 1500000000000000000n },
                    { name: 'Makefile', text: 'all: prog\n\n%.cp: %.src\n\tcat $< > $@\n\nprog: a.cp b.cp\n\tcat $^ > $@\n',
                        mtime: 1500000000000000000n },
                    { name: 'a.txt', text: 'hello\n', mtime: 1700000000123456789n },
                    { name: 'd/b.txt', text: 'worldly\n', mtime: 1600000123000000001n },
                    { name: 'd', dir: true, mtime: 1400000000500000000n },
                ],
                samples: [
                    { cmd: 'stamp', note: 'type, length, mtime (high word, low word) — nanoseconds straight from the table',
                        script: 'stamp a.txt d/b.txt d\necho "stamp $?"\necho made > new.txt\nstamp new.txt\nstamp nosuch\necho "missing $?"\n' },
                    { cmd: 'mk (incremental)', note: 'tests/stage017/expected/inc.txt: nothing on the 2nd run, b-side only on the 3rd, everything with -B',
                        script: 'echo "--1--"\nmk\necho "--2--"\nmk\necho "--3--"\necho C > b.src\nmk\necho "--4--"\nmk -B\necho "--end--"\nstamp a.src b.src a.cp b.cp prog\n' },
                ],
            },
            {
                id: 'names',
                label: 'kernel25 · sfs4 names',
                kernel: 'assets/bin/kernel25.bin',
                fs: 4, sfsOffset: 512 << 20, imgSize: 4 << 20, maxEntries: 128, ramSize: 1 << 30,
                heavy: true,
                blurb: 'GCC 4.7.4\'s tree has a 92-byte file name and paths 131 bytes long, 12 '
                    + 'levels deep. sfs3 refuses the name (47-byte limit); sfs4 widens the entry to '
                    + '128 bytes and kernel25 moves the image above the user area (a 512 MiB window at '
                    + '0xa000_0000, so the emulator needs 1 GiB). The path buffer grew to 255 bytes — '
                    + 'without that, a 92-byte name can be `cat` but not `stat`.',
                files: [
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'stamp', asset: 'assets/bin/stamp' },
                    { name: G92, text: 'ninety-two\n', mtime: 1700000001222222222n },
                    { name: DEEP, text: 'deep file\n', mtime: 1700000002333333333n },
                ],
                samples: [
                    { cmd: 'long names', note: 'read, copy, create and stat a 92-byte name and a 131-byte path',
                        script: `cat ${G92}\necho "cat $?"\ncp ${G92} ${G92B}\necho "cp $?"\ncat ${G92B}\n`
                            + `stamp ${G92}\necho "name-stat $?"\nstamp ${DEEP}\necho "deep-stat $?"\n` },
                ],
            },
            {
                id: 'times',
                label: 'kernel27 · times',
                kernel: 'assets/bin/kernel27.bin',
                fs: 4, sfsOffset: 512 << 20, imgSize: 4 << 20, maxEntries: 128, ramSize: 1 << 30,
                heavy: true,
                blurb: '`times` (153) with per-process accounting — the kernel reads the RTC on '
                    + 'every trap entry and exit and charges the difference to the current depth, so '
                    + 'a spawned child\'s CPU time lands in the parent\'s `cutime`. `tmx` burns 3 '
                    + 'ticks in a child and checks that; it also checks `sysconf(_SC_CLK_TCK)`, '
                    + '`pathconf`, and that a 255-byte path passes `stat` (kernel26 lost one byte).',
                files: [
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'tmx', asset: 'assets/bin/tmx' },
                ],
                samples: [
                    { cmd: 'tmx', note: 'expect: clk 100 / spawn 0 / cut y / path-max 256 256 / p255 0 0 / p256 -1 7. '
                        + 'own-small may read n here: the emulator is about ten times slower than QEMU, so the spawn path alone can exceed the child\'s 3 ticks',
                        script: 'tmx\necho "rc $?"\n' },
                ],
            },
            {
                id: 'libc26',
                label: 'kernel28 · libc26 + ld18',
                kernel: 'assets/bin/kernel28.bin',
                fs: 4, sfsOffset: 512 << 20, imgSize: 4 << 20, maxEntries: 128, ramSize: 1 << 30,
                heavy: true,
                blurb: 'The frontline toolchain for GCC\'s cc1: `l26x.c` is compiled here by '
                    + 'pp16 → **cc15as** (with `#pragma stone far_call`, the long-call code model) '
                    + 'and linked by **ld18**, which exchanges input and output through RAM and '
                    + 'takes libc26 as library parts (only what resolves an undefined name is '
                    + 'linked). The program exercises strtok, strcasecmp, frexp, fstat on an '
                    + 'unlinked file (kernel28\'s fstat2), O_EXCL, a stable qsort, `%.2f` rounding '
                    + 'and freopen.',
                files: [
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'l26x', build: frontlineBuild('l26x.c', 'files/tests/stage017/user/l26x.c') },
                ],
                samples: [
                    { cmd: 'l26x', note: 'expected lines are in tests/stage017/test.sh (section 12); rc 0 and freopen y at the end',
                        script: 'l26x\necho "rc $?"\ncat y.txt\n' },
                ],
            },
            {
                id: 'heap29',
                label: 'kernel29 · 2 GiB',
                kernel: 'assets/bin/kernel29.bin',
                fs: 4, sfsOffset: 1536 << 20, imgSize: 4 << 20, maxEntries: 128, ramSize: 2147483648,
                heavy: true,
                blurb: 'GCC\'s cc1 ran out of heap on `gcc/insn-recog.c -O2` under kernel28 (256 MiB '
                    + 'of user space, 142 MB of it the image). kernel29 maps RAM as 2 GiB — the '
                    + 'full 32-bit range above 0x8000_0000 — and gives user space 1 GiB; the sfs '
                    + 'window moves to 0xe000_0000. `bigheap` takes 48 MiB sixteen times and writes '
                    + 'both ends of each block: expect **heap 768 0 1**. The emulator reserves 2 GiB '
                    + 'lazily; this scenario really touches about 800 MB of your browser\'s memory.',
                files: [
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'bigheap', build: frontlineBuild('bigheap.c', 'files/tests/stage017/user/bigheap.c') },
                ],
                samples: [
                    { cmd: 'bigheap', note: 'heap <MiB taken> <mismatches> <crossed 0x9600_0000> — 768 0 1 on kernel29',
                        script: 'bigheap\necho "rc $?"\n' },
                ],
            },
            {
                id: 'heap28',
                label: 'kernel28 · 256 MiB',
                kernel: 'assets/bin/kernel28.bin',
                fs: 4, sfsOffset: 512 << 20, imgSize: 4 << 20, maxEntries: 128, ramSize: 1 << 30,
                heavy: true,
                blurb: 'The identical binary on the previous generation: malloc returns NULL part '
                    + 'way through and the top never crosses 0x9600_0000. One number alone proves '
                    + 'nothing, which is why tests/stage017 always runs both.',
                files: [
                    { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                    { name: 'bigheap', build: frontlineBuild('bigheap.c', 'files/tests/stage017/user/bigheap.c') },
                ],
                samples: [
                    { cmd: 'bigheap', note: 'stops inside 256 MiB: heap <n> 0 0', script: 'bigheap\necho "rc $?"\n' },
                ],
            },
        ],
    },
    // ---- Stage 18: nas1 (psABI のアセンブラ) ----
    // tests/stage018 と同じく kernel29 の上で走らせる (nas1 は libc26 と組んで
    // あり，fstat2 (kernel28) を要る。tools/nas18.sh run と同じ配置)
    stage018: {
        mode: 'script',
        bootLine: 'sh2 go.sh\n',
        note: '`nas1` is the first piece of the psABI toolchain for Linux: a GNU-as-compatible '
            + 'assembler for RV32IMA + Zicsr + Zifencei that writes ELF32 relocatable objects. It '
            + 'is written in C89, compiled by cc15as and linked by ld18 against libc26, and runs '
            + 'as a command on kernel29. The `.o` files it produces appear below after the session; '
            + 'tests/stage018 checks them against the ISA text with objdump / readelf, and against '
            + 'a host-built nas1 byte for byte.',
        scenarios: [{
            id: 'nas1',
            label: 'kernel29 · nas1',
            kernel: 'assets/bin/kernel29.bin',
            fs: 4, sfsOffset: 1536 << 20, imgSize: 64 << 20, maxEntries: 1024, ramSize: 2147483648,
            heavy: true,
            blurb: '`insn.s` lists every instruction and pseudo-instruction with the encoding '
                + 'expected from the ISA text in a comment; `elf.s` exercises sections, data, '
                + 'relocations (`%hi` / `%lo` / `%pcrel_hi` / `call` / branches / `ADD64` − '
                + '`SUB64` pairs) and symbol binding — its expected section table is '
                + 'tests/stage018/nas1/elf.expect. `far.s` branches out of range and must be '
                + 'refused rather than silently truncated.',
            files: [
                { name: 'bin/sh2', asset: 'assets/bin/sh2.bin' },
                { name: 'sh2', asset: 'assets/bin/sh2.bin' },
                { name: 'nas1', asset: 'assets/bin/nas1' },
                { name: 'insn.s', asset: 'files/tests/stage018/nas1/insn.s' },
                { name: 'elf.s', asset: 'files/tests/stage018/nas1/elf.s' },
                { name: 'far.s', text: ' beq a0, a1, .+4096\n' },
                { name: 'hello.s', text: HELLO_S },
            ],
            samples: [
                { cmd: 'assemble the tests', note: 'insn.o and elf.o; far.s must fail (expect rc 0, 0, then nonzero)',
                    script: 'nas1 -o insn.o insn.s\necho "insn $?"\nnas1 -o elf.o elf.s\necho "elf $?"\n'
                        + 'nas1 -o far.o far.s\necho "far $?"\n' },
                { cmd: 'hello.s', note: 'a small unit of your own — edit hello.s via the script (here-doc) and assemble it',
                    script: 'nas1 -o hello.o hello.s\necho "hello $?"\n' },
            ],
        }],
    },
};
