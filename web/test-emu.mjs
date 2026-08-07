// web/rv32.js (展示用エミュレータ) の検証。
//
// チェーンの実成果物 (tmp/build/。QEMU + コンテナで生成したもの) を
// エミュレータで実行し，出力が QEMU での出力とビット一致することを確かめる。
// フィルタ実行 (Stage 1〜11, 14) に加えて，OS (kernel12/13 + sfs) の起動と
// ゲスト内ビルドまで tests/stage012・013 と同じ素材・期待値で検査する。
// 事前に sh tools/build.sh all を済ませておくこと。
//
// 使用法: node web/test-emu.mjs
import { readFileSync, existsSync } from 'node:fs';
import { Machine, runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';
import { packSfs, unpackSfs, SFS_OFFSET } from './sfs.js';

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

// --- Stage 14: cc14b -------------------------------------------------------
{
    const bundle = buildBundle([{
        name: 'multidecl.c', data: read('tests/stage014/probe/multidecl.c'),
    }]);
    const i = run('tmp/build/pp.bin', bundle);
    const o = run('tmp/build/cc14b.bin', i.output);
    const bin = runFilter(read('tmp/build/ld.bin'),
        withTerminator(o.output, 'nul'));
    const r = runFilter(bin.output, new Uint8Array(0));
    report(i.exitCode === 0 && o.exitCode === 0 && bin.exitCode === 0
        && r.exitCode === 0
        && new TextDecoder().decode(r.output).trim() === '7',
    'cc14b: 適合プローブ multidecl.c が通り実行できる (台帳の期待値 "7")');
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

console.log(`\npassed: ${pass}, failed: ${fail} (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
process.exit(fail === 0 ? 0 : 1);
