// web/rv32.js (展示用エミュレータ) の検証。
//
// チェーンの実成果物 (tmp/build/。QEMU + コンテナで生成したもの) を
// エミュレータで実行し，出力が QEMU での出力とビット一致することを確かめる。
// 事前に sh tools/build.sh all を済ませておくこと。
//
// 使用法: node web/test-emu.mjs
import { readFileSync, existsSync } from 'node:fs';
import { runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';

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

console.log(`\npassed: ${pass}, failed: ${fail} (${((Date.now() - t0) / 1000).toFixed(1)}s)`);
process.exit(fail === 0 ? 0 : 1);
