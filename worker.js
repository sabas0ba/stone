// プレイグラウンドとターミナルの実行処理 (Web Worker)。
// - パイプライン: メインスレッドから受け取った計画を rv32.js で順に実行
// - ターミナル: kernel + sfs イメージで OS を起動し，UART を対話接続する
// バイナリ資産は fetch してキャッシュする。
import { Machine, runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';
import { packSfs, unpackSfs, SFS_OFFSET } from './sfs.js';
import { packSfs2, unpackSfs2 } from './sfs2.js';
import { packSfs3, unpackSfs3 } from './sfs3.js';
import { packSfs4, unpackSfs4 } from './sfs4.js';
import { ld18Input, runLd18 } from './ld18.js';

const cache = new Map();

async function fetchBytes(url) {
    if (cache.has(url)) return cache.get(url);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`fetch ${url}: ${res.status}`);
    const bytes = new Uint8Array(await res.arrayBuffer());
    cache.set(url, bytes);
    return bytes;
}

// step の入力を組み立てる。
//   {src:'user', term}                     利用者の入力テキスト
//   {src:'bundle', members:[...]}          stone-bundle (pp への入力)
//   {src:'prev', term, extra:[url...], prefix} 前段の出力 (+追加オブジェクト)
async function composeInput(step, prev, userBytes) {
    if (step.src === 'user') return withTerminator(userBytes, step.term);
    if (step.src === 'bundle') {
        const members = [];
        for (const m of step.members) {
            if (m.user) members.push({ name: m.name, data: userBytes });
            else members.push({ name: m.name, data: await fetchBytes(m.asset) });
        }
        return buildBundle(members);
    }
    if (step.src === 'prev') {
        const parts = [];
        if (step.prefix) parts.push(new TextEncoder().encode(step.prefix));
        parts.push(prev);
        for (const url of step.extra || []) parts.push(await fetchBytes(url));
        return withTerminator(concatBytes(parts), step.term);
    }
    throw new Error(`unknown src: ${step.src}`);
}

// パイプラインを最後まで実行し，各段の結果を progress で知らせる
async function runPipeline(steps, userBytes, progress) {
    let prev = null;
    for (let i = 0; i < steps.length; i++) {
        const step = steps[i];
        const bin = await fetchBytes(step.bin);
        const t = performance.now();
        let r;
        if (step.kind === 'ld18') {
            const libs = [];
            for (const url of step.lib || []) libs.push(await fetchBytes(url));
            const objs = [prev];
            for (const url of step.extra || []) objs.push(await fetchBytes(url));
            r = runLd18(bin, ld18Input(libs, objs, step.dstk));
        } else {
            const input = await composeInput(step, prev, userBytes);
            r = runFilter(bin, input);
        }
        if (progress) {
            progress(i, {
                status: r.status, exitCode: r.exitCode, icount: r.icount,
                ms: performance.now() - t, outLen: r.output.length,
            });
        }
        if (r.status !== 'exit' || r.exitCode !== 0) {
            return { ok: false, failedStep: i, exitCode: r.exitCode,
                status: r.status, output: r.output };
        }
        prev = r.output;
    }
    return { ok: true, output: prev };
}

async function handlePipeline(msg) {
    const { id, steps, userText, stdinText, runOutput } = msg;
    const enc = new TextEncoder();
    try {
        const r = await runPipeline(steps, enc.encode(userText), (i, info) => {
            postMessage({ id, kind: 'step', index: i, name: steps[i].name, ...info });
        });
        if (!r.ok) {
            postMessage({ id, kind: 'done', ok: false, failedStep: r.failedStep,
                exitCode: r.exitCode, status: r.status, output: r.output },
            [r.output.buffer]);
            return;
        }
        let ran = null;
        if (runOutput) {
            const t = performance.now();
            const x = runFilter(r.output, enc.encode(stdinText || ''));
            ran = { status: x.status, exitCode: x.exitCode, icount: x.icount,
                ms: performance.now() - t, output: x.output };
        }
        postMessage({ id, kind: 'done', ok: true, output: r.output, ran },
            [r.output.buffer, ...(ran ? [ran.output.buffer] : [])]);
    } catch (e) {
        postMessage({ id, kind: 'done', ok: false, error: String(e) });
    }
}

// ---- ターミナル (OS セッション) ----
// boot: sfs を構築してカーネルを起動し，UART を対話接続する。
// 実行は 3000 万命令ずつに分割し，合間に tin (キー入力) を受け付ける
const sessions = new Map();
const SLICE = 30_000_000;

