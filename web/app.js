// stone — bootstrap の系譜 (メイン)。
// data/stages.json (世代コンテンツ + ビルド時に付与した成果物メタ) を読み，
// 世代スライダーと各パネルを描画する。プレイグラウンドは worker.js が実行する。
import { PIPELINES } from './pipelines.js';

const REPO = 'https://github.com/sabas0ba/stone';
const $ = (id) => document.getElementById(id);

// Stage 番号 → Phase 帯 (docs/plan.md, docs/roadmap.md の区分)
const PHASES = [
    { from: 1, to: 7, label: '前半計画 (seed → セルフホスト → 最適化)', cls: 'seed', color: 'var(--phase-seed)' },
    { from: 8, to: 10, label: 'Phase A: 言語と道具立て', cls: 'a', color: 'var(--phase-a)' },
    { from: 11, to: 13, label: 'Phase B: 自立した開発環境', cls: 'b', color: 'var(--phase-b)' },
    { from: 14, to: 14, label: 'Phase C: 外部資産', cls: 'c', color: 'var(--phase-c)' },
];

let DATA = null;
let current = 1;
const worker = new Worker('worker.js', { type: 'module' });
let runId = 0;

// ---- 最小 markdown 描画 (見出し・強調・code・リスト・表・整形済み) ----
const escapeHtml = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');

function inlineMd(s) {
    return escapeHtml(s)
        .replace(/`([^`]+)`/g, (_, c) => `<code>${c}</code>`)
        .replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>')
        .replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_, t, u) => {
            const url = /^https?:/.test(u) ? u : `${REPO}/blob/main/${u}`;
            return `<a href="${url}" rel="noopener">${t}</a>`;
        });
}

function renderMd(src) {
    const lines = src.split('\n');
    const out = [];
    let i = 0;
    while (i < lines.length) {
        const ln = lines[i];
        if (ln.startsWith('```')) {                             // 整形済み
            i++;
            const buf = [];
            while (i < lines.length && !lines[i].startsWith('```')) buf.push(lines[i++]);
            i++;
            out.push(`<pre><code>${escapeHtml(buf.join('\n'))}</code></pre>`);
            continue;
        }
        if (/^#{1,4} /.test(ln)) {                              // 見出し
            const lv = ln.match(/^#+/)[0].length;
            out.push(`<h${lv}>${inlineMd(ln.replace(/^#+ /, ''))}</h${lv}>`);
            i++;
            continue;
        }
        if (ln.startsWith('|')) {                               // 表
            const rows = [];
            while (i < lines.length && lines[i].startsWith('|')) rows.push(lines[i++]);
            const isSep = (r) => /^\|[\s:|-]+\|$/.test(r);
            const cells = (r) => r.replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
            const hasHead = rows.length > 1 && isSep(rows[1]);
            let html = '<table>';
            rows.forEach((r, k) => {
                if (isSep(r)) return;
                const tag = hasHead && k === 0 ? 'th' : 'td';
                html += `<tr>${cells(r).map((c) => `<${tag}>${inlineMd(c)}</${tag}>`).join('')}</tr>`;
            });
            out.push(html + '</table>');
            continue;
        }
        if (/^[-*] /.test(ln)) {                                // 箇条書き
            const items = [];
            while (i < lines.length && /^[-*] /.test(lines[i])) {
                items.push(`<li>${inlineMd(lines[i].slice(2))}</li>`);
                i++;
            }
            out.push(`<ul>${items.join('')}</ul>`);
            continue;
        }
        if (ln.trim() === '') { i++; continue; }
        const buf = [];                                         // 段落
        while (i < lines.length && lines[i].trim() !== ''
            && !/^(#{1,4} |[-*] |\||```)/.test(lines[i])) buf.push(lines[i++]);
        out.push(`<p>${inlineMd(buf.join('\n'))}</p>`);
    }
    return out.join('\n');
}

// ---- 補助 ----
const fmtSize = (n) => n >= 1024 * 1024
    ? `${(n / 1024 / 1024).toFixed(1)} MiB`
    : n >= 1024 ? `${(n / 1024).toFixed(1)} KiB` : `${n} B`;
const fmtNum = (n) => n.toLocaleString('en-US');

function hexdump(bytes, limit = 512) {
    const n = Math.min(bytes.length, limit);
    const lines = [];
    for (let o = 0; o < n; o += 16) {
        const row = [...bytes.slice(o, o + 16)];
        const hex = row.map((b) => b.toString(16).padStart(2, '0')).join(' ');
        const asc = row.map((b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : '·')).join('');
        lines.push(`${o.toString(16).padStart(6, '0')}  ${hex.padEnd(47)}  ${asc}`);
    }
    if (bytes.length > limit) lines.push(`… (全 ${fmtNum(bytes.length)} バイト)`);
    return lines.join('\n');
}

const looksText = (bytes) => {
    let printable = 0;
    const n = Math.min(bytes.length, 2000);
    for (let i = 0; i < n; i++) {
        const b = bytes[i];
        if (b === 9 || b === 10 || b === 13 || (b >= 0x20 && b < 0x7f) || b >= 0x80) printable++;
    }
    return n === 0 || printable / n > 0.95;
};

// ---- タイムライン ----
function buildTimeline() {
    const phases = $('tl-phases');
    for (const p of PHASES) {
        const span = document.createElement('span');
        span.style.flex = String(p.to - p.from + 1);
        span.style.borderTopColor = p.color;
        span.textContent = p.label;
        phases.appendChild(span);
    }
    const nodes = $('tl-nodes');
    for (const st of DATA.stages) {
        const b = document.createElement('button');
        b.className = 'tl-node';
        b.id = `node-${st.num}`;
        b.innerHTML = `${st.num}<small>${escapeHtml(st.name)}</small>`;
        b.onclick = () => select(st.num);
        nodes.appendChild(b);
    }
    $('tl-range').oninput = (e) => select(Number(e.target.value));
    $('tl-prev').onclick = () => select(Math.max(1, current - 1));
    $('tl-next').onclick = () => select(Math.min(14, current + 1));
    document.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'TEXTAREA' || e.target.tagName === 'INPUT') return;
        if (e.key === 'ArrowLeft') select(Math.max(1, current - 1));
        if (e.key === 'ArrowRight') select(Math.min(14, current + 1));
    });
}

