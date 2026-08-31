// stone — bootstrap の系譜 (メイン)。
// data/stages.json (世代コンテンツ + ビルド時に付与した成果物メタ) を読み，
// 世代スライダーと各パネルを描画する。プレイグラウンドは worker.js が実行する。
import { PIPELINES, TERMINALS } from './pipelines.js';

const REPO = 'https://github.com/sabas0ba/stone';
const $ = (id) => document.getElementById(id);

// Stage 番号 → Phase 帯 (docs/plan.md, docs/roadmap.md の区分)
const PHASES = [
    { from: 1, to: 7, label: 'Foundations (seed → self-host → optimizer)', cls: 'seed', color: 'var(--phase-seed)' },
    { from: 8, to: 10, label: 'Phase A: language & toolchain', cls: 'a', color: 'var(--phase-a)' },
    { from: 11, to: 13, label: 'Phase B: self-sufficient environment', cls: 'b', color: 'var(--phase-b)' },
    { from: 14, to: 16, label: 'Phase C: external code & OS', cls: 'c', color: 'var(--phase-c)' },
];

let DATA = null;
let current = 1;
const lastStage = () => (DATA ? DATA.stages.length : 1);
const worker = new Worker('worker.js', { type: 'module' });
let runId = 0;

// worker からの応答は実行 id で振り分ける (プレイグラウンドとターミナルが共用)
const handlers = new Map();
worker.onmessage = (ev) => {
    const h = handlers.get(ev.data.id);
    if (h) h(ev.data);
};

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
    $('tl-range').max = String(DATA.stages.length);
    $('tl-range').oninput = (e) => { stopAuto(); select(Number(e.target.value)); };
    $('tl-prev').onclick = () => { stopAuto(); select(Math.max(1, current - 1)); };
    $('tl-next').onclick = () => { stopAuto(); select(Math.min(lastStage(), current + 1)); };
    $('tl-play').onclick = () => (autoTimer ? stopAuto() : startAuto());
    document.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'TEXTAREA' || e.target.tagName === 'INPUT') return;
        if (e.key === 'ArrowLeft') { stopAuto(); select(Math.max(1, current - 1)); }
        if (e.key === 'ArrowRight') { stopAuto(); select(Math.min(lastStage(), current + 1)); }
    });
}

// ---- 自動スクロール (世代を一定間隔で進める) ----
let autoTimer = null;

function startAuto() {
    autoTimer = setInterval(() => select(current >= lastStage() ? 1 : current + 1), 7000);
    $('tl-play').classList.add('playing');
    $('tl-play').textContent = '⏸ auto';
    select(current >= lastStage() ? 1 : current + 1);
}

function stopAuto() {
    if (!autoTimer) return;
    clearInterval(autoTimer);
    autoTimer = null;
    $('tl-play').classList.remove('playing');
    $('tl-play').textContent = '▶ auto';
}

// ---- 能力の推移 + 成果物の大きさ (横軸が世代のものを 1 枚にまとめる) ----
// data/stages.json の tracks を「系統 x 世代」の格子にする。世代ごとの
// 新機能はチップで読めるが，**積み上がっている**ことは 1 世代だけを見ても
// 分からない。成果物の大きさも横軸が同じなので同じ格子の 1 行に置く。
// 系統の行は世代 (kernel17 / cc15p など) へ開ける。
const openTracks = new Set();
let detailKey = null;           // 明細を出している "track|sub|stage"
let mainArtifacts = [];         // 世代ごとの最大の成果物 (大きさの行と読み取りが使う)

const trackCount = (t, num) => t.features.filter((f) => f.stage === num).length;

// その系統に現れる世代を，初出の順に
function subsOf(t) {
    const out = [];
    for (const f of t.features) if (f.sub && !out.includes(f.sub)) out.push(f.sub);
    return out;
}

