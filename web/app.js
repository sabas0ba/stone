// stone — bootstrap の系譜 (メイン)。
// data/stages.json (世代コンテンツ + ビルド時に付与した成果物メタ) を読み，
// 世代スライダーと各パネルを描画する。プレイグラウンドは worker.js が実行する。
import { PIPELINES } from './pipelines.js';

const REPO = 'https://github.com/sabas0ba/stone';
const $ = (id) => document.getElementById(id);

// Stage 番号 → Phase 帯 (docs/plan.md, docs/roadmap.md の区分)
const PHASES = [
    { from: 1, to: 7, label: 'Foundations (seed → self-host → optimizer)', cls: 'seed', color: 'var(--phase-seed)' },
    { from: 8, to: 10, label: 'Phase A: language & toolchain', cls: 'a', color: 'var(--phase-a)' },
    { from: 11, to: 13, label: 'Phase B: self-sufficient environment', cls: 'b', color: 'var(--phase-b)' },
    { from: 14, to: 14, label: 'Phase C: external code', cls: 'c', color: 'var(--phase-c)' },
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
    if (bytes.length > limit) lines.push(`… (${fmtNum(bytes.length)} bytes total)`);
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
        b.onclick = () => { stopAuto(); select(st.num); };
        nodes.appendChild(b);
    }
    $('tl-range').oninput = (e) => { stopAuto(); select(Number(e.target.value)); };
    $('tl-prev').onclick = () => { stopAuto(); select(Math.max(1, current - 1)); };
    $('tl-next').onclick = () => { stopAuto(); select(Math.min(14, current + 1)); };
    $('tl-play').onclick = () => (autoTimer ? stopAuto() : startAuto());
    document.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'TEXTAREA' || e.target.tagName === 'INPUT') return;
        if (e.key === 'ArrowLeft') { stopAuto(); select(Math.max(1, current - 1)); }
        if (e.key === 'ArrowRight') { stopAuto(); select(Math.min(14, current + 1)); }
    });
}

// ---- 自動スクロール (世代を一定間隔で進める) ----
let autoTimer = null;

function startAuto() {
    autoTimer = setInterval(() => select(current >= 14 ? 1 : current + 1), 7000);
    $('tl-play').classList.add('playing');
    $('tl-play').textContent = '⏸ auto';
    select(current >= 14 ? 1 : current + 1);
}

function stopAuto() {
    if (!autoTimer) return;
    clearInterval(autoTimer);
    autoTimer = null;
    $('tl-play').classList.remove('playing');
    $('tl-play').textContent = '▶ auto';
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
    let svg = `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" aria-label="Artifact size over generations">`;
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
        r.addEventListener('click', () => { stopAuto(); select(Number(r.dataset.num)); });
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
        ['reads', st.lang_in],
        ['written in', st.lang_impl],
        ['built by', st.built_by],
    ].map(([k, v]) => `<span>${k}: <b>${escapeHtml(v)}</b></span>`).join('');

    $('st-summary').innerHTML = renderMd(st.summary_md);
    $('st-caps').innerHTML = st.new_capabilities
        .map((c) => `<li>${inlineMd(c)}</li>`).join('');
    $('st-trivia-box').hidden = !st.trivia;
    if (st.trivia) $('st-trivia').innerHTML = inlineMd(st.trivia);

    renderCoverage(st);

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

// ---- カバレッジ (処理系 / OS としての完成度) ----
// stage.coverage: [{title, note?, items: [{label, state: ok|gap|bad, note?}]}]
function renderCoverage(st) {
    const box = $('st-coverage-box');
    const groups = st.coverage || [];
    box.hidden = groups.length === 0;
    if (!groups.length) return;
    $('st-coverage').innerHTML = groups.map((g) => {
        const n = { ok: 0, gap: 0, bad: 0 };
        for (const it of g.items) n[it.state] = (n[it.state] || 0) + 1;
        const total = g.items.length;
        const bar = ['ok', 'gap', 'bad']
            .filter((k) => n[k])
            .map((k) => `<span class="${k}" style="width:${(n[k] / total) * 100}%"></span>`)
            .join('');
        const stats = [
            n.ok ? `<span class="ok">${n.ok} ok</span>` : '',
            n.gap ? `<span class="gap">${n.gap} gap</span>` : '',
            n.bad ? `<span class="bad">${n.bad} bad</span>` : '',
        ].filter(Boolean).join(' / ');
        const chips = g.items.map((it) =>
            `<span class="cov-chip ${it.state}"${it.note ? ` title="${escapeHtml(it.note)}"` : ''}>${escapeHtml(it.label)}</span>`).join('');
        return `<div class="cov-group">
            <div class="cov-title">${inlineMd(g.title)}</div>
            <div class="cov-stats">${stats} — ${total} items${g.note ? ` · ${inlineMd(g.note)}` : ''}</div>
            <div class="cov-bar">${bar}</div>
            <div class="cov-chips">${chips}</div>
        </div>`;
    }).join('');
}

