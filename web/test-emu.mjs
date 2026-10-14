// web/rv32.js (デモ用エミュレータ) の検証。
//
// チェーンの実成果物 (tmp/build/。QEMU + コンテナで生成したもの) を
// エミュレータで実行し，出力が QEMU での出力とビット一致することを確かめる。
// フィルタ実行 (Stage 1〜11, 14, 15) に加えて，OS (kernel12/13 + sfs,
// kernel17/18/19 + sfs2, kernel23〜29 + sfs2/3/4) の起動とゲスト内ビルドまで
// tests/stage012・013・015・016・017・018 と同じ素材・期待値で検査する。
// Stage 17 以降は sfs3 / sfs4 の JS 版がホストのツールとバイト一致すること，
// ld18 の JS 駆動がチェーンの nas1 を再現すること，goldfish RTC を読む
// カーネルの時刻と差分ビルドまで含む。
// 事前に sh tools/build.sh all を済ませておくこと。
//
// 使用法: node web/test-emu.mjs
import { readFileSync, existsSync } from 'node:fs';
import { Machine, runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';
import { packSfs, unpackSfs, SFS_OFFSET } from './sfs.js';
import { packSfs2, unpackSfs2 } from './sfs2.js';
import { packSfs3, unpackSfs3 } from './sfs3.js';
import { packSfs4, unpackSfs4 } from './sfs4.js';
import { ld18Input, runLd18 } from './ld18.js';
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

// --- OS: kernel13 で sh と ed の共存 (spawn の逐次性) ----------------------
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

// --- sfs2: JS 版がホスト側のツール (tools/sfs2.sh) とバイト一致すること --
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
            'sfs2: JS の pack がホスト側 tools/sfs2.sh のイメージとバイト一致');

        const back = unpackSfs2(mine);
        const got = Object.fromEntries(back.map((f) => [f.path, f]));
        report(back.length === 11                       // 6 ファイル + 5 ディレクトリ
            && got['src/one.c'] && dec(got['src/one.c'].data) === 'aaa\n'
            && dec(got['inc/one.c'].data) === 'ddd\n'   // 同名で取り違えない
            && got['src/a/b'].dir === true
            && got['empty'].dir === true
            && got['inc/empty.h'].data.length === 0,
        'sfs2: unpack がツリーをそのまま戻す (空の階層・同名別階層を含む)');
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
}

// sfs2 のツリーでカーネルを起動する (tests/stage016 と同じ注入位置・大きさ)
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

// --- OS: libc15 の 64 bit / 浮動小数点が OS の上で動作する (Stage 15 第 4 部)
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

// --- OS: kernel17 が sfs2 のツリーを経路で参照する (第 1 部) --------------
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
    // 走らせたあとのイメージに out/ と out/f.txt が増えていること
    // (プレイグラウンドのツリー表示が見せるのはこれである)
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

// ===========================================================================
// Stage 17 / 18: stone の OS の上のツール群と nas1。手順・素材・期待値は
// tests/stage017/test.sh・tests/stage018/test.sh と同一 (web/pipelines.js の
// ターミナルが使うものと同じ根・同じ台本)