// ---- 推移チャート (主成果物のサイズ，対数目盛) ----
function buildChart() {
    const stages = DATA.stages;
    // 各世代の最大の成果物 (その世代の規模の代表として)
    const mains = stages.map((s) => s.artifacts.reduce(
        (a, b) => ((b.size || 0) > (a.size || 0) ? b : a), s.artifacts[0] || {}));
    const sizes = mains.map((a) => a.size || 1);
    const maxLog = Math.max(...sizes.map((v) => Math.log10(v)));
    const W = 1040, H = 130, bw = W / stages.length;
    let svg = `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" aria-label="成果物サイズの推移">`;
    stages.forEach((st, i) => {
        const h = Math.max(4, (Math.log10(sizes[i]) / maxLog) * 84);
        const x = i * bw + 4, y = 104 - h;
        svg += `<rect class="evo-bar" data-num="${st.num}" x="${x}" y="${y}"`
            + ` width="${bw - 8}" height="${h}" rx="3"><title>${mains[i].file || st.name}: ${fmtSize(sizes[i])}</title></rect>`;
        svg += `<text class="evo-size" x="${x + (bw - 8) / 2}" y="${y - 5}" text-anchor="middle">${fmtSize(sizes[i])}</text>`;
        svg += `<text class="evo-label" x="${x + (bw - 8) / 2}" y="${H - 8}" text-anchor="middle">${st.num}</text>`;
    });
    svg += '</svg>';
    $('evo-chart').innerHTML = svg;
    $('evo-chart').querySelectorAll('.evo-bar').forEach((r) => {
        r.addEventListener('click', () => select(Number(r.dataset.num)));
    });
}

