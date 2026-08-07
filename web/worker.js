// プレイグラウンドの実行係 (Web Worker)。
// メインスレッドから受け取ったパイプライン計画を rv32.js で順に実行し，
// 各段の結果を返す。バイナリ資産は fetch してキャッシュする。
import { runFilter, withTerminator, concatBytes, buildBundle } from './rv32.js';

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

self.onmessage = async (ev) => {
    const { id, steps, userText, stdinText, runOutput } = ev.data;
    const enc = new TextEncoder();
    const userBytes = enc.encode(userText);
    let prev = null;
    try {
        for (let i = 0; i < steps.length; i++) {
            const step = steps[i];
            const bin = await fetchBytes(step.bin);
            const input = await composeInput(step, prev, userBytes);
            const t = performance.now();
            const r = runFilter(bin, input);
            postMessage({
                id, kind: 'step', index: i, name: step.name,
                status: r.status, exitCode: r.exitCode, icount: r.icount,
                ms: performance.now() - t, outLen: r.output.length,
            });
            if (r.status !== 'exit' || r.exitCode !== 0) {
                postMessage({ id, kind: 'done', ok: false, failedStep: i,
                    exitCode: r.exitCode, status: r.status,
                    output: r.output }, [r.output.buffer]);
                return;
            }
            prev = r.output;
        }
        let ran = null;
        if (runOutput) {
            const t = performance.now();
            const r = runFilter(prev, enc.encode(stdinText || ''));
            ran = {
                status: r.status, exitCode: r.exitCode, icount: r.icount,
                ms: performance.now() - t, output: r.output,
            };
        }
        postMessage({ id, kind: 'done', ok: true, output: prev, ran },
            [prev.buffer, ...(ran ? [ran.output.buffer] : [])]);
    } catch (e) {
        postMessage({ id, kind: 'done', ok: false, error: String(e) });
    }
};