// --- sfs3 / sfs4: JS 版がホスト側のツールとバイト一致すること --------------
{
    const tree = [
        { name: 'top.txt', data: text('hello\n'), mtime: 1700000000123456789n },
        { name: 'src/one.c', data: text('aaa\n'), mtime: 1600000123000000001n },
        { name: 'src/a/two.c', data: text('bbb\n'), mtime: 1500000000999999999n },
        { name: 'src/a/b/three.c', data: text('ccc\n'), mtime: 1n },
        { name: 'inc/one.c', data: text('ddd\n'), mtime: 0n },
        { name: 'inc/empty.h', data: new Uint8Array(0), mtime: 1400000000500000000n },
        { name: 'empty', dir: true, mtime: 1300000000000000000n },
        { name: 'src', dir: true, mtime: 1200000000000000000n },
        { name: 'src/a', dir: true, mtime: 1100000000000000000n },
        { name: 'src/a/b', dir: true, mtime: 1000000000000000000n },
        { name: 'inc', dir: true, mtime: 900000000000000000n },
    ];
    const stampOf = (ns) => `@${ns / 1000000000n}.${String(ns % 1000000000n).padStart(9, '0')}`;
    for (const [tag, pack, unpack, tool, extra] of [
        ['sfs3', packSfs3, unpackSfs3, 'tools/sfs3.sh', []],
        ['sfs4', packSfs4, unpackSfs4, 'tools/sfs4.sh',
            [{ name: `src/${'g'.repeat(92)}`, data: text('long\n'), mtime: 1333333333444444444n }]],
    ]) {
        const dir = mkdtempSync(`${tmpdir()}/stone-${tag}-`);
        try {
            const list = [...tree, ...extra];
            for (const f of list) {
                const full = `${dir}/root/${f.name}`;
                if (f.dir) { mkdirSync(full, { recursive: true }); continue; }
                mkdirSync(full.slice(0, full.lastIndexOf('/')), { recursive: true });
                writeFileSync(full, f.data);
            }
            // 時刻はディレクトリを深い順に，中身を作った後で付ける
            for (const f of list.filter((x) => !x.dir)) {
                execFileSync('touch', ['-d', stampOf(f.mtime), `${dir}/root/${f.name}`]);
            }
            for (const f of list.filter((x) => x.dir).sort((a, b) => b.name.length - a.name.length)) {
                execFileSync('touch', ['-d', stampOf(f.mtime), `${dir}/root/${f.name}`]);
            }
            execFileSync('touch', ['-d', stampOf(800000000000000000n), `${dir}/root`]);
            execFileSync('sh', [tool, 'pack', `${dir}/root`, `${dir}/ref.img`, '1048576', '64'],
                { stdio: 'pipe' });
            const mine = pack(list, 1 << 20, 64, { rootMtime: 800000000000000000n });
            report(eq(mine, read(`${dir}/ref.img`)),
                `${tag}: JS の pack がホスト側 ${tool} のイメージとバイト一致 (時刻を含む)`);
            const back = unpack(mine);
            const got = Object.fromEntries(back.map((f) => [f.path, f]));
            report(back.length === list.length
                && got['src/one.c'].mtime === 1600000123000000001n
                && got['src/a/b'].dir === true && got['src/a/b'].mtime === 1000000000000000000n
                && dec(got['inc/one.c'].data) === 'ddd\n',
            `${tag}: unpack がツリーと時刻をそのまま戻す`);
        } finally {
            rmSync(dir, { recursive: true, force: true });
        }
    }
}

// sfs2 / sfs3 / sfs4 のツリーでカーネルを起動する。配置と RAM は tests/stage017 の
// runroot* と同じ (kernel23/24: @64 MiB・512 MiB，kernel25〜28: @512 MiB・1 GiB，
// kernel29: @1.5 GiB・2 GiB)
const PACK = { 2: packSfs2, 3: packSfs3, 4: packSfs4 };
const UNPACK = { 2: unpackSfs2, 3: unpackSfs3, 4: unpackSfs4 };
const runOSn = (kernel, fs, files, opts = {}) => {
    const imgSize = opts.imgSize || (4 << 20);
    const off = opts.sfsOffset || SFS_OFFSET;
    const m = new Machine(read(kernel), { ramSize: opts.ramSize || (512 << 20) });
    m.mem.set(PACK[fs](files, imgSize, opts.maxEntries || 128), off);
    const chunks = [];
    for (;;) {
        const r = m.run(4_000_000_000);
        chunks.push(r.output);
        if (r.status === 'exit' || r.status !== 'budget') {
            const files2 = () => UNPACK[fs](m.mem.subarray(off, off + imgSize));
            return { exitCode: r.status === 'exit' ? m.exitCode : -1, status: r.status,
                output: concatBytes(chunks), m, files: files2 };
        }
    }
};
const sh2 = (script) => [
    { name: 'bin/sh2', data: read('tmp/build/sh2.bin') },
    { name: 'sh2', data: read('tmp/build/sh2.bin') },
    { name: 'go.sh', data: text(script) },
    boot('sh2 go.sh\n'),
];
const expect17 = (name) => read(`tests/stage017/expected/${name}.txt`);
const u17 = (p) => read(`tests/stage017/user/${p}`);
const HDR18 = ['assert.h', 'ctype.h', 'dirent.h', 'errno.h', 'fcntl.h', 'inttypes.h',
    'limits.h', 'math.h', 'setjmp.h', 'stdarg.h', 'stddef.h', 'stdio.h', 'stdlib.h',
    'string.h', 'time.h', 'unistd.h'];
