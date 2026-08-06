// RV32IM ベアメタルエミュレータ (展示層)。
//
// stone の成果物 (フラットバイナリ) をブラウザ内で「UART フィルタ」として
// 実行するための最小実装である。実行モデルは docs/plan.md 3 章と同一:
//   - フラットバイナリを RAM 先頭 0x8000_0000 に配置し，そこから実行
//   - 16550 互換 UART (0x1000_0000): RBR/THR (offset 0), LSR (offset 5)
//   - test finisher (0x0010_0000): 0x5555 = 正常 / (code<<16)|0x3333 = 異常
//
// ビルド経路には一切使用しない (検証の基準は QEMU + コンテナのまま。
// docs/plan.md 2.3)。本実装の正しさは web/test-emu.mjs が「チェーンの
// 実成果物を実行した出力が QEMU での出力とビット一致すること」で確かめる。
//
// ES module。ブラウザ (Web Worker) と node の両方から使う。

export const RAM_BASE = 0x80000000;
export const RAM_SIZE = 128 << 20;          // QEMU virt の既定と同じ 128 MiB

const UART_BASE = 0x10000000;
const FINISHER = 0x00100000;

// program: Uint8Array (フラットバイナリ)
// input:   Uint8Array (UART へ流す入力。終端文字は呼ぶ側が付ける)
// opts.budget: 実行命令数の上限 (無限ループ対策)
//
// 返り値: { status, exitCode, output, icount, pc }
//   status: 'exit' (finisher で停止) | 'budget' (命令数上限) | 'trap' (異常)
export function runFilter(program, input, opts = {}) {
    const budget = opts.budget ?? 4_000_000_000;
    const mem = new Uint8Array(RAM_SIZE);   // 新規確保 = ゼロ初期化 (QEMU と同じ)
    mem.set(program, 0);
    const dv = new DataView(mem.buffer);
    const x = new Int32Array(32);
    const out = [];
    let pc = 0;                             // RAM_BASE からのオフセットで持つ
    let inPos = 0;
    let icount = 0;
    const inLen = input.length;

    const done = (status, exitCode) => ({
        status,
        exitCode,
        output: Uint8Array.from(out),
        icount,
        pc: (RAM_BASE + pc) >>> 0,
    });

    // MMIO (RAM 外) の読み書き。UART と finisher のみ
    const mmioLoad = (addr) => {
        if (addr === UART_BASE) return inPos < inLen ? input[inPos++] : 0;
        if (addr === UART_BASE + 5) return (inPos < inLen ? 1 : 0) | 0x60;
        return 0;
    };

    while (icount < budget) {
        icount++;
        if (pc >>> 0 >= RAM_SIZE) return done('trap', -1);
        const inst = dv.getUint32(pc, true);
        const opcode = inst & 0x7f;
        const rd = (inst >>> 7) & 0x1f;
        const rs1 = (inst >>> 15) & 0x1f;
        const rs2 = (inst >>> 20) & 0x1f;
        const f3 = (inst >>> 12) & 7;
        let nextPc = pc + 4;

        switch (opcode) {
        case 0x37:                          // lui
            if (rd) x[rd] = inst & 0xfffff000;
            break;
        case 0x17:                          // auipc
            if (rd) x[rd] = (RAM_BASE + pc + (inst & 0xfffff000)) | 0;
            break;
        case 0x6f: {                        // jal
            const imm = ((inst >> 31) << 20)
                | (((inst >>> 12) & 0xff) << 12)
                | (((inst >>> 20) & 1) << 11)
                | ((inst >>> 21) & 0x3ff) << 1;
            if (rd) x[rd] = (RAM_BASE + pc + 4) | 0;
            nextPc = pc + imm;
            break;
        }
        case 0x67: {                        // jalr
            const imm = inst >> 20;
            const t = (x[rs1] + imm) & ~1;
            if (rd) x[rd] = (RAM_BASE + pc + 4) | 0;
            nextPc = (t - RAM_BASE) | 0;
            break;
        }
        case 0x63: {                        // 分岐
            const a = x[rs1], b = x[rs2];
            let taken;
            switch (f3) {
            case 0: taken = a === b; break;
            case 1: taken = a !== b; break;
            case 4: taken = a < b; break;
            case 5: taken = a >= b; break;
            case 6: taken = (a >>> 0) < (b >>> 0); break;
            case 7: taken = (a >>> 0) >= (b >>> 0); break;
            default: return done('trap', -1);
            }
            if (taken) {
                nextPc = pc + (((inst >> 31) << 12)
                    | (((inst >>> 7) & 1) << 11)
                    | (((inst >>> 25) & 0x3f) << 5)
                    | ((inst >>> 8) & 0xf) << 1);
            }
            break;
        }
        case 0x03: {                        // ロード
            const addr = (x[rs1] + (inst >> 20)) >>> 0;
            const off = (addr - RAM_BASE) >>> 0;
            let v;
            if (off < RAM_SIZE) {
                switch (f3) {
                case 0: v = dv.getInt8(off); break;
                case 1: v = dv.getInt16(off, true); break;
                case 2: v = dv.getInt32(off, true); break;
                case 4: v = dv.getUint8(off); break;
                case 5: v = dv.getUint16(off, true); break;
                default: return done('trap', -1);
                }
            } else {
                v = mmioLoad(addr);
            }
            if (rd) x[rd] = v;
            break;
        }
        case 0x23: {                        // ストア
            const imm = ((inst >> 25) << 5) | ((inst >>> 7) & 0x1f);
            const addr = (x[rs1] + imm) >>> 0;
            const off = (addr - RAM_BASE) >>> 0;
            if (off < RAM_SIZE) {
                switch (f3) {
                case 0: dv.setUint8(off, x[rs2] & 0xff); break;
                case 1: dv.setUint16(off, x[rs2] & 0xffff, true); break;
                case 2: dv.setInt32(off, x[rs2], true); break;
                default: return done('trap', -1);
                }
            } else if (addr === UART_BASE) {
                out.push(x[rs2] & 0xff);
            } else if (addr === FINISHER) {
                const v = x[rs2] >>> 0;
                if ((v & 0xffff) === 0x5555) return done('exit', 0);
                if ((v & 0xffff) === 0x3333) return done('exit', v >>> 16);
                return done('trap', -1);
            }
            // その他の MMIO への書込みは無視する
            break;
        }
        case 0x13: {                        // 即値演算
            const imm = inst >> 20;
            const a = x[rs1];
            let v;
            switch (f3) {
            case 0: v = (a + imm) | 0; break;
            case 1: v = a << (imm & 31); break;
            case 2: v = a < imm ? 1 : 0; break;
            case 3: v = (a >>> 0) < (imm >>> 0) ? 1 : 0; break;
            case 4: v = a ^ imm; break;
            case 5: v = (inst >>> 25) === 0x20 ? a >> (imm & 31)
                                              : a >>> (imm & 31) | 0; break;
            case 6: v = a | imm; break;
            case 7: v = a & imm; break;
            }
            if (rd) x[rd] = v;
            break;
        }
        case 0x33: {                        // レジスタ演算 (RV32I + M)
            const a = x[rs1], b = x[rs2];
            const f7 = inst >>> 25;
            let v;
            if (f7 === 1) {                 // M 拡張
                switch (f3) {
                case 0: v = Math.imul(a, b); break;
                case 1:                     // mulh
                    v = Number(BigInt.asIntN(32, (BigInt(a) * BigInt(b)) >> 32n));
                    break;
                case 2:                     // mulhsu
                    v = Number(BigInt.asIntN(32, (BigInt(a) * BigInt(b >>> 0)) >> 32n));
                    break;
                case 3:                     // mulhu
                    v = Number(BigInt.asIntN(32,
                        (BigInt(a >>> 0) * BigInt(b >>> 0)) >> 32n));
                    break;
                case 4:                     // div
                    v = b === 0 ? -1
                        : (a === -2147483648 && b === -1) ? a : (a / b) | 0;
                    break;
                case 5:                     // divu
                    v = b === 0 ? -1 : (((a >>> 0) / (b >>> 0)) >>> 0) | 0;
                    break;
                case 6:                     // rem
                    v = b === 0 ? a
                        : (a === -2147483648 && b === -1) ? 0 : (a % b) | 0;
                    break;
                case 7:                     // remu
                    v = b === 0 ? a : (((a >>> 0) % (b >>> 0)) >>> 0) | 0;
                    break;
                }
            } else {
                switch (f3) {
                case 0: v = f7 === 0x20 ? (a - b) | 0 : (a + b) | 0; break;
                case 1: v = a << (b & 31); break;
                case 2: v = a < b ? 1 : 0; break;
                case 3: v = (a >>> 0) < (b >>> 0) ? 1 : 0; break;
                case 4: v = a ^ b; break;
                case 5: v = f7 === 0x20 ? a >> (b & 31) : (a >>> (b & 31)) | 0; break;
                case 6: v = a | b; break;
                case 7: v = a & b; break;
                }
            }
            if (rd) x[rd] = v;
            break;
        }
        case 0x0f:                          // fence: 単一ハートでは何もしない
            break;
        default:
            return done('trap', -1);        // ecall / ebreak / CSR 含む
        }
        pc = nextPc | 0;
    }
    return done('budget', -1);
}