function cells(feats, n, kind, key) {
    let html = '';
    for (let i = 1; i <= n; i++) {
        const c = feats.filter((f) => f.stage === i).length;
        html += `<button class="tk-cell tk-${kind} lv${Math.min(4, c)}"`
            + ` data-stage="${i}" data-key="${escapeHtml(key)}"`
            + ` title="Stage ${i}: ${c || 'nothing'} new">${c || ''}</button>`;
    }
    return html;
}

function buildTracks() {
    const tracks = DATA.tracks || [];
    if (!tracks.length) { $('tracks').hidden = true; return; }
    const n = DATA.stages.length;
    const el = $('tk-grid');
    el.style.setProperty('--tk-cols', String(n));

    let html = '<div class="tk-row tk-head"><span class="tk-label"></span>';
    for (let i = 1; i <= n; i++) {
        html += `<span class="tk-col" data-stage="${i}">${i}</span>`;
    }
    html += '</div>';

    // 成果物の大きさ (その世代の最大の成果物。対数目盛)
    mainArtifacts = DATA.stages.map((st) => st.artifacts.reduce(
        (a, b) => ((b.size || 0) > (a.size || 0) ? b : a), st.artifacts[0] || {}));
    const mains = mainArtifacts;
    const sizes = mains.map((a) => a.size || 1);
    const maxLog = Math.max(...sizes.map((v) => Math.log10(v)));
    html += '<div class="tk-row tk-sizes"><span class="tk-label">artifact size</span>';
    DATA.stages.forEach((st, i) => {
        const h = Math.max(8, (Math.log10(sizes[i]) / maxLog) * 100);
        const title = `${mains[i].file || st.name}: ${fmtSize(sizes[i])}`;
        html += `<button class="tk-cell tk-size" data-stage="${st.num}"`
            + ` title="${escapeHtml(title)}"><i style="height:${h}%"></i></button>`;
    });
    html += '</div>';

    for (const t of tracks) {
        const subs = subsOf(t);
        html += `<div class="tk-row" data-track="${t.id}">`
            + `<button class="tk-label tk-${t.kind}" data-toggle="${t.id}"`
            + ` title="${escapeHtml(`${t.label} — split into ${subs.length} generations`)}">`
            + `<span class="tk-caret">▸</span>${escapeHtml(t.label)}</button>`
            + cells(t.features, n, t.kind, t.id) + '</div>';
        for (const sub of subs) {
            const feats = t.features.filter((f) => f.sub === sub);
            html += `<div class="tk-row tk-subrow" data-parent="${t.id}" hidden>`
                + `<span class="tk-label tk-${t.kind}">${escapeHtml(sub)}</span>`
                + cells(feats, n, t.kind, `${t.id}|${sub}`) + '</div>';
        }
    }
    el.innerHTML = html;

    for (const b of el.querySelectorAll('.tk-label[data-toggle]')) {
        b.onclick = () => toggleTrack(b.dataset.toggle);
    }
    for (const b of el.querySelectorAll('.tk-cell')) {
        b.onclick = () => {
            stopAuto();
            const stage = Number(b.dataset.stage);
            if (b.dataset.key) showDetail(b.dataset.key, stage);
            select(stage);
        };
    }
}

function toggleTrack(id) {
    if (openTracks.has(id)) openTracks.delete(id);
    else openTracks.add(id);
    const open = openTracks.has(id);
    for (const r of $('tk-grid').querySelectorAll(`.tk-subrow[data-parent="${id}"]`)) {
        r.hidden = !open;
    }
    const lab = $('tk-grid').querySelector(`.tk-label[data-toggle="${id}"]`);
    lab.classList.toggle('open', open);
    lab.querySelector('.tk-caret').textContent = open ? '▾' : '▸';
}

