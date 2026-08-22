// web/rv32.js (展示用エミュレータ) の検証。
//
// チェーンの実成果物 (tmp/build/。QEMU + コンテナで生成したもの) を
// エミュレータで実行し，出力が QEMU での出力とビット一致することを確かめる。
// フィルタ実行 (Stage 1〜11, 14, 15) に加えて，OS (kernel12/13 + sfs,
// kernel17/18/19 + sfs2) の起動とゲスト内ビルドまで tests/stage012・013・
// 015・016 と同じ素材・期待値で検査する。
// 事前に sh tools/build.sh all を済ませておくこと。
//
// 使用法: node web/test-emu.mjs
import { readFileSync, existsSync } from 'node:fs';
import { Machine, runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';
import { packSfs, unpackSfs, SFS_OFFSET } from './sfs.js';
import { packSfs2, unpackSfs2 } from './sfs2.js';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';

process.chdir(new URL('..', import.meta.url).pathname);

let pass = 0, fail = 0;
const report = (ok, name) => {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}`);
    ok ? pass++ : fail++;
};
const eq = (a, b) =>
    a.length === b.length && a.every((v, i) => v === b[i]);
const read = (p) => new Uint8Array(readFileSync(p));
const text = (s) => new TextEncoder().encode(s);

if (!existsSync('tmp/build/asm.bin')) {
    console.error('error: tmp/build が無い。先に sh tools/build.sh all を実行する');
    process.exit(2);
}

const t0 = Date.now();
const run = (bin, input) => runFilter(read(bin), input);

// --- Stage 1: hex0 ---------------------------------------------------------
{
    const r = run('stage001/hex0.bin', read('stage001/hex0.hex'));
    report(r.status === 'exit' && r.exitCode === 0
        && eq(r.output, read('stage001/hex0.bin')),
    'hex0: 自己再生成 hex0(hex0.hex) == hex0.bin');

    report(run('stage001/hex0.bin', text('AB .')).exitCode === 1
        && run('stage001/hex0.bin', text('a.')).exitCode === 2,
    'hex0: エラー系 (不正文字 1 / 奇数桁 2)');
}

// --- Stage 2/3: hex1, asm --------------------------------------------------
{
    const r = run('tmp/build/hex1.bin', read('stage003/asm.hex1'));
    report(r.exitCode === 0 && eq(r.output, read('tmp/build/asm.bin')),
        'hex1: hex1(asm.hex1) == asm.bin (チェーン成果物と一致)');

    const golden = run('stage001/hex0.bin',
        read('tests/stage003/rv32im-expected.hex'));
    const enc = run('tmp/build/asm.bin', read('tests/stage003/rv32im.s'));
    report(enc.exitCode === 0 && eq(enc.output, golden.output),
        'asm: RV32IM 全命令エンコードがゴールデンと一致');

    const prog = run('tmp/build/asm.bin', read('tests/stage003/run.s'));
    const r2 = runFilter(prog.output, new Uint8Array(0));
    report(r2.exitCode === 0
        && new TextDecoder().decode(r2.output) === 'OK',
    'asm: run.s をアセンブルして実行 (stdout "OK")');
}

// --- Stage 4/5: sol, sc ----------------------------------------------------
{
    const r = run('tmp/build/sol.bin', read('stage005/sc.sol'));
    report(r.exitCode === 0 && eq(r.output, read('tmp/build/sc.bin')),
        'sol: sol(sc.sol) == sc.bin (チェーン成果物と一致)');

    const fib = run('tmp/build/sc.bin',
        withTerminator(read('tests/stage005/fib.sc'), 'eot'));
    const r2 = runFilter(fib.output, new Uint8Array(0));
    report(r2.exitCode === 0
        && new TextDecoder().decode(r2.output).trim() === '610',
    'sc: fib.sc をコンパイルして実行 (stdout "610")');
}

// --- Stage 6/7: scc, occ (固定点) ------------------------------------------
{
    const src = withTerminator(read('stage006/scc.sc'), 'eot');
    const r = run('tmp/build/sc.bin', src);
    report(r.exitCode === 0 && eq(r.output, read('tmp/build/scc1.bin')),
        'scc: sc(scc.sc) == scc1.bin');
    const r2 = run('tmp/build/scc.bin', src);
    report(r2.exitCode === 0 && eq(r2.output, read('tmp/build/scc.bin')),
        'scc: 固定点 scc(scc.sc) == scc.bin');

    const r3 = run('tmp/build/occ.bin',
        withTerminator(read('stage007/occ.sc'), 'eot'));
    report(r3.exitCode === 0 && eq(r3.output, read('tmp/build/occ.bin')),
        'occ: 固定点 occ(occ.sc) == occ.bin');
}

// --- Stage 8: cc + ld ------------------------------------------------------
{
    const o = run('tmp/build/cc8.bin',
        withTerminator(read('stage008/cc.sc'), 'eot'));
    const bin = runFilter(read('tmp/build/ld.bin'),
        withTerminator(o.output, 'nul'));
    report(o.exitCode === 0 && bin.exitCode === 0
        && eq(bin.output, read('tmp/build/cc8.bin')),
    'cc8: 固定点 ld(cc8(cc.sc)) == cc8.bin');
}

// --- Stage 9〜11: pp + cc + ld + libc (ホスト型 C) -------------------------
{
    const headers = ['ctype.h', 'limits.h', 'stdarg.h', 'stddef.h',
        'stdlib.h', 'string.h'].map((h) => ({
        name: h, data: read(`stage011/libc/include/${h}`),
    }));
    const bundle = buildBundle([...headers,
        { name: 'str.c', data: read('tests/stage011/src/str.c') }]);
    const i = run('tmp/build/pp.bin', bundle);
    const o = run('tmp/build/cc.bin', i.output);
    const bin = runFilter(read('tmp/build/ld.bin'), concatBytes([
        o.output, read('tmp/build/l11_string.o'), Uint8Array.of(0)]));
    const r = runFilter(bin.output, new Uint8Array(0));
    report(i.exitCode === 0 && o.exitCode === 0 && bin.exitCode === 0
        && r.exitCode === 0
        && eq(r.output, read('tests/stage011/expected/str.txt')),
    'libc: bundle -> pp -> cc -> ld(+string.o) -> 実行が expected と一致');
}

// --- 適合台帳 (Stage 14 / 15) ---------------------------------------------
// 台帳の書式は tests/stage014/ledger.txt 冒頭。期待値は sh の側で
// エスケープしてあるので (改行が \n)，実測を同じ形へ直して突き合わせる
const ledger = (path) => readFileSync(path, 'utf8').split('\n')
    .map((ln) => ln.trim())
    .filter((ln) => ln && !ln.startsWith('#'))
    .map((ln) => {
        const [name, state, want] = ln.split(/\s+/);
        return { name, state, want };
    });

// tests/stage*/test.sh の probe() が期待値を作るのと同じ変換
const asLedger = (bytes) => `${new TextDecoder().decode(bytes).replace(/\n+$/, '')}\n`
    .replace(/\\/g, '\\\\').replace(/\t/g, '\\t').replace(/\n/g, '\\n');

// --- Stage 14: cc14g (台帳の最前線) ---------------------------------------
{
    const led = ledger('tests/stage014/ledger.txt');
    const pick = ['multidecl', 'nestinit', 'bitfield', 'kr_func', 'vaforward'];
    let bad = null;
    for (const name of pick) {
        const e = led.find((x) => x.name === name);
        if (!e || e.state !== 'ok') { bad = `${name}: 台帳に ok が無い`; break; }
        const bundle = buildBundle([
            { name: 'stdarg.h', data: read('stage013/libc/include/stdarg.h') },
            { name: `${name}.c`, data: read(`tests/stage014/probe/${name}.c`) },
        ]);
        const i = run('tmp/build/pp.bin', bundle);
        const o = run('tmp/build/cc14g.bin', i.output);
        const bin = runFilter(read('tmp/build/ld.bin'),
            withTerminator(o.output, 'nul'));
        const r = runFilter(bin.output, new Uint8Array(0));
        if (i.exitCode || o.exitCode || bin.exitCode || r.exitCode) {
            bad = `${name}: 途中で失敗した`;
            break;
        }
        if (asLedger(r.output) !== e.want) {
            bad = `${name}: ${asLedger(r.output)} != ${e.want}`;
            break;
        }
    }
    report(bad === null,
        `cc14g: 適合プローブ ${pick.join(' / ')} が台帳の期待値どおり${bad ? ` — ${bad}` : ''}`);
}

// --- Stage 15: cc15p + rt64 + rtfp (64 bit と浮動小数点) ------------------
// 手順は tests/stage015/test.sh の probe() と同じ (pp -> cc15p ->
// ld(+rt64,+rtfp))。web/pipelines.js の stage015 が同じ並びを使う
{
    const led = ledger('tests/stage015/ledger.txt');
    const pick = ['llarith', 'lldiv', 'llvarg', 'fparith', 'fpstore'];
    const hdr = read('stage015/libc/include/stdarg.h');
    let bad = null;
    for (const name of pick) {
        const e = led.find((x) => x.name === name);
        if (!e || e.state !== 'ok') { bad = `${name}: 台帳に ok が無い`; break; }
        const bundle = buildBundle([
            { name: 'stdarg.h', data: hdr },
            { name: `${name}.c`, data: read(`tests/stage015/probe/${name}.c`) },
        ]);
        const i = run('tmp/build/pp.bin', bundle);
        const o = run('tmp/build/cc15p.bin', i.output);
        const bin = runFilter(read('tmp/build/ld.bin'), concatBytes([
            o.output, read('tmp/build/rt64.o'), read('tmp/build/rtfp.o'),
            Uint8Array.of(0)]));
        const r = runFilter(bin.output, new Uint8Array(0));
        if (i.exitCode || o.exitCode || bin.exitCode || r.exitCode) {
            bad = `${name}: 途中で失敗した`;
            break;
        }
        if (asLedger(r.output) !== e.want) {
            bad = `${name}: ${asLedger(r.output)} != ${e.want}`;
            break;
        }
    }
    report(bad === null,
        `cc15p: 64 bit と浮動小数点の probe ${pick.join(' / ')} が台帳とビット一致`
        + `${bad ? ` — ${bad}` : ''}`);
}

// ===========================================================================
// OS (Stage 12/13): カーネルを sfs イメージ付きで起動する。
// 手順・素材・期待値は tests/stage012・tests/stage013 と同一

// files を sfs に詰めて kernel を起動し，input を UART へ流す
const runOS = (kernel, files, input, imgSize = 4 << 20) => {
    const m = new Machine(read(kernel));
    m.mem.set(packSfs(files, imgSize), SFS_OFFSET);
    m.feed(input);
    const chunks = [];
    for (;;) {
        const r = m.run(4_000_000_000);
        chunks.push(r.output);
        if (r.status === 'exit') {
            return { exitCode: m.exitCode, output: concatBytes(chunks), m };
        }
        if (r.status !== 'budget') {
            return { exitCode: -1, status: r.status, output: concatBytes(chunks), m };
        }
    }
};
const harvest = (m, imgSize = 4 << 20) => {
    const files = unpackSfs(m.mem.subarray(SFS_OFFSET, SFS_OFFSET + imgSize));
    return Object.fromEntries(files.map((f) => [f.name, f.data]));
};
const f13 = (p) => read(`tests/stage013/fixtures/${p}`);
const boot = (line) => ({ name: 'boot', data: text(line) });
const dec = (b) => new TextDecoder().decode(b);

// --- OS: kernel12 で hello (ELF ロード・argv・終了コード) ------------------
{
    const hs12 = ['ctype.h', 'errno.h', 'fcntl.h', 'limits.h', 'stdarg.h',
        'stddef.h', 'stdio.h', 'stdlib.h', 'string.h', 'unistd.h']
        .map((h) => ({ name: h, data: read(`stage012/libc/include/${h}`) }));
    const bundle = buildBundle([...hs12,
        { name: 'hello.c', data: read('tests/stage012/user/hello.c') }]);
    const i = run('tmp/build/pp.bin', bundle);
    const o = run('tmp/build/cc.bin', i.output);
    const elf = runFilter(read('tmp/build/ld12.bin'), concatBytes([
        text('E'), o.output, Uint8Array.of(0)]));
    const r = runOS('tmp/build/kernel.bin',
        [{ name: 'hello', data: elf.output }, boot('hello\n')], new Uint8Array(0));
    report(elf.exitCode === 0 && r.exitCode === 3
        && eq(r.output, read('tests/stage012/expected/hello.txt')),
    'os12: kernel12 が hello (ELF) を U モードで走らせ argv と終了コード 3 を返す');
}

// --- OS: kernel13 で sh と ed の同居 (spawn の逐次性) ----------------------
{
    const r = runOS('tmp/build/kernel13.bin', [
        { name: 'sh', data: read('tmp/build/sh13') },
        { name: 'ed', data: read('tmp/build/ed13') },
        boot('sh\n'),
    ], f13('shed-script.txt'));
    const got = harvest(r.m);
    report(r.exitCode === 0
        && eq(r.output, read('tests/stage013/expected/shed.txt'))
        && dec(got['made.txt']) === 'alpha\nbeta\n',
    'os13: sh から ed を spawn し，同じ UART 入力を順に読み継ぐ (shed)');
}

// --- OS: ゲスト内ビルド (bundle -> pp -> cc -> ldin -> ld) -----------------
{
    const r = runOS('tmp/build/kernel13.bin', [
        { name: 'sh', data: read('tmp/build/sh13') },
        { name: 'bundle', data: read('tmp/build/bundle13') },
        { name: 'ldin', data: read('tmp/build/ldin13') },
        { name: 'pp', data: read('tmp/build/pp13cmd') },
        { name: 'cc', data: read('tmp/build/cc13cmd') },
        { name: 'ld', data: read('tmp/build/ld13cmd') },
        { name: 'hi.c', data: f13('hi.c') },
        boot('sh\n'),
    ], f13('guestbuild.txt'));
    const got = harvest(r.m);
    // ホスト経路と同じ生成物になること (エミュレータ内でホスト経路も再現)
    const hb = buildBundle([{ name: 'hi.c', data: f13('hi.c') }]);
    const hi = run('tmp/build/pp.bin', hb);
    const ho = run('tmp/build/cc.bin', hi.output);
    const hbin = runFilter(read('tmp/build/ld13.bin'), concatBytes([
        text('E'), ho.output, Uint8Array.of(0)]));
    report(r.exitCode === 0
        && eq(r.output, read('tests/stage013/expected/gb.txt'))
        && eq(got['hi.i'], hi.output) && eq(got['hi.o'], ho.output)
        && eq(got['hi'], hbin.output),
    'os13: ゲスト内ビルドの各段生成物がホスト経路とバイト一致 (guestbuild)');
}

// --- OS: 処理系の自己再生成 (cc が OS 上で cc10l.bin を作り直す) -----------
{
    const r = runOS('tmp/build/kernel13.bin', [
        { name: 'sh', data: read('tmp/build/sh13') },
        { name: 'eot', data: read('tmp/build/eot13') },
        { name: 'ldin', data: read('tmp/build/ldin13') },
        { name: 'cc', data: read('tmp/build/cc13cmd') },
        { name: 'ld', data: read('tmp/build/ld13cmd') },
        { name: 'cc12.sc', data: read('stage010/cc12.sc') },
        boot('sh\n'),
    ], f13('selfbuild.txt'));
    const got = harvest(r.m);
    report(r.exitCode === 0
        && eq(got['cc10l.bin'], read('tmp/build/cc10l.bin'))
        && eq(got['cc12.o'], read('tmp/build/cc10l.o')),
    'os13: OS 上で作り直した cc10l.bin がチェーンの成果物とバイト一致 (selfbuild)');
}

// --- OS: 対話実行 (入力待ちで停止し，入力が来たら続きから走る) ------------
{
    const m = new Machine(read('tmp/build/kernel13.bin'));
    m.mem.set(packSfs([
        { name: 'sh', data: read('tmp/build/sh13') },
        boot('sh\n'),
    ]), SFS_OFFSET);
    const step = () => {
        const chunks = [];
        for (;;) {
            const r = m.run(1_000_000_000);
            chunks.push(r.output);
            if (r.status !== 'budget') return { status: r.status, text: dec(concatBytes(chunks)) };
        }
    };
    const r1 = step();                                  // 入力なし -> プロンプトで停止
    m.feed(text('echo interactive\n'));
    const r2 = step();
    m.feed(text('exit 4\n'));
    const r3 = step();
    report(r1.status === 'waiting' && r1.text.endsWith('$ ')
        && r2.status === 'waiting' && r2.text.includes('interactive')
        && r3.status === 'exit' && m.exitCode === 4,
    'os13: 対話実行 — 入力待ちで止まり，1 行ずつ流すと応答して exit 4 で終わる');
}

// ===========================================================================
// sfs2 と Stage 16 のカーネル。手順・素材・期待値は tests/stage016 と同一

// --- sfs2: JS 版がホスト側の道具 (tools/sfs2.sh) とバイト一致すること ----
{
    const dir = mkdtempSync(`${tmpdir()}/stone-sfs2-`);
    try {
        const tree = [
            { name: 'top.txt', data: text('hello\n') },
            { name: 'src/one.c', data: text('aaa\n') },
            { name: 'src/a/two.c', data: text('bbb\n') },
            { name: 'src/a/b/three.c', data: text('ccc\n') },
            { name: 'inc/one.c', data: text('ddd\n') },   // src/one.c と同名・別階層
            { name: 'inc/empty.h', data: new Uint8Array(0) },
            { name: 'empty', dir: true },
        ];
        for (const f of tree) {
            const full = `${dir}/root/${f.name}`;
            if (f.dir) { mkdirSync(full, { recursive: true }); continue; }
            mkdirSync(full.slice(0, full.lastIndexOf('/')), { recursive: true });
            writeFileSync(full, f.data);
        }
        execFileSync('sh', ['tools/sfs2.sh', 'pack', `${dir}/root`,
            `${dir}/ref.img`, '1048576', '64'], { stdio: 'pipe' });
        const mine = packSfs2(tree, 1 << 20, 64);
        const ref = read(`${dir}/ref.img`);
        report(eq(mine, ref),
            'sfs2: JS の pack がホスト側 tools/sfs2.sh の像とバイト一致');

        const back = unpackSfs2(mine);
        const got = Object.fromEntries(back.map((f) => [f.path, f]));
        report(back.length === 11                       // 6 ファイル + 5 ディレクトリ
            && got['src/one.c'] && dec(got['src/one.c'].data) === 'aaa\n'
            && dec(got['inc/one.c'].data) === 'ddd\n'   // 同名で取り違えない
            && got['src/a/b'].dir === true
            && got['empty'].dir === true
            && got['inc/empty.h'].data.length === 0,
        'sfs2: unpack が木をそのまま戻す (空の階層・同名別階層を含む)');
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
}

// sfs2 の木でカーネルを起動する (tests/stage016 と同じ注入位置・大きさ)
const runOS2 = (kernel, files, opts = {}) => {
    const imgSize = opts.imgSize || (4 << 20);
    const m = new Machine(read(kernel), { ramSize: opts.ramSize });
    m.mem.set(packSfs2(files, imgSize, opts.maxEntries || 128), SFS_OFFSET);
    const chunks = [];
    for (;;) {
        const r = m.run(4_000_000_000);
        chunks.push(r.output);
        if (r.status === 'exit') {
            return { exitCode: m.exitCode, output: concatBytes(chunks), m, imgSize };
        }
        if (r.status !== 'budget') {
            return { exitCode: -1, status: r.status, output: concatBytes(chunks), m, imgSize };
        }
    }
};

// libc15 / libc16 とリンクした 'E' 実行形式を作る。並びは tests/stage016
const OBJ15 = ['l15_src_string', 'l15_src_stdlib', 'l15_src_misc15',
    'l15_posix_sys', 'l15_posix_morecore', 'l15_posix_stdio', 'l15_posix_assert'];
const OBJ16 = ['l16_src_string', 'l16_src_stdlib', 'l16_src_misc15',
    'l16_posix_sys', 'l16_posix_morecore', 'l16_posix_stdio', 'l16_posix_assert',
    'l16_posix_dir'];
const HDR15 = ['assert.h', 'ctype.h', 'errno.h', 'fcntl.h', 'inttypes.h',
    'limits.h', 'math.h', 'setjmp.h', 'stdarg.h', 'stddef.h', 'stdio.h',
    'stdlib.h', 'string.h', 'time.h', 'unistd.h'];
const HDR16 = ['assert.h', 'ctype.h', 'dirent.h', 'errno.h', 'fcntl.h',
    'inttypes.h', 'limits.h', 'math.h', 'setjmp.h', 'stdarg.h', 'stddef.h',
    'stdio.h', 'stdlib.h', 'string.h', 'time.h', 'unistd.h'];

function guestElf(gen, unit, extraHeaders = []) {
    const inc = gen === 16 ? 'stage016/libc/include' : 'stage015/libc/include';
    const hdrs = (gen === 16 ? HDR16 : HDR15)
        .map((h) => ({ name: h, data: read(`${inc}/${h}`) }));
    const objs = (gen === 16 ? OBJ16 : OBJ15)
        .map((n) => read(`tmp/build/${n}.o`))
        .concat([read('tmp/build/rt64.o'), read('tmp/build/rtfp.o')]);
    const i = run('tmp/build/pp16.bin', buildBundle([
        ...hdrs, ...extraHeaders,
        { name: unit.replace(/^.*\//, ''), data: read(unit) }]));
    const o = run('tmp/build/cc15p.bin', i.output);
    const elf = runFilter(read('tmp/build/ld16.bin'), concatBytes([
        text('E'), o.output, ...objs, Uint8Array.of(0)]));
    if (i.exitCode || o.exitCode || elf.exitCode) {
        throw new Error(`guestElf ${unit}: pp ${i.exitCode} cc ${o.exitCode} ld ${elf.exitCode}`);
    }
    return elf.output;
}

// --- OS: libc15 の 64 bit / 浮動小数点が OS の上で効く (Stage 15 第 4 部) --
// 手順・素材・期待値は tests/stage015 の lib15 と同じ。ただしリンカは
// ld14 (tests/stage015 と同じ) で，走らせるのは kernel16
{
    const hdrs = HDR15.map((h) => ({
        name: h, data: read(`stage015/libc/include/${h}`),
    }));
    const objs = OBJ15.map((n) => read(`tmp/build/${n}.o`))
        .concat([read('tmp/build/rt64.o'), read('tmp/build/rtfp.o')]);
    const i = run('tmp/build/pp.bin', buildBundle([...hdrs,
        { name: 'lib15.c', data: read('tests/stage015/user/lib15.c') }]));
    const o = run('tmp/build/cc15p.bin', i.output);
    const elf = runFilter(read('tmp/build/ld14.bin'), concatBytes([
        text('E'), o.output, ...objs, Uint8Array.of(0)]));
    const r = runOS('tmp/build/kernel16.bin',
        [{ name: 'lib15', data: elf.output }, boot('lib15\n')], new Uint8Array(0));
    report(i.exitCode === 0 && o.exitCode === 0 && elf.exitCode === 0
        && r.exitCode === 0
        && eq(r.output, read('tests/stage015/expected/lib15.txt')),
    'os15: kernel16 の上で %llu / %f / snprintf / strto / sscanf / setjmp / lseek が通る');
}

// --- OS: kernel17 が sfs2 の木を経路で引く (第 1 部) ----------------------
{
    const r = runOS2('tmp/build/kernel17.bin', [
        { name: 'top.txt', data: text('TOP\n') },
        { name: 'src/one.c', data: text('SRC-ONE\n') },
        { name: 'inc/one.c', data: text('INC-ONE\n') },     // 同名・別階層
        { name: 'src/a/b/three.c', data: text('THREE\n') },
        { name: 'pathprobe', data: guestElf(15, 'tests/stage016/user/pathprobe.c') },
        boot('pathprobe\n'),
    ]);
    report(r.exitCode === 0
        && eq(r.output, read('tests/stage016/expected/pathprobe.txt')),
    'os16: kernel17 が経路を解決する (絶対・相対・深さ・同名・不在。pathprobe)');
}

// --- OS: kernel18 がディレクトリを操作する (第 2 部) ----------------------
{
    const r = runOS2('tmp/build/kernel18.bin', [
        { name: 'top.txt', data: text('TOP\n') },
        { name: 'src/one.c', data: text('SRC-ONE\n') },
        { name: 'src/a/two.c', data: text('A-TWO\n') },
        { name: 'inc/one.c', data: text('INC-ONE\n') },
        {
            name: 'dirprobe',
            data: guestElf(16, 'tests/stage016/user/dirprobe.c', [{
                name: 'sys/stat.h',
                data: read('stage016/libc/include/sys/stat.h'),
            }]),
        },
        boot('dirprobe\n'),
    ]);
    // 走らせたあとの像に out/ と out/f.txt が増えていること
    // (プレイグラウンドの木表示が見せるのはこれである)
    const made = unpackSfs2(r.m.mem.subarray(SFS_OFFSET, SFS_OFFSET + r.imgSize));
    const out = made.find((f) => f.path === 'out');
    const made2 = made.find((f) => f.path === 'out/f.txt');
    report(r.exitCode === 0
        && eq(r.output, read('tests/stage016/expected/dirprobe.txt'))
        && out && out.dir === true && made2 && dec(made2.data) === 'MADE\n',
    'os16: kernel18 が一覧・作成・移動と . / .. を扱う (dirprobe)');
}

// --- OS: 記憶域が 14 MB から 256 MB へ広がった (第 3 部) ------------------
// **片方だけを見ても「広がった」ことは言えない** (docs/stage016-os.md 8.6)
{
    const elf = guestElf(16, 'tests/stage016/user/memprobe.c');
    const files = [{ name: 'memprobe', data: elf }, boot('memprobe\n')];
    const got = (kernel, ramSize) => {
        const r = runOS2(kernel, files, { maxEntries: 32, ramSize });
        const s = dec(r.output);
        const m = s.match(/^got (\d+)$/m);
        return { mib: m ? Number(m[1]) : -1, ok: /^verify ok$/m.test(s),
            exitCode: r.exitCode };
    };
    const old = got('tmp/build/kernel18.bin');
    const wide = got('tmp/build/kernel19.bin', 512 << 20);
    report(old.exitCode === 0 && old.ok && old.mib > 0 && old.mib < 14,
        `os16: kernel18 (128 MB) の上限は 14 MiB 未満 (got ${old.mib} MiB, verify ok)`);
    report(wide.exitCode === 0 && wide.ok && wide.mib >= 250,
        `os16: kernel19 (512 MB) は 250 MiB 以上を取って書き戻せる (got ${wide.mib} MiB)`);
}

console.log(`\npassed: ${pass}, failed: ${fail} (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
process.exit(fail === 0 ? 0 : 1);