// sfs の世代ごとの pack / unpack。sfs3 以降は項目が時刻を持つ
const FS = {
    1: { pack: (f, size, n) => packSfs(f, size), unpack: (img) =>
        unpackSfs(img).map((f) => ({ ...f, dir: false })) },
    2: { pack: packSfs2, unpack: (img) =>
        unpackSfs2(img).map((f) => ({ name: f.path, dir: f.dir, data: f.data })) },
    3: { pack: packSfs3, unpack: (img) =>
        unpackSfs3(img).map((f) => ({ name: f.path, dir: f.dir, data: f.data, mtime: f.mtime })) },
    4: { pack: packSfs4, unpack: (img) =>
        unpackSfs4(img).map((f) => ({ name: f.path, dir: f.dir, data: f.data, mtime: f.mtime })) },
};

async function prepareFile(f, imgFiles, progress) {
    const mtime = f.mtime == null ? undefined : BigInt(f.mtime);
    if (f.dir) {
        imgFiles.push({ name: f.name, dir: true, mtime });
    } else if (f.asset) {
        imgFiles.push({ name: f.name, data: await fetchBytes(f.asset), mtime });
    } else if (f.text != null) {
        imgFiles.push({ name: f.name, data: new TextEncoder().encode(f.text), mtime });
    } else if (f.build) {
        progress(`building ${f.name} in the emulator…`);
        const r = await runPipeline(f.build, new Uint8Array(0), null);
        if (!r.ok) {
            const why = r.output && r.output.length ? ` — ${new TextDecoder().decode(r.output).trim()}` : '';
            throw new Error(`build ${f.name}: ${f.build[r.failedStep].name} ${r.status} exit ${r.exitCode}${why}`);
        }
        imgFiles.push({ name: f.name, data: r.output, mtime });
    }
}

async function handleBoot(msg) {
    const { id, kernel, files, bootLine, imgSize, maxEntries, ramSize } = msg;
    const fs = msg.fs || 1;
    const sfsOffset = msg.sfsOffset || SFS_OFFSET;
    try {
        const imgFiles = [];
        const progress = (text) => postMessage({ id, kind: 'tstate', state: 'preparing', text });
        for (const f of files) await prepareFile(f, imgFiles, progress);
        imgFiles.push({ name: 'boot', data: new TextEncoder().encode(bootLine) });
        for (const [name, text] of Object.entries(msg.extraFiles || {})) {
            imgFiles.push({ name, data: new TextEncoder().encode(text) });
        }
        const size = imgSize || (4 << 20);
        const img = FS[fs].pack(imgFiles, size, maxEntries || (fs === 1 ? 128 : 256));
        // 大きな RAM (kernel19 の 512 MB，kernel29 の 2 GiB) は確保に失敗しうるので
        // 分けて確保する。rv32.js は WebAssembly.Memory で予約し，触れた分だけ使う
        let m;
        try {
            m = new Machine(await fetchBytes(kernel), { ramSize });
        } catch (e) {
            throw new Error(`could not allocate ${Math.round((ramSize || 0) / (1 << 20))} MB `
                + `of emulator memory in this browser (${e})`);
        }
        m.mem.set(img, sfsOffset);
        sessions.set(id, { m, size, fs, sfsOffset, pumping: false });
        postMessage({ id, kind: 'tstate', state: 'running', icount: 0 });
        pump(id);
    } catch (e) {
        postMessage({ id, kind: 'tstate', state: 'error', error: String(e) });
    }
}

async function pump(id) {
    const s = sessions.get(id);
    if (!s || s.pumping) return;
    s.pumping = true;
    try {
        for (;;) {
            if (!sessions.has(id)) return;
            const r = s.m.run(SLICE);
            if (r.output.length) {
                postMessage({ id, kind: 'tout', data: r.output }, [r.output.buffer]);
            }
            if (r.status === 'budget') {
                // 入力メッセージを取り込むために一度制御を返す
                await new Promise((res) => setTimeout(res));
                continue;
            }
            if (r.status === 'waiting') {
                postMessage({ id, kind: 'tstate', state: 'waiting',
                    icount: s.m.icount });
                return;
            }
            // exit / trap
            postMessage({ id, kind: 'tstate',
                state: r.status === 'exit' ? 'exited' : 'crashed',
                exitCode: s.m.exitCode, icount: s.m.icount });
            const img = s.m.mem.slice(s.sfsOffset, s.sfsOffset + s.size);
            const out = FS[s.fs].unpack(img).filter((f) => f.name !== 'boot');
            postMessage({ id, kind: 'tfiles', files: out },
                out.map((f) => f.data.buffer));
            sessions.delete(id);
            return;
        }
    } finally {
        s.pumping = false;
    }
}

self.onmessage = (ev) => {
    const m = ev.data;
    if (m.kind === 'boot') { handleBoot(m); return; }
    if (m.kind === 'tin') {
        const s = sessions.get(m.id);
        if (s) { s.m.feed(m.data); pump(m.id); }
        return;
    }
    if (m.kind === 'tkill') { sessions.delete(m.id); return; }
    handlePipeline(m);
};