const L18 = ['l18_src_string', 'l18_src_stdlib', 'l18_src_misc15', 'l18_posix_sys',
    'l18_posix_morecore', 'l18_posix_stdio', 'l18_posix_assert', 'l18_posix_dir',
    'rt64', 'rtfp'];
// tests/stage017 の mkroot と同じ根 (/bin の 3 段・/include・/lib の 1 揃い・ツール)
const toolbin17 = () => [
    { name: 'bin/pp16', data: read('tmp/build/pp16cmd') },
    { name: 'bin/cc15p', data: read('tmp/build/cc15pcmd') },
    { name: 'bin/ld16', data: read('tmp/build/ld16cmd') },
    ...HDR18.map((h) => ({ name: `include/${h}`, data: read(`stage016/libc18/include/${h}`) })),
    { name: 'include/sys/stat.h', data: read('stage016/libc18/include/sys/stat.h') },
    { name: 'include/sys/time.h', data: read('stage016/libc18/include/sys/time.h') },
];
const root17 = () => [
    ...toolbin17(),
    ...L18.map((o) => ({ name: `lib/${o}.o`, data: read(`tmp/build/${o}.o`) })),
    { name: 'cc', data: read('tmp/build/cc17') },
    { name: 'cc18', data: read('tmp/build/cc18') },
    { name: 'ar', data: read('tmp/build/ar17') },
    { name: 'mk', data: read('tmp/build/mk17') },
];
const libc18src = () => [
    ...['string', 'ctype', 'stdlib', 'morecore', 'misc15'].map((f) =>
        ({ name: `src/${f}.c`, data: read(`stage016/libc18/src/${f}.c`) })),
    ...['sys', 'morecore', 'stdio', 'assert', 'dir'].map((f) =>
        ({ name: `posix/${f}.c`, data: read(`stage016/libc18/posix/${f}.c`) })),
];

// --- OS: cc が hello と selfhost を組み，走らせる (第 1 部) ------------------
{
    const r = runOSn('tmp/build/kernel23.bin', 2, [
        ...root17(),
        { name: 'hello.c', data: u17('hello.c') },
        { name: 'selfhost.c', data: u17('selfhost.c') },
        ...sh2('cc -o hello hello.c\necho "hello-build $?"\nhello\necho "hello-run $?"\n'
            + 'cc -o selfhost selfhost.c\necho "selfhost-build $?"\nselfhost\necho "selfhost-run $?"\n'),
    ], { imgSize: 16 << 20, maxEntries: 256 });
    report(r.exitCode === 0 && eq(r.output, expect17('basic')),
        'os17: kernel23 の上で cc (pp16 → cc15p → ld16) が hello と selfhost を組み，走る (basic)');
}