// パイプライン実行の入力組立てに使う補助
export const EOT = 0x04;

export function withTerminator(bytes, term) {
    if (term === 'dot') {
        // hex0/hex1/asm/sol の終端 '.'。既に終端していれば足さない
        for (let i = bytes.length - 1; i >= 0; i--) {
            const c = bytes[i];
            if (c === 0x20 || c === 0x09 || c === 0x0d || c === 0x0a) continue;
            if (c === 0x2e) return bytes;
            break;
        }
        return concatBytes([bytes, Uint8Array.of(0x2e)]);
    }
    if (term === 'eot') return concatBytes([bytes, Uint8Array.of(EOT)]);
    if (term === 'nul') return concatBytes([bytes, Uint8Array.of(0)]);
    return bytes;
}

export function concatBytes(parts) {
    let n = 0;
    for (const p of parts) n += p.length;
    const r = new Uint8Array(n);
    let o = 0;
    for (const p of parts) { r.set(p, o); o += p.length; }
    return r;
}

// pp への入力 (束ね) を組み立てる。形式は docs/stage009-pp.md 2.2
// (tools/bundle.sh と同じ。最後のメンバが翻訳単位)
export function buildBundle(members) {
    const enc = new TextEncoder();
    const parts = [enc.encode('#!stone-bundle\n')];
    for (const m of members) {
        parts.push(enc.encode(`@${m.name} ${m.data.length}\n`));
        parts.push(m.data);
    }
    parts.push(Uint8Array.of(EOT));
    return concatBytes(parts);
}