// 押した升 (系統 x 世代) の中身だけを出す。もう一度押すと畳む
function showDetail(key, stage) {
    const box = $('tk-detail');
    if (detailKey === `${key}|${stage}`) {
        detailKey = null;
        box.hidden = true;
        return;
    }
    const [id, sub] = key.split('|');
    const t = (DATA.tracks || []).find((x) => x.id === id);
    if (!t) return;
    const feats = t.features.filter((f) => f.stage === stage
        && (sub === undefined || f.sub === sub));
    detailKey = `${key}|${stage}`;
    box.hidden = false;
    const name = sub === undefined ? t.label : `${t.label} · ${sub}`;
    box.innerHTML = `<span class="tk-detail-head tk-${t.kind}">`
        + `${escapeHtml(name)} — Stage ${stage}</span>`
        + (feats.length
            ? `<ul class="tk-list">${feats.map((f) => `<li>${inlineMd(f.label)}`
                + `${sub === undefined && f.sub ? ` <em>${escapeHtml(f.sub)}</em>` : ''}`
                + '</li>').join('')}</ul>`
            : '<span class="tk-none">nothing new — everything earlier still holds</span>');
}

// 世代が変わったら列の強調だけを動かす (明細は押したときにだけ出す)
function renderTracks(num) {
    for (const c of $('tk-grid').querySelectorAll('.tk-cell, .tk-col')) {
        c.classList.toggle('now', Number(c.dataset.stage) === num);
    }
    const a = mainArtifacts[num - 1];
    $('tk-size-now').innerHTML = a && a.size != null
        ? `${escapeHtml(a.file || '')} <b>${fmtSize(a.size)}</b>` : '';
}