// --- OS: 複数の翻訳単位と書庫 (第 2 部) -------------------------------------
{
    const r = runOSn('tmp/build/kernel23.bin', 2, [
        ...root17(),
        { name: 'multi.c', data: u17('multi.c') },
        { name: 'mathx.c', data: u17('mathx.c') },
        { name: 'strx.c', data: u17('strx.c') },
        { name: 'inc/mathx.h', data: u17('inc/mathx.h') },
        { name: 'inc/strx.h', data: u17('inc/strx.h') },
        ...sh2('cc18 -c -I inc mathx.c -o mathx.o\necho "compile-mathx $?"\n'
            + 'cc18 -c -I inc strx.c -o strx.o\necho "compile-strx $?"\n'
            + 'ar rcs libx.a mathx.o strx.o\necho "ar-rcs $?"\nar t libx.a\n'
            + 'cc18 -I inc -DVERSION=7 -o multi multi.c libx.a\necho "link $?"\nmulti\necho "run $?"\n'),
    ], { imgSize: 16 << 20, maxEntries: 256 });
    const made = Object.fromEntries(r.files().map((f) => [f.path, f]));
    report(r.exitCode === 0 && eq(r.output, expect17('multi'))
        && made['libx.a'] && dec(made['libx.a'].data.subarray(0, 8)) === '!<arch>\n',
    'os17: cc18 と ar17 が複数の翻訳単位を書庫経由でビルドする (multi。書庫は ar の形)');
}

// --- OS: make が libc を組む (第 3 部の 1 / 第 3 部の 2) -------------------
{
    const kroot = (mk, makefile) => [
        ...toolbin17(),
        { name: 'lib/rt64.o', data: read('tmp/build/rt64.o') },
        { name: 'lib/rtfp.o', data: read('tmp/build/rtfp.o') },
        ...libc18src(),
        { name: 'cc18', data: read('tmp/build/cc18') },
        { name: 'ar', data: read('tmp/build/ar17') },
        { name: 'mk', data: read(`tmp/build/${mk}`) },
        { name: 'uselibc.c', data: u17('uselibc.c') },
        { name: 'Makefile', data: read(`tests/stage017/mk/${makefile}`) },
    ];
    const r = runOSn('tmp/build/kernel23.bin', 2, [
        ...kroot('mk17', 'libc.mk'),
        ...sh2('mk\necho "mk $?"\nuselibc\necho "run $?"\n'),
    ], { imgSize: 32 << 20, maxEntries: 512 });
    report(r.exitCode === 0 && eq(r.output, expect17('mk')),
        'os17: mk17 が Makefile どおりに libc を組み，繋いだものが走る (mk)');
    const r2 = runOSn('tmp/build/kernel23.bin', 2, [
        ...kroot('mk19', 'fn.mk'),
        ...sh2('mk show\nmk\necho "mk $?"\nuselibc\necho "run $?"\n'),
    ], { imgSize: 32 << 20, maxEntries: 512 });
    report(r2.exitCode === 0 && eq(r2.output, expect17('fn')),
        'os17: mk19 の関数で導いた並びから libc が組める (fn)');
}

