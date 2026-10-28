// ld18 の駆動 (tools/ld18.sh の JS 版)。
//
// ld18 (stage015/ld18.sc) は入出力を RAM で受け渡す (@section io):
//   入力: RAM の 256 MiB (0x9000_0000) に 'LDI1' + 全体の長さ + 'E' (69) +
//         データスタックの大きさ，続けて各オブジェクトの (長さ, ライブラリか, 中身)
//   出力: RAM の 640 MiB (0xa800_0000) に 'LD18' + 長さ，次のページ (+4096) から中身
// ライブラリの部品 (-L。未定義の名前を定義するときだけ組む) を先に並べ，
// 必ず組むもの (-N) を後ろに置く —— 前置部の syscall スタブを jal で呼ぶ
// libc の部品は前置部から 1 MiB 以内に要るからである (tools/ld18.sh)。
// RAM は 1 GiB (ld18.sh と同じ)。
import { Machine, concatBytes } from './rv32.js';

export const LD18_RAM = 1 << 30;
const LD18_IN = 256 << 20;
const LD18_OUT = 640 << 20;

// libs / objs: Uint8Array の列。返り値: RAM へ置く入力の塊
export function ld18Input(libs, objs, dstk = 262144) {
    const u32 = (v) => {
        const b = new Uint8Array(4);
        new DataView(b.buffer).setUint32(0, v, true);
        return b;
    };
    const parts = [new TextEncoder().encode('LDI1'), u32(0), u32(69), u32(dstk)];
    for (const [list, lib] of [[libs, 1], [objs, 0]]) {
        for (const o of list) {
            parts.push(u32(o.length), u32(lib), o);
            const pad = (4 - (o.length % 4)) % 4;
            if (pad) parts.push(new Uint8Array(pad));
        }
    }
    const blob = concatBytes(parts);
    new DataView(blob.buffer).setUint32(4, blob.length, true);
    return blob;
}

// 返り値: { status, exitCode, icount, output (実行形式), diag (ld18 の診断) }
export function runLd18(ld18bin, blob, budget = 4_000_000_000) {
    const m = new Machine(ld18bin, { ramSize: LD18_RAM });
    m.mem.set(blob, LD18_IN);
    const chunks = [];
    let status;
    for (;;) {
        const r = m.run(budget);
        chunks.push(r.output);
        if (r.status === 'budget' && m.icount < budget * 4) continue;
        status = r.status;
        break;
    }
    const diag = concatBytes(chunks);
    if (status !== 'exit' || m.exitCode !== 0) {
        return { status, exitCode: m.exitCode, icount: m.icount, output: diag, diag };
    }
    const dv = new DataView(m.mem.buffer, m.mem.byteOffset);
    const magic = new TextDecoder().decode(m.mem.subarray(LD18_OUT, LD18_OUT + 4));
    if (magic !== 'LD18') {
        return { status: 'trap', exitCode: -1, icount: m.icount, output: diag, diag };
    }
    const len = dv.getUint32(LD18_OUT + 4, true);
    const out = m.mem.slice(LD18_OUT + 4096, LD18_OUT + 4096 + len);
    return { status: 'exit', exitCode: 0, icount: m.icount, output: out, diag };
}