async function showSource(path) {
    $('src-name').textContent = path;
    $('src-body').textContent = 'Loading…';
    $('src-dialog').showModal();
    try {
        const res = await fetch(`files/${path}`);
        if (res.ok) {
            $('src-body').textContent = await res.text();
        } else {
            // ディレクトリ等はサイト内で開けない。リポジトリへ誘導する
            $('src-body').innerHTML = 'This path cannot be opened on the site. '
                + `<a href="${REPO}/tree/main/${escapeHtml(path)}" rel="noopener">View it in the repository</a>`;
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
            ? 'The artifacts of this generation form an OS (a kernel plus ELF executables), not a UART filter, so they are outside the playground\'s scope. See the design documents and artifact downloads instead.'
            : 'This generation has no playground.';
        return;
    }
    $('pg-body').hidden = false;
    none.hidden = true;
    $('pg-note').innerHTML = pgConfig.note ? inlineMd(pgConfig.note) : '';
    $('pg-input-label').textContent = pgConfig.inputLabel || '';
    $('pg-stdin-box').hidden = !pgConfig.run;
    $('pg-stdin').value = pgConfig.stdin || '';
    $('pg-input').value = 'Loading…';
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
                + (ok ? ` ✓ (${fmtNum(m.icount)} instructions, ${m.ms.toFixed(0)}ms)`
                    : ` ✗ (exit code ${m.exitCode})`);
            return;
        }
        btn.disabled = false;
        $('pg-result').hidden = false;
        if (!m.ok) {
            $('pg-out-head').innerHTML = m.error
                ? `<span class="err">Error: ${escapeHtml(m.error)}</span>`
                : `<span class="err">Failed: ${pgConfig.steps[m.failedStep].name} exited with code ${m.exitCode}`
                  + `${m.status === 'budget' ? ' (instruction budget exhausted — missing terminator?)' : ''}</span>`;
            $('pg-out').textContent = m.output && m.output.length
                ? new TextDecoder().decode(m.output) : '(no output)';
            $('pg-ran').hidden = true;
            return;
        }
        const out = m.output;
        if (pgConfig.output === 'text') {
            const stripped = out.length && out[out.length - 1] === 4
                ? out.slice(0, -1) : out;
            $('pg-out-head').innerHTML = `<span class="ok">✓</span> output (text, ${fmtNum(stripped.length)} bytes)`;
            $('pg-out').textContent = new TextDecoder().decode(stripped);
            $('pg-ran').hidden = true;
        } else {
            $('pg-out-head').innerHTML = `<span class="ok">✓</span> output binary (${fmtNum(out.length)} bytes)`;
            $('pg-out').textContent = hexdump(out);
            if (m.ran) {
                $('pg-ran').hidden = false;
                const r = m.ran;
                const okRun = r.status === 'exit';
                $('pg-ran-head').innerHTML = (okRun
                    ? `<span class="${r.exitCode === 0 ? 'ok' : 'err'}">▶ run: exit code ${r.exitCode}</span>`
                    : `<span class="err">▶ run: ${r.status}</span>`)
                    + ` (${fmtNum(r.icount)} instructions, ${r.ms.toFixed(0)}ms)`;
                $('pg-ran-out').textContent = r.output.length === 0 ? '(no output)'
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
        $('build-info').textContent = `artifacts built from chain @ ${DATA.commit.slice(0, 9)}`;
    }
    buildTimeline();
    buildChart();
    const m = location.hash.match(/^#stage(\d{3})$/);
    select(m ? Number(m[1]) : 1, false);
}

main();