// --- OS: sfs3 の時刻と差分ビルド (第 4 部。kernel24 は goldfish RTC を読む) ---
{
    const hilo = (ns) => `${ns >> 32n} ${ns & 0xffffffffn}`;
    const t = { a: 1700000000123456789n, b: 1600000123000000001n, z: 1500000000999999999n,
        d: 1400000000500000000n };
    const before = Date.now();
    const r = runOSn('tmp/build/kernel24.bin', 3, [
        { name: 'stamp', data: read('tmp/build/stamp') },
        { name: 'a.txt', data: text('hello\n'), mtime: t.a },
        { name: 'd/b.txt', data: text('worldly\n'), mtime: t.b },
        { name: 'zero', data: new Uint8Array(0), mtime: t.z },
        { name: 'd', dir: true, mtime: t.d },
        ...sh2('stamp a.txt d/b.txt zero d\necho "stamp $?"\nstamp nosuch\necho "missing $?"\n'
            + 'echo made > new.txt\nstamp new.txt\n'),
    ]);
    const after = Date.now();
    const want = `f 6 ${hilo(t.a)} a.txt\nf 8 ${hilo(t.b)} d/b.txt\nf 0 ${hilo(t.z)} zero\n`
        + `d 0 ${hilo(t.d)} d\nstamp 0\n? nosuch\nmissing 1\n`;
    const out = dec(r.output);
    report(r.exitCode === 0 && out.startsWith(want),
        'os17: ホストが詰めた時刻を kernel24 が読み，ナノ秒まで一致する (stamp)');
    const m = out.slice(want.length).match(/^f 5 (\d+) (\d+) new\.txt\n$/);
    const ns = m ? (BigInt(m[1]) << 32n) | BigInt(m[2]) : -1n;
    const sec = Number(ns / 1000000000n);
    report(m !== null && sec >= Math.floor(before / 1000) - 2 && sec <= Math.ceil(after / 1000) + 2,
        'os17: OS が書いたファイルに今の時刻 (goldfish RTC) が立つ');
    const made = r.files().find((f) => f.path === 'new.txt');
    report(made && made.mtime === ns,
        'os17: 走った後の sfs3 イメージを JS が読み，表の時刻が stamp の出力と一致する');

    // mk18 の差分ビルド (期待値は tests/stage017/expected/inc.txt)
    const old = 1500000000000000000n;
    const r2 = runOSn('tmp/build/kernel24.bin', 3, [
        { name: 'mk', data: read('tmp/build/mk18') },
        { name: 'a.src', data: text('A\n'), mtime: old },
        { name: 'b.src', data: text('B\n'), mtime: old },
        { name: 'Makefile', data: text('all: prog\n\n%.cp: %.src\n\tcat $< > $@\n\nprog: a.cp b.cp\n\tcat $^ > $@\n'),
            mtime: old },
        ...sh2('echo "--1--"\nmk\necho "--2--"\nmk\necho "--3--"\necho C > b.src\nmk\n'
            + 'echo "--4--"\nmk -B\necho "--end--"\n'),
    ]);
    report(r2.exitCode === 0 && eq(r2.output, expect17('inc')),
        'os17: mk18 は 2 度目に作らず，触った依存に繋がる分だけ作り直す (inc)');
}

// --- OS: sfs4 と kernel25 — 長い名前と広い窓 (第 5 部) ----------------------
{
    const G92 = 'g'.repeat(92);
    const G92B = 'h'.repeat(92);
    const DEEP = 'aaaaaaaaaa/bbbbbbbbbb/cccccccccc/dddddddddd/eeeeeeeeee/ffffffffff/'
        + 'gggggggggg/hhhhhhhhhh/iiiiiiiiii/jjjjjjjjjj/kkkkkkkkkk/target.txt';
    const hilo = (ns) => `${ns >> 32n} ${ns & 0xffffffffn}`;
    const t1 = 1700000001222222222n, t2 = 1700000002333333333n;
    const r = runOSn('tmp/build/kernel25.bin', 4, [
        { name: 'stamp', data: read('tmp/build/stamp') },
        { name: G92, data: text('ninety-two\n'), mtime: t1 },
        { name: DEEP, data: text('deep file\n'), mtime: t2 },
        ...sh2(`cat ${G92}\necho "cat $?"\ncp ${G92} ${G92B}\necho "cp $?"\ncat ${G92B}\n`
            + `stamp ${G92}\necho "name-stat $?"\nstamp ${DEEP}\necho "deep-stat $?"\n`),
    ], { sfsOffset: 512 << 20, ramSize: 1 << 30 });
    const want = `ninety-two\ncat 0\ncp 0\nninety-two\nf 11 ${hilo(t1)} ${G92}\nname-stat 0\n`
        + `f 10 ${hilo(t2)} ${DEEP}\ndeep-stat 0\n`;
    const made = r.files().find((f) => f.path === G92B);
    report(r.exitCode === 0 && dec(r.output) === want && made && dec(made.data) === 'ninety-two\n',
        'os17: kernel25 (sfs4 @0xa000_0000, RAM 1 GiB) が 92 バイトの名前と 131 バイトの経路を扱う');
}

