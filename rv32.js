// RV32IM ベアメタルエミュレータ (デモ層)。
//
// stone の成果物 (フラットバイナリ) をブラウザ内で実行するための最小実装。
// 実行モデルは docs/plan.md 3 章と同一:
//   - フラットバイナリを RAM 先頭 0x8000_0000 に配置し，そこから実行
//   - 16550 互換 UART (0x1000_0000): RBR/THR (offset 0), LSR (offset 5)
//   - test finisher (0x0010_0000): 0x5555 = 正常 / (code<<16)|0x3333 = 異常
//
// Stage 12 以降のカーネルを動かすため，M/U の 2 特権と CSR・mret・
// ecall トラップを備える (ld12/ld13 の 'K' 前置部と kernel が使う範囲。
// docs/stage012-os.md 5 章)。PMP は CSR としては受けるが強制はしない
// (全許可相当。デモではメモリ保護の失敗を再現する必要がない)。
//
// Stage 17 以降のカーネルのために 2 つを足してある:
//   - goldfish RTC (0x0010_1000): TIME_LOW / TIME_HIGH。epoch からのナノ秒
//     (kernel24 以降が sfs3 / sfs4 の更新時刻と times (153) に使う。
//     docs/stage017-cc.md 11.2)。下位語を読んだ瞬間の上位語を固定して
//     TIME_HIGH に見せる (QEMU の goldfish-rtc と同じ)
//   - RAM の遅延確保: 2 GiB (kernel29) の RAM は WebAssembly.Memory で
//     予約し，触れたページだけを確保する。ブラウザでも node でも
//     同じ 1 本の Uint8Array として扱う (docs/stage017-gcc.md 8.15)
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
const RTC_LOW = 0x00101000;             // goldfish-rtc TIME_LOW
const RTC_HIGH = 0x00101004;            // goldfish-rtc TIME_HIGH

// RAM を確保する。大きな RAM (1〜2 GiB) は WebAssembly.Memory で予約する ——
// 触れたページだけが実際に確保されるので，疎に使うカーネル (kernel29 は
// sfs の窓を 0xe000_0000 に置く) の分を前もって払わずに済む。
// WebAssembly が無い環境や上限を超えたときは通常の ArrayBuffer に落とす
function allocRam(size) {
    const PAGE = 65536;
    if (typeof WebAssembly !== 'undefined' && size % PAGE === 0) {
        try {
            const m = new WebAssembly.Memory({ initial: size / PAGE });
            return new Uint8Array(m.buffer);
        } catch { /* fall through */ }
    }
    return new Uint8Array(size);
}

const CSR_MSTATUS = 0x300;
const CSR_MTVEC = 0x305;
const CSR_MEPC = 0x341;
const CSR_MCAUSE = 0x342;
const CSR_MTVAL = 0x343;

// 逐次実行できる計算機。UART の入力が尽きると 'waiting' で停止し，
// feed() で入力を足して run() を呼び直すと続きから走る (対話実行)。
export class Machine {
    // opts.ramSize: RAM の大きさ (既定 128 MiB)。kernel19 の配置は
    // 0xa000_0000 まで届くので 512 MiB を要る (docs/stage016-os.md 8.3)。
    // kernel25〜28 は 1 GiB，kernel29 は 2 GiB (docs/stage017-gcc.md 7.2 / 8.15)
    constructor(program, opts = {}) {
        this.ramSize = opts.ramSize || RAM_SIZE;
        this.mem = allocRam(this.ramSize);        // 新規確保 = ゼロ初期化 (QEMU と同じ)
        this.mem.set(program, 0);
        this.rtcHigh = 0;                         // TIME_LOW を読んだ瞬間の上位語
        // 時計は起動時の epoch を基点に，単調な performance.now() で進める。
        // Date.now() をそのまま返すとホストの時刻合わせで戻ることがあり，
        // kernel27 以降は連続する読みの差を取るので，戻ると times が溢れる
        this.rtcEpochNs = BigInt(Date.now()) * 1000000n;
        this.rtcStart = performance.now();
        this.dv = new DataView(this.mem.buffer);
        this.x = new Int32Array(32);
        this.pc = 0;                            // RAM_BASE からのオフセット
        this.priv = 3;                          // 3 = M / 0 = U
        this.csr = new Uint32Array(4096);
        this.input = [];
        this.inHead = 0;
        this.icount = 0;
        this.done = false;
        this.exitCode = -1;
        this.emptyPolls = 0;    // 入力が無い状態での連続 LSR ポーリング数
    }

