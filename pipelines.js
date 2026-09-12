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
        // 適合台帳 (tests/stage014/ledger.txt) の probe をそのまま選べる。
        // 手順は tests/stage014/test.sh の probe() と同じ (pp -> cc14g -> ld)
        // 見本の一覧は台帳 (coverage の kind:'ledger' の組) から作る。
        // pipelines.js に名前を写さないので，台帳を直せば選択肢も直る
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
};

// OS 世代のターミナル。kernel + sfs で起動し，UART をそのまま端末へつなぐ。
// files: {name, asset} は成果物をそのまま，{name, build} はその場で
// コンパイルして sfs へ置く (どちらも本物のチェーン成果物で処理する)。
// samples は「入力欄へ先置きする一連のコマンド」(Enter は利用者が押す)。
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
// 正規表現でなめて資産を集めるので，テンプレート文字列にすると
// 参照が拾えない
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

// 「ヘッダを束ねて pp16 -> cc15p -> ld16 ('E') で実行形式にする」手順。
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
    // Stage 15: 64 bit と浮動小数点が **libc の上で** 効くところを見せる。
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
        note: 'Three generations, three things to see. Each program below is the exact probe '
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
                    + 'two directories, which the old flat sfs could not even represent.',
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
                    + 'the namespace was flat and silently wrong the moment there is a cwd. Watch '
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
};