// --- OS: プロセスの時間と経路の上限 (第 9 部。kernel27 / libc25) -------------
{
    const r = runOSn('tmp/build/kernel27.bin', 4, [
        { name: 'tmx', data: read('tmp/build/tmx') },
        ...sh2('tmx\necho "rc $?"\n'),
    ], { sfsOffset: 512 << 20, ramSize: 1 << 30 });
    // own-small は「spawn の間に親が使った時間 < 子の時間 (3 tick)」である。
    // 子は 30 ms しか燃やさないので，QEMU より 1 桁遅いこのエミュレータでは
    // spawn の経路 (親のイメージの退避と子の読込み) だけで 3 tick を超えて
    // n になりうる。時計の正しさは cut (子の時間が cutime へ入る) で見る
    const want = 'clk 100\npre-cut y\nspawn 0\ncut y\nown-small ?\npath-max 256 256\n'
        + 'path-noent -1 2\nmkdir y\np254 0 0\np255 0 0\np256 -1 7\nrc 0\n';
    const got = dec(r.output).replace(/^own-small [yn]$/m, 'own-small ?');
    report(r.exitCode === 0 && got === want,
        'os17: kernel27 の times (RTC で計上) が子の時間を cutime へ入れ，255 バイトの経路が通る (tmx)');
}

// --- ld18: RAM で受け渡すリンカの JS 駆動がチェーンの nas1 を再現する -------
const L26 = ['l26_posix_assert', 'l26_posix_dir', 'l26_posix_morecore', 'l26_posix_signal',
    'l26_posix_stdio', 'l26_posix_sys', 'l26_posix_sys26', 'l26_src_ctype', 'l26_src_misc15',
    'l26_src_misc26', 'l26_src_morecore', 'l26_src_stdlib', 'l26_src_string', 'rt64', 'rtfp'];
const l26libs = () => L26.map((n) => read(`tmp/build/${n}.o`));
{
    const r = runLd18(read('tmp/build/ld18.bin'), ld18Input(l26libs(), [read('tmp/build/nas1.o')]));
    report(r.status === 'exit' && r.exitCode === 0 && eq(r.output, read('tmp/build/nas1')),
        'ld18: JS 駆動 (RAM の 0x9000_0000 / 0xa800_0000) が tools/ld18.sh と同じ nas1 を出す');
}

// 最前線 (pp16 → cc15as の遠距離呼出し → ld18 + libc26) で OS の実行形式を組む。
// tests/stage017 第 12 部・第 13 部と同じ手順
const HDR26 = ['alloca.h', 'assert.h', 'ctype.h', 'dirent.h', 'errno.h', 'fcntl.h',
    'float.h', 'inttypes.h', 'limits.h', 'math.h', 'setjmp.h', 'signal.h', 'stdarg.h',
    'stddef.h', 'stdio.h', 'stdlib.h', 'string.h', 'time.h', 'unistd.h'];