// ---- Stage パネル ----
function select(num, push = true) {
    current = num;
    const st = DATA.stages[num - 1];
    if (push) history.replaceState(null, '', `#${st.id}`);
    $('tl-range').value = String(num);
    document.querySelectorAll('.tl-node').forEach((n) => n.classList.remove('active'));
    $(`node-${num}`).classList.add('active');
    document.querySelectorAll('.evo-bar').forEach((r) => {
        r.classList.toggle('active', Number(r.dataset.num) === num);
    });

    $('st-num').textContent = String(num).padStart(2, '0');
    $('st-title').textContent = st.title;
    $('st-tagline').textContent = st.tagline;
    $('st-meta').innerHTML = [
        ['読む言語', st.lang_in],
        ['実装言語', st.lang_impl],
        ['ビルド', st.built_by],
    ].map(([k, v]) => `<span>${k}: <b>${escapeHtml(v)}</b></span>`).join('');

    $('st-summary').innerHTML = renderMd(st.summary_md);
    $('st-caps').innerHTML = st.new_capabilities
        .map((c) => `<li>${inlineMd(c)}</li>`).join('');
    $('st-trivia-box').hidden = !st.trivia;
    if (st.trivia) $('st-trivia').innerHTML = inlineMd(st.trivia);

    $('st-artifacts').innerHTML = st.artifacts.map((a) => `
        <tr><td class="af-file"><a href="assets/bin/${a.file}" download>${a.file}</a></td>
            <td class="af-size">${a.size != null ? fmtSize(a.size) : '—'}</td></tr>
        <tr><td class="af-desc" colspan="2">${escapeHtml(a.desc)}${
    a.sha256 ? ` <span class="af-sha">sha256:${a.sha256.slice(0, 12)}…</span>` : ''}</td></tr>
    `).join('');

    $('st-sources').innerHTML = '';
    for (const s of st.sources) {
        const li = document.createElement('li');
        if (s.path.endsWith('.bin')) {           // バイナリはビューアで開かない
            const a = document.createElement('a');
            a.href = `files/${s.path}`;
            a.download = '';
            a.textContent = s.path;
            li.appendChild(a);
        } else {
            const b = document.createElement('button');
            b.textContent = s.path;
            b.onclick = () => showSource(s.path);
            li.appendChild(b);
        }
        const d = document.createElement('span');
        d.className = 'src-desc';
        d.textContent = s.desc;
        li.appendChild(d);
        $('st-sources').appendChild(li);
    }

    $('st-docs').innerHTML = st.docs.map((d) =>
        `<li><a href="${REPO}/blob/main/${d}" rel="noopener">${d}</a></li>`).join('');

    setupPlayground(st);
}

async function showSource(path) {
    $('src-name').textContent = path;
    $('src-body').textContent = '読み込み中…';
    $('src-dialog').showModal();
    try {
        const res = await fetch(`files/${path}`);
        if (res.ok) {
            $('src-body').textContent = await res.text();
        } else {
            // ディレクトリ等はサイト内で開けない。リポジトリへ誘導する
            $('src-body').innerHTML = 'このパスはサイト内で開けない。'
                + `<a href="${REPO}/tree/main/${escapeHtml(path)}" rel="noopener">リポジトリで見る</a>`;
        }
    } catch (e) {
        $('src-body').textContent = String(e);
    }
}

// ---- プレイグラウンド ----
let pgConfig = null;

async function setupPlayground(st) {
    pgConfig = PIPELINES[st.id] || null;
    $('pg-result').hidden = true;
    $('pg-steps').textContent = '';
    const none = $('pg-none');
    if (!pgConfig) {
        $('pg-body').hidden = true;
        none.hidden = false;
        none.textContent = st.num === 12 || st.num === 13
            ? 'この世代の成果物は OS (カーネルと ELF 実行形式) であり，UART フィルタ型ではないためプレイグラウンドの対象外。設計文書と成果物のダウンロードを参照。'
            : 'この世代にはプレイグラウンドが無い。';
        return;
    }
    $('pg-body').hidden = false;
    none.hidden = true;
    $('pg-note').innerHTML = pgConfig.note ? inlineMd(pgConfig.note) : '';
    $('pg-input-label').textContent = pgConfig.inputLabel || '';
    $('pg-stdin-box').hidden = !pgConfig.run;
    $('pg-stdin').value = pgConfig.stdin || '';
    $('pg-input').value = '読み込み中…';
    try {
        const res = await fetch(pgConfig.sample);
        $('pg-input').value = res.ok ? await res.text() : '';
    } catch { $('pg-input').value = ''; }
}