    feed(bytes) {
        for (let i = 0; i < bytes.length; i++) this.input.push(bytes[i]);
        this.emptyPolls = 0;
    }

    get inputAvailable() { return this.inHead < this.input.length; }

    // 例外 (ecall / 不正命令) を M モードのトラップ入口へ渡す
    trap(cause) {
        this.csr[CSR_MEPC] = (RAM_BASE + this.pc) >>> 0;
        this.csr[CSR_MCAUSE] = cause;
        this.csr[CSR_MTVAL] = 0;
        // MPP <- 現在の特権，MPIE <- MIE，MIE <- 0
        const s = this.csr[CSR_MSTATUS];
        this.csr[CSR_MSTATUS] = (s & ~0x1888) | (this.priv << 11) | ((s & 8) << 4);
        this.priv = 3;
        this.pc = (this.csr[CSR_MTVEC] - RAM_BASE) | 0;
    }

    // budget 命令まで実行する。返り値:
    //   { status: 'exit'|'waiting'|'budget'|'trap', output: Uint8Array }
    //   'waiting' = UART 入力待ち。feed() 後に run() で再開する
    run(budget) {
        const { dv, x, csr, input } = this;
        const ramSize = this.ramSize;
        const out = [];
        const ret = (status) => ({ status, output: Uint8Array.from(out) });
        if (this.done) return ret('exit');
        let pc = this.pc;
        let executed = 0;

        while (executed < budget) {
            if (pc >>> 0 >= ramSize) { this.pc = pc; return ret('trap'); }
            const inst = dv.getUint32(pc, true);
            const opcode = inst & 0x7f;
            const rd = (inst >>> 7) & 0x1f;
            const rs1 = (inst >>> 15) & 0x1f;
            const rs2 = (inst >>> 20) & 0x1f;
            const f3 = (inst >>> 12) & 7;
            let nextPc = pc + 4;
            executed++;
            this.icount++;

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
                const t = (x[rs1] + (inst >> 20)) & ~1;
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
                default: this.pc = pc; return ret('trap');
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
                if (off < ramSize) {
                    switch (f3) {
                    case 0: v = dv.getInt8(off); break;
                    case 1: v = dv.getInt16(off, true); break;
                    case 2: v = dv.getInt32(off, true); break;
                    case 4: v = dv.getUint8(off); break;
                    case 5: v = dv.getUint16(off, true); break;
                    default: this.pc = pc; return ret('trap');
                    }
                } else if (addr === UART_BASE) {
                    v = this.inHead < input.length ? input[this.inHead++] : 0;
                    this.emptyPolls = 0;
                } else if (addr === RTC_LOW) {
                    // epoch からのナノ秒 (起動時の epoch + 単調な経過時間)
                    const ns = this.rtcEpochNs
                        + BigInt(Math.floor((performance.now() - this.rtcStart) * 1000000));
                    this.rtcHigh = Number(ns >> 32n) | 0;
                    v = Number(ns & 0xffffffffn) | 0;
                } else if (addr === RTC_HIGH) {
                    v = this.rtcHigh;
                } else if (addr === UART_BASE + 5) {
                    // LSR は getc (bit0 待ち) と putc (bit5 待ち) の両方が読む。
                    // 送信は常に可能なので，入力が無いまま LSR のポーリングが
                    // 続いたときだけ「getc の入力待ち」と判断して停止する。
                    // この命令は実行せずに止まり，feed() 後の run() で
                    // 同じ命令からやり直す
                    if (this.inHead < input.length) {
                        v = 0x61;               // Data Ready | THR Empty
                        this.emptyPolls = 0;
                    } else if (++this.emptyPolls > 64) {
                        this.pc = pc;
                        return ret('waiting');
                    } else {
                        v = 0x60;               // THR Empty のみ
                    }
                } else {
                    v = 0;
                }
                if (rd) x[rd] = v;
                break;
            }
            case 0x23: {                        // ストア
                const imm = ((inst >> 25) << 5) | ((inst >>> 7) & 0x1f);
                const addr = (x[rs1] + imm) >>> 0;
                const off = (addr - RAM_BASE) >>> 0;
                if (off < ramSize) {
                    switch (f3) {
                    case 0: dv.setUint8(off, x[rs2] & 0xff); break;
                    case 1: dv.setUint16(off, x[rs2] & 0xffff, true); break;
                    case 2: dv.setInt32(off, x[rs2], true); break;
                    default: this.pc = pc; return ret('trap');
                    }
                } else if (addr === UART_BASE) {
                    out.push(x[rs2] & 0xff);
                    this.emptyPolls = 0;
                } else if (addr === FINISHER) {
                    const v = x[rs2] >>> 0;
                    this.pc = pc;
                    if ((v & 0xffff) === 0x5555) {
                        this.done = true; this.exitCode = 0; return ret('exit');
                    }
                    if ((v & 0xffff) === 0x3333) {
                        this.done = true; this.exitCode = v >>> 16; return ret('exit');
                    }
                    return ret('trap');
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
                    case 1:
                        v = Number(BigInt.asIntN(32, (BigInt(a) * BigInt(b)) >> 32n));
                        break;
                    case 2:
                        v = Number(BigInt.asIntN(32, (BigInt(a) * BigInt(b >>> 0)) >> 32n));
                        break;
                    case 3:
                        v = Number(BigInt.asIntN(32,
                            (BigInt(a >>> 0) * BigInt(b >>> 0)) >> 32n));
                        break;
                    case 4:
                        v = b === 0 ? -1
                            : (a === -2147483648 && b === -1) ? a : (a / b) | 0;
                        break;
                    case 5:
                        v = b === 0 ? -1 : (((a >>> 0) / (b >>> 0)) >>> 0) | 0;
                        break;
                    case 6:
                        v = b === 0 ? a
                            : (a === -2147483648 && b === -1) ? 0 : (a % b) | 0;
                        break;
                    case 7:
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
            case 0x0f:                          // fence
                break;
            case 0x73: {                        // SYSTEM (ecall / mret / CSR)
                if (f3 === 0) {
                    if (inst === 0x00000073) {          // ecall
                        this.pc = pc;
                        this.trap(this.priv === 3 ? 11 : 8);
                        pc = this.pc;
                        continue;
                    }
                    if (inst === 0x00100073) {          // ebreak
                        this.pc = pc;
                        this.trap(3);
                        pc = this.pc;
                        continue;
                    }
                    if (inst === 0x30200073) {          // mret
                        if (this.priv !== 3) { this.pc = pc; return ret('trap'); }
                        const s = csr[CSR_MSTATUS];
                        this.priv = (s >>> 11) & 3;
                        // MIE <- MPIE, MPIE <- 1, MPP <- 0
                        csr[CSR_MSTATUS] = (s & ~0x1888) | ((s >>> 4) & 8) | 0x80;
                        nextPc = (csr[CSR_MEPC] - RAM_BASE) | 0;
                        break;
                    }
                    this.pc = pc;
                    return ret('trap');
                }
                // CSR 操作。U モードの M-CSR アクセスは不正命令として
                // カーネルのトラップ入口へ渡す
                if (this.priv !== 3) {
                    this.pc = pc; this.trap(2); pc = this.pc; continue;
                }
                const num = (inst >>> 20) & 0xfff;
                const old = csr[num];
                const src = (f3 & 4) ? rs1 : (x[rs1] >>> 0);   // 即値形は rs1 が値
                switch (f3 & 3) {
                case 1: csr[num] = src; break;                  // csrrw
                case 2: if (rs1) csr[num] = old | src; break;   // csrrs
                case 3: if (rs1) csr[num] = old & ~src; break;  // csrrc
                }
                if (rd) x[rd] = old | 0;
                break;
            }
            default:
                // 未知の命令。U モードならカーネルへ (不正命令 2)，
                // M モードなら致命
                if (this.priv !== 3) {
                    this.pc = pc; this.trap(2); pc = this.pc; continue;
                }
                this.pc = pc;
                return ret('trap');
            }
            pc = nextPc | 0;
        }
        this.pc = pc;
        return ret('budget');
    }
}

// フィルタ実行 (一括)。従来 API。
// 返り値: { status: 'exit'|'idle'|'budget'|'trap', exitCode, output, icount, pc }
//   'idle' = 入力を使い切って入力待ちになった (終端の付け忘れ等)
export function runFilter(program, input, opts = {}) {
    const budget = opts.budget ?? 4_000_000_000;
    const m = new Machine(program);
    m.feed(input);
    const chunks = [];
    let status;
    for (;;) {
        const r = m.run(budget - m.icount > 0 ? budget - m.icount : 0);
        chunks.push(r.output);
        if (r.status === 'waiting') { status = 'idle'; break; }
        if (r.status !== 'budget' || m.icount >= budget) {
            status = r.status;
            break;
        }
    }
    return {
        status,
        exitCode: m.exitCode,
        output: concatBytes(chunks),
        icount: m.icount,
        pc: (RAM_BASE + m.pc) >>> 0,
    };
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

// pp への入力 (バンドル) を組み立てる。形式は docs/stage009-pp.md 2.2
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