// ---- Stage パネル ----
function select(num, push = true) {
    current = num;
    const st = DATA.stages[num - 1];
    if (push) history.replaceState(null, '', `#${st.id}`);
    $('tl-range').value = String(num);
    document.querySelectorAll('.tl-node').forEach((n) => n.classList.remove('active'));
    $(`node-${num}`).classList.add('active');

    $('st-num').textContent = String(num).padStart(2, '0');
    $('st-title').textContent = st.title;
    $('st-tagline').textContent = st.tagline;
    $('st-meta').innerHTML = [
        ['reads', st.lang_in],
        ['written in', st.lang_impl],
        ['built by', st.built_by],
    ].map(([k, v]) => `<span title="${escapeHtml(`${k}: ${v}`)}">`
        + `${k}: <b>${escapeHtml(v)}</b></span>`).join('');

    // 冒頭段落だけを見せ，全文はアコーディオンへ (プレイグラウンドを主役にする)
    const lines = st.summary_md.split('\n');
    let cut = lines.findIndex((ln) => ln.trim() === '');
    if (cut <= 0 || /^([#|`-])/.test(lines[0])) cut = 0;
    $('st-lede').innerHTML = renderMd(lines.slice(0, cut).join('\n'));
    const rest = lines.slice(cut).join('\n').trim();
    $('acc-spec').hidden = rest === '';
    $('st-summary').innerHTML = renderMd(rest);

    $('st-caps').innerHTML = st.new_capabilities
        .map((c) => `<span class="cap-chip">${inlineMd(c)}</span>`).join('');
    $('acc-caps-sum').textContent =
        `New in this generation (${st.new_capabilities.length})`;
    $('acc-trivia').hidden = !st.trivia;
    if (st.trivia) $('st-trivia').innerHTML = inlineMd(st.trivia);

    // 世代を移ったらアコーディオンは畳み直す
    for (const d of document.querySelectorAll('.acc')) d.open = false;

    renderCoverage(st);
    renderTracks(num);

    $('acc-art-sum').textContent = 'Artifacts, sources & design documents '
        + `(${st.artifacts.length} artifacts, ${st.sources.length} sources)`;
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
    setupTerminal(st);
}

// ---- カバレッジ (処理系 / OS としての完成度) ----
// stage.coverage: [{title, note?, items: [{label, state: ok|gap|bad, note?}]}]
function renderCoverage(st) {
    const box = $('acc-cov');
    const groups = st.coverage || [];
    box.hidden = groups.length === 0;
    if (!groups.length) return;
    // サマリ行に合計を出す (閉じたままでも完成度が読めるように)
    const tot = { ok: 0, gap: 0, bad: 0 };
    for (const g of groups) {
        for (const it of g.items) tot[it.state] = (tot[it.state] || 0) + 1;
    }
    $('acc-cov-sum').innerHTML = 'Coverage — how complete is it? '
        + `<span class="ok">${tot.ok} ok</span>`
        + (tot.gap ? ` / <span class="gap">${tot.gap} gap</span>` : '')
        + (tot.bad ? ` / <span class="bad">${tot.bad} bad</span>` : '');
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

// 適合台帳の組 (build-site.sh が ledger.txt から差し込む)
const ledgerGroup = (st) => (st.coverage || []).find((g) => g.kind === 'ledger');

async function loadSample(path) {
    $('pg-input').value = 'Loading…';
    try {
        const res = await fetch(path);
        $('pg-input').value = res.ok ? await res.text() : '';
    } catch { $('pg-input').value = ''; }
}

function showLedgerNote(it) {
    $('pg-ledger').innerHTML = it
        ? `<span class="cov-chip ${it.state}">${it.state}</span>`
          + `<span class="pg-ledger-note">${escapeHtml(it.note || '')}</span>`
        : '';
}

async function setupPlayground(st) {
    pgConfig = PIPELINES[st.id] || null;
    $('pg-result').hidden = true;
    $('pg-steps').textContent = '';
    const none = $('pg-none');
    if (!pgConfig) {
        // ターミナルを持つ世代はプレイグラウンド節ごと隠す
        $('playground').hidden = true;
        return;
    }
    $('playground').hidden = false;
    $('pg-body').hidden = false;
    none.hidden = true;
    $('pg-note').innerHTML = pgConfig.note ? inlineMd(pgConfig.note) : '';
    $('pg-input-label').textContent = pgConfig.inputLabel || '';
    $('pg-stdin-box').hidden = !pgConfig.run;
    $('pg-stdin').value = pgConfig.stdin || '';

    // 台帳を持つ世代は probe を選べるようにする。名前は pipelines.js に
    // 写さず台帳から取るので，台帳を直せば選択肢も追随する
    const led = pgConfig.samplesFromLedger ? ledgerGroup(st) : null;
    const items = led ? led.items : [];
    $('pg-picker').hidden = items.length === 0;
    if (!items.length) {
        showLedgerNote(null);
        await loadSample(pgConfig.sample);
        return;
    }
    $('pg-sample').innerHTML = items.map((it, i) =>
        `<option value="${i}">${escapeHtml(it.label)} · ${it.state}</option>`).join('');
    $('pg-sample').value = '0';
    $('pg-sample').onchange = () => {
        const it = items[Number($('pg-sample').value)];
        if (!it) return;
        showLedgerNote(it);
        loadSample(`${pgConfig.sampleDir}/${it.label}.c`);
    };
    showLedgerNote(items[0]);
    await loadSample(`${pgConfig.sampleDir}/${items[0].label}.c`);
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

    handlers.set(id, (m) => {
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
    });

    worker.postMessage({
        id,
        steps: pgConfig.steps,
        userText: $('pg-input').value,
        stdinText: $('pg-stdin').value,
        runOutput: Boolean(pgConfig.run),
    });
};

// ---- ターミナル (OS 世代の対話セッション) ----
// 1 つの世代が複数の筋書き (別のカーネル・別の木) を持つことがある。
// Stage 16 は kernel17 / 18 / 19 を並べて見せる (記憶域は 2 つ並べないと
// 「広がった」ことが言えない。docs/stage016-os.md 8.6)
let termConfig = null;
let termScen = null;
let termId = null;
let termSampleIdx = 0;

const scenariosOf = (cfg) => cfg.scenarios || [cfg];

function setupTerminal(st) {
    termConfig = TERMINALS[st.id] || null;
    $('terminal').hidden = !termConfig;
    killTermSession();
    if (!termConfig) return;
    $('term-note').innerHTML = inlineMd(termConfig.note || '');
    const scen = scenariosOf(termConfig);
    const box = $('term-scenarios');
    box.hidden = scen.length < 2;
    box.innerHTML = '';
    if (scen.length > 1) {
        scen.forEach((sc, i) => {
            const b = document.createElement('button');
            b.className = 'term-scen';
            b.textContent = sc.label || `scenario ${i + 1}`;
            b.onclick = () => selectScenario(i);
            box.appendChild(b);
        });
    }
    selectScenario(0);
}

function killTermSession() {
    if (termId == null) return;
    worker.postMessage({ kind: 'tkill', id: termId });
    handlers.delete(termId);
    termId = null;
}

function selectScenario(i) {
    const scen = scenariosOf(termConfig);
    termScen = scen[i] || scen[0];
    killTermSession();
    [...$('term-scenarios').children].forEach((b, k) => {
        b.classList.toggle('active', k === i);
    });
    $('term-blurb').hidden = !termScen.blurb;
    if (termScen.blurb) $('term-blurb').innerHTML = inlineMd(termScen.blurb);
    $('term-out').textContent = '';
    $('term-status').textContent = 'not booted';
    $('term-status').className = 'term-status';
    $('term-files-box').hidden = true;
    $('term-files-head').textContent = termConfig.fs === 2
        ? 'The sfs2 tree after the session — click a file to download'
        : 'Files in the sfs image after the session — click to download';
    termSampleIdx = 0;
    renderTermSamples();
    $('term-boot').textContent = 'Boot';
    $('term-in').placeholder = termConfig.mode === 'boot'
        ? 'boot line (program + argv) — press Enter to boot'
        : 'type a command and press Enter';
    loadTermSample();
}

function renderTermSamples() {
    $('term-samples').innerHTML = '';
    termScen.samples.forEach((s, i) => {
        const li = document.createElement('li');
        li.className = i < termSampleIdx ? 'done' : i === termSampleIdx ? 'current' : '';
        li.innerHTML = `<code>${escapeHtml(s.cmd)}</code>`
            + `<span class="ts-note">${escapeHtml(s.note || '')}</span>`;
        li.onclick = () => { termSampleIdx = i; renderTermSamples(); loadTermSample(); };
        $('term-samples').appendChild(li);
    });
}

function loadTermSample() {
    const s = termScen.samples[termSampleIdx];
    if (s) $('term-in').value = s.cmd;
}

function termPrint(text, cls) {
    const out = $('term-out');
    if (cls) {
        const span = document.createElement('span');
        span.className = cls;
        span.textContent = text;
        out.appendChild(span);
    } else {
        out.appendChild(document.createTextNode(text));
    }
    out.scrollTop = out.scrollHeight;
}

const termStatus = (text, cls) => {
    $('term-status').textContent = text;
    $('term-status').className = `term-status ${cls || ''}`;
};

// 起動前から置いてあった名前 (親のディレクトリも含む)。
// セッション中に増えたものへ new を付けるために使う
function seededNames() {
    const names = new Set();
    for (const f of termScen.files || []) {
        const parts = f.name.split('/');
        for (let i = 1; i <= parts.length; i++) names.add(parts.slice(0, i).join('/'));
    }
    return names;
}

// sfs / sfs2 の中身を出す。sfs2 は木なので経路順に並べて字下げする
function renderTermFiles(files) {
    const names = seededNames();
    const tree = termConfig.fs === 2;
    const list = [...files].sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
    $('term-files-box').hidden = false;
    $('term-files').innerHTML = '';
    for (const f of list) {
        const parts = f.name.split('/');
        const li = document.createElement('li');
        if (tree) li.style.paddingLeft = `${(parts.length - 1) * 16}px`;
        const label = tree ? parts[parts.length - 1] : f.name;
        if (f.dir) {
            li.insertAdjacentHTML('beforeend',
                `<span class="tf-dir">${escapeHtml(label)}/</span>`);
        } else {
            const a = document.createElement('a');
            a.textContent = label;
            a.onclick = () => {
                const url = URL.createObjectURL(new Blob([f.data]));
                const dl = document.createElement('a');
                dl.href = url;
                dl.download = parts[parts.length - 1];
                dl.click();
                URL.revokeObjectURL(url);
            };
            li.appendChild(a);
            li.insertAdjacentHTML('beforeend',
                `<span class="tf-size">${fmtSize(f.data.length)}</span>`);
        }
        if (!names.has(f.name)) {
            li.insertAdjacentHTML('beforeend', '<span class="tf-new">new</span>');
        }
        $('term-files').appendChild(li);
    }
}

function termBoot(bootLine) {
    killTermSession();
    termId = ++runId;
    $('term-out').textContent = '';
    $('term-files-box').hidden = true;
    termStatus(termScen.heavy ? 'preparing… (allocating a large emulator)' : 'preparing…',
        'running');
    termPrint(`— boot: ${bootLine.trim()} —\n`, 'echo');
    handlers.set(termId, (m) => {
        if (m.kind === 'tout') {
            let s = '';
            for (let i = 0; i < m.data.length; i++) s += String.fromCharCode(m.data[i]);
            termPrint(s);
        } else if (m.kind === 'tstate') {
            if (m.state === 'running') termStatus('running…', 'running');
            else if (m.state === 'waiting') termStatus('waiting for input', 'running');
            else if (m.state === 'exited') {
                termStatus(`exited with code ${m.exitCode} (${fmtNum(m.icount)} instructions)`);
                termPrint(`\n— machine halted, exit code ${m.exitCode} —\n`, 'echo');
            } else {
                termStatus(m.error || 'crashed', 'err');
            }
        } else if (m.kind === 'tfiles') {
            renderTermFiles(m.files);
        }
    });
    worker.postMessage({
        kind: 'boot',
        id: termId,
        kernel: termScen.kernel,
        files: termScen.files,
        imgSize: termScen.imgSize,
        maxEntries: termScen.maxEntries,
        ramSize: termScen.ramSize,
        fs: termConfig.fs || 1,
        bootLine,
    });
}

$('term-boot').onclick = () => {
    if (!termConfig || !termScen) return;
    termBoot(termConfig.mode === 'boot'
        ? `${$('term-in').value.trim() || termScen.samples[0].cmd}\n`
        : termScen.bootLine);
    $('term-in').focus();
};

$('term-in').addEventListener('keydown', (e) => {
    if (e.key !== 'Enter' || !termConfig) return;
    const line = $('term-in').value;
    if (termConfig.mode === 'boot') {
        termBoot(`${line.trim() || termScen.samples[0].cmd}\n`);
    } else {
        if (termId == null) { termBoot(termScen.bootLine); }
        termPrint(`${line}\n`, 'echo');
        const bytes = new Uint8Array([...`${line}\n`].map((c) => c.charCodeAt(0) & 0xff));
        worker.postMessage({ kind: 'tin', id: termId, data: bytes });
    }
    // 送った行が案内どおりなら次の手順を先置きする
    const cur = termScen.samples[termSampleIdx];
    if (cur && line.trim() === cur.cmd) {
        termSampleIdx++;
        renderTermSamples();
        loadTermSample();
        if (!termScen.samples[termSampleIdx]) $('term-in').value = '';
    } else {
        $('term-in').value = '';
    }
});

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
    buildTracks();
    const m = location.hash.match(/^#stage(\d{3})$/);
    select(m ? Number(m[1]) : 1, false);
}

main();