$('pg-run').onclick = () => {
    if (!pgConfig) return;
    const id = ++runId;
    const btn = $('pg-run');
    btn.disabled = true;
    const stepsEl = $('pg-steps');
    stepsEl.innerHTML = pgConfig.steps
        .map((s, i) => `<span id="pgs-${i}">${s.name}</span>`)
        .join(' → ');
    $('pg-result').hidden = true;

    worker.onmessage = (ev) => {
        const m = ev.data;
        if (m.id !== id) return;
        if (m.kind === 'step') {
            const el = $(`pgs-${m.index}`);
            const ok = m.status === 'exit' && m.exitCode === 0;
            el.className = ok ? 'st-ok' : 'st-err';
            el.textContent = `${pgConfig.steps[m.index].name}`
                + (ok ? ` ✓ (${fmtNum(m.icount)} 命令, ${m.ms.toFixed(0)}ms)`
                    : ` ✗ (終了コード ${m.exitCode})`);
            return;
        }
        btn.disabled = false;
        $('pg-result').hidden = false;
        if (!m.ok) {
            $('pg-out-head').innerHTML = m.error
                ? `<span class="err">エラー: ${escapeHtml(m.error)}</span>`
                : `<span class="err">失敗: ${pgConfig.steps[m.failedStep].name} が終了コード ${m.exitCode}`
                  + `${m.status === 'budget' ? ' (命令数上限。終端の付け忘れ?)' : ''}</span>`;
            $('pg-out').textContent = m.output && m.output.length
                ? new TextDecoder().decode(m.output) : '(出力なし)';
            $('pg-ran').hidden = true;
            return;
        }
        const out = m.output;
        if (pgConfig.output === 'text') {
            const stripped = out.length && out[out.length - 1] === 4
                ? out.slice(0, -1) : out;
            $('pg-out-head').innerHTML = `<span class="ok">✓</span> 出力 (テキスト, ${fmtNum(stripped.length)} バイト)`;
            $('pg-out').textContent = new TextDecoder().decode(stripped);
            $('pg-ran').hidden = true;
        } else {
            $('pg-out-head').innerHTML = `<span class="ok">✓</span> 出力バイナリ (${fmtNum(out.length)} バイト)`;
            $('pg-out').textContent = hexdump(out);
            if (m.ran) {
                $('pg-ran').hidden = false;
                const r = m.ran;
                const okRun = r.status === 'exit';
                $('pg-ran-head').innerHTML = (okRun
                    ? `<span class="${r.exitCode === 0 ? 'ok' : 'err'}">▶ 実行: 終了コード ${r.exitCode}</span>`
                    : `<span class="err">▶ 実行: ${r.status}</span>`)
                    + ` (${fmtNum(r.icount)} 命令, ${r.ms.toFixed(0)}ms)`;
                $('pg-ran-out').textContent = r.output.length === 0 ? '(出力なし)'
                    : looksText(r.output) ? new TextDecoder().decode(r.output)
                        : hexdump(r.output);
            } else {
                $('pg-ran').hidden = true;
            }
        }
    };

    worker.postMessage({
        id,
        steps: pgConfig.steps,
        userText: $('pg-input').value,
        stdinText: $('pg-stdin').value,
        runOutput: Boolean(pgConfig.run),
    });
};

$('src-close').onclick = () => $('src-dialog').close();
$('src-dialog').onclick = (e) => {
    if (e.target === $('src-dialog')) $('src-dialog').close();
};

// ---- 起動 ----
async function main() {
    const res = await fetch('data/stages.json');
    DATA = await res.json();
    $('repo-link').href = REPO;
    if (DATA.commit) {
        $('build-info').textContent = `成果物: ${DATA.commit.slice(0, 9)} のチェーンから生成`;
    }
    buildTimeline();
    buildChart();
    const m = location.hash.match(/^#stage(\d{3})$/);
    select(m ? Number(m[1]) : 1, false);
}

main();
