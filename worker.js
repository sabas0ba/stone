// プレイグラウンドとターミナルの実行係 (Web Worker)。
// - パイプライン: メインスレッドから受け取った計画を rv32.js で順に実行
// - ターミナル: kernel + sfs イメージで OS を起動し，UART を対話接続する
// バイナリ資産は fetch してキャッシュする。
import { Machine, runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';
import { packSfs, unpackSfs, SFS_OFFSET } from './sfs.js';
import { packSfs2, unpackSfs2 } from './sfs2.js';

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
        const input = await composeInput(step, prev, userBytes);
        const t = performance.now();
        const r = runFilter(bin, input);
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
// boot: sfs を組んでカーネルを起動し，UART を対話接続する。
// 実行は 3000 万命令ずつに刻み，合間に tin (キー入力) を受け付ける
const sessions = new Map();
const SLICE = 30_000_000;

async function prepareFile(f, imgFiles) {
    if (f.asset) {
        imgFiles.push({ name: f.name, data: await fetchBytes(f.asset) });
    } else if (f.text != null) {
        imgFiles.push({ name: f.name, data: new TextEncoder().encode(f.text) });
    } else if (f.build) {
        const r = await runPipeline(f.build, new Uint8Array(0), null);
        if (!r.ok) throw new Error(`build ${f.name}: step ${r.failedStep} exit ${r.exitCode}`);
        imgFiles.push({ name: f.name, data: r.output });
    }
}

async function handleBoot(msg) {
    const { id, kernel, files, bootLine, imgSize, maxEntries, ramSize } = msg;
    const fs = msg.fs || 1;
    try {
        const imgFiles = [];
        for (const f of files) await prepareFile(f, imgFiles);
        imgFiles.push({ name: 'boot', data: new TextEncoder().encode(bootLine) });
        const size = imgSize || (4 << 20);
        const img = fs === 2
            ? packSfs2(imgFiles, size, maxEntries || 256)
            : packSfs(imgFiles, size, maxEntries || 128);
        // 大きな RAM (kernel19 の 512 MB) は確保に失敗しうるので分けて掴む
        let m;
        try {
            m = new Machine(await fetchBytes(kernel), { ramSize });
        } catch (e) {
            throw new Error(`could not allocate ${Math.round((ramSize || 0) / (1 << 20))} MB `
                + `of emulator memory in this browser (${e})`);
        }
        m.mem.set(img, SFS_OFFSET);
        sessions.set(id, { m, size, fs, pumping: false });
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
                // 入力メッセージを取り込むために一度譲る
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
            const img = s.m.mem.slice(SFS_OFFSET, SFS_OFFSET + s.size);
            const out = (s.fs === 2
                ? unpackSfs2(img).map((f) => ({ name: f.path, dir: f.dir, data: f.data }))
                : unpackSfs(img).map((f) => ({ ...f, dir: false })))
                .filter((f) => f.name !== 'boot');
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