function frontlineElf(unit) {
    const inc = 'stage017/libc26/include';
    const i = run('tmp/build/pp16.bin', buildBundle([
        ...HDR26.map((h) => ({ name: h, data: read(`${inc}/${h}`) })),
        ...['time.h', 'times.h', 'stat.h', 'types.h'].map((h) =>
            ({ name: `sys/${h}`, data: read(`${inc}/sys/${h}`) })),
        { name: unit.replace(/^.*\//, ''), data: read(unit) }]));
    const o = run('tmp/build/cc15as.bin', concatBytes([text('#pragma stone far_call\n'), i.output]));
    const r = runLd18(read('tmp/build/ld18.bin'), ld18Input(l26libs(), [o.output]));
    if (i.exitCode || o.exitCode || r.exitCode) {
        throw new Error(`frontlineElf ${unit}: pp ${i.exitCode} cc ${o.exitCode} ld ${r.exitCode}`);
    }
    return r.output;
}

// --- OS: libc26 の関数が kernel28 / kernel29 の上で動く (第 12 部・第 13 部) ---
{
    const l26x = frontlineElf('tests/stage017/user/l26x.c');
    const want = 'tok [a] [b] [c]\ncmp 1 1 1\nfrexp 750 6 -62500 -2\nfstat 0 6 1 1\naccess 0 -1\n'
        + 'asctime Sun Sep 16 01:03:52 1973\nalloca 42000 50000\nunlink 0 8 -1\n'
        + 'excl -1 17 1 1 0 -1 0 0 1\nmisc 5 123 4096 1 3 3\nqsort 0 3 6 2 5 8 1 4 7\n'
        + 'fmt 0.01 0.12 2.5 2\nrc 0\nfreopen y\n';
    const files = [{ name: 'l26x', data: l26x }, ...sh2('l26x\necho "rc $?"\ncat y.txt\n')];
    const r28 = runOSn('tmp/build/kernel28.bin', 4, files, { sfsOffset: 512 << 20, ramSize: 1 << 30 });
    report(r28.exitCode === 0 && dec(r28.output) === want,
        'os17: libc26 の関数が kernel28 の上で期待どおりに動く (ld18 / cc15as の遠距離呼出し, l26x)');
    const r29 = runOSn('tmp/build/kernel29.bin', 4, files, { sfsOffset: 1536 << 20, ramSize: 2147483648 });
    report(r29.exitCode === 0 && dec(r29.output) === want,
        'os17: 同じ l26x が kernel29 (RAM 2 GiB，sfs @0xe000_0000) の上でも同じ結果になる');

    // ユーザ領域が 1 GiB あること。48 MiB を 16 回取る。kernel28 では途中で止まる
    const bigheap = frontlineElf('tests/stage017/user/bigheap.c');
    const bh = [{ name: 'bigheap', data: bigheap }, ...sh2('bigheap\n')];
    const b29 = runOSn('tmp/build/kernel29.bin', 4, bh, { sfsOffset: 1536 << 20, ramSize: 2147483648 });
    report(b29.exitCode === 0 && dec(b29.output) === 'heap 768 0 1\n',
        'os17: kernel29 のヒープは 768 MiB を取れ，0x9600_0000 (kernel28 の上限) を超える');
    const b28 = runOSn('tmp/build/kernel28.bin', 4, bh, { sfsOffset: 512 << 20, ramSize: 1 << 30 });
    const m28 = dec(b28.output).match(/^heap (\d+) 0 0\n$/);
    report(b28.exitCode === 0 && m28 !== null && Number(m28[1]) < 768,
        `os17: 同じプログラムは kernel28 では 256 MiB の内で止まる (heap ${m28 ? m28[1] : '?'})`);
}

// --- Stage 18: nas1 が kernel29 の上で試験の入力を組み立てる -----------------
{
    const r = runOSn('tmp/build/kernel29.bin', 4, [
        { name: 'nas1', data: read('tmp/build/nas1') },
        { name: 'insn.s', data: read('tests/stage018/nas1/insn.s') },
        { name: 'elf.s', data: read('tests/stage018/nas1/elf.s') },
        { name: 'far.s', data: text(' beq a0, a1, .+4096\n') },
        ...sh2('nas1 -o insn.o insn.s\necho "insn $?"\nnas1 -o elf.o elf.s\necho "elf $?"\n'
            + 'nas1 -o far.o far.s\necho "far $?"\n'),
    ], { sfsOffset: 1536 << 20, ramSize: 2147483648, imgSize: 64 << 20, maxEntries: 1024 });
    const made = Object.fromEntries(r.files().map((f) => [f.path, f]));
    const out = dec(r.output);
    report(r.exitCode === 0 && /^insn 0\nelf 0\n/.test(out) && /\nfar [1-9]\d*\n$/.test(out)
        && made['insn.o'] && made['elf.o'] && !made['far.o'],
    'os18: kernel29 の上で nas1 が insn.s / elf.s を組み立て，範囲外の分岐 (far.s) を止める');

    // elf.o の節見出しが tests/stage018/nas1/elf.expect の section 行と一致する
    // (readelf -S の要約。ELF32 の節見出しを直に読む)
    const b = made['elf.o'] ? made['elf.o'].data : new Uint8Array(0);
    const sections = (bytes) => {
        const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const shoff = dv.getUint32(32, true), shnum = dv.getUint16(48, true);
        const shstr = dv.getUint16(50, true), ent = dv.getUint16(46, true);
        const sh = (i) => ({ name: dv.getUint32(shoff + i * ent, true),
            type: dv.getUint32(shoff + i * ent + 4, true), flags: dv.getUint32(shoff + i * ent + 8, true),
            off: dv.getUint32(shoff + i * ent + 16, true), size: dv.getUint32(shoff + i * ent + 20, true),
            align: dv.getUint32(shoff + i * ent + 32, true), entsize: dv.getUint32(shoff + i * ent + 36, true) });
        const strOff = sh(shstr).off;
        const nm = (o) => { let e = strOff + o; while (bytes[e]) e++; return dec(bytes.subarray(strOff + o, e)); };
        const T = { 1: 'PROGBITS', 7: 'NOTE', 8: 'NOBITS' };
        const F = (f) => [[1, 'W'], [2, 'A'], [4, 'X'], [0x10, 'M'], [0x20, 'S']]   // readelf の順
            .filter(([bit]) => f & bit).map(([, c]) => c).join('');
        const out2 = [];
        for (let i = 1; i < shnum; i++) {
            const s = sh(i);
            if (!(s.flags & 2)) continue;                 // 割付けられる節だけ (expect と同じ)
            out2.push(`section ${nm(s.name)} ${T[s.type] || s.type} ${F(s.flags)} size=0x${s.size.toString(16)} align=${s.align} entsize=${s.entsize}`);
        }
        return out2;
    };
    const want = readFileSync('tests/stage018/nas1/elf.expect', 'utf8').split('\n')
        .filter((ln) => ln.startsWith('section '));
    let got = [];
    try { got = b.length ? sections(b) : []; } catch { got = []; }
    report(got.length === want.length && got.every((ln, i) => ln === want[i]),
        'os18: elf.o の節 (名前・種類・旗・大きさ・整列) が tests/stage018/nas1/elf.expect と一致');

    // ホストの cc で組んだ nas1 と同じ .o を出すこと (tests/stage018 の 5。gcc があるときだけ)
    let hostcc = null;
    try { execFileSync('gcc', ['--version'], { stdio: 'pipe' }); hostcc = 'gcc'; } catch { /* 無い */ }
    if (hostcc && made['insn.o'] && made['elf.o']) {
        const dir = mkdtempSync(`${tmpdir()}/stone-nas1-`);
        try {
            execFileSync(hostcc, ['-std=c89', '-pedantic', '-Wno-long-long', '-Wall', '-Wextra',
                '-Werror', '-O1', '-o', `${dir}/nas1`, 'stage018/nas1.c'], { stdio: 'pipe' });
            let same = true;
            for (const u of ['insn', 'elf']) {
                execFileSync(`${dir}/nas1`, ['-o', `${dir}/${u}.o`, `tests/stage018/nas1/${u}.s`], { stdio: 'pipe' });
                if (!eq(read(`${dir}/${u}.o`), made[`${u}.o`].data)) same = false;
            }
            report(same, 'os18: stone の OS の上の nas1 とホストの cc で組んだ nas1 が同じ .o を出す');
        } finally {
            rmSync(dir, { recursive: true, force: true });
        }
    } else {
        console.log('skip os18: ホストの cc が無い (nas1 のホスト版との突き合わせ)');
    }
}

console.log(`\npassed: ${pass}, failed: ${fail} (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
process.exit(fail === 0 ? 0 : 1);
