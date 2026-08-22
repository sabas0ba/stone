#!/bin/sh
# Web アプリ (GitHub Pages) のサイト組立て。
#
# 事前に sh tools/build.sh all でチェーンの成果物を tmp/build/ に
# 用意しておくこと。生成物は tmp/site/ (git ignore) に置く。
#
#   tmp/site/
#   ├── index.html, style.css, app.js, rv32.js, worker.js, pipelines.js
#   ├── data/stages.json     世代コンテンツ + 成果物メタ (サイズ / SHA-256)
#   ├── assets/bin/          チェーン成果物 (プレイグラウンドが実行する実物)
#   └── files/               ソース・サンプル (ビューアとプレイグラウンド初期値)
#
# 参照される資産は web/pipelines.js と web/data/stages-content.json から
# 機械的に集める。参照先が無ければ失敗する (デッドリンクを出荷しない)。
#
# 使用法: sh web/build-site.sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

[ -f tmp/build/cc.bin ] || {
    echo "error: tmp/build が無い。先に sh tools/build.sh all を実行する" >&2
    exit 2
}

dist=tmp/site
rm -rf "$dist"
mkdir -p "$dist/data" "$dist/assets/bin" "$dist/files"

cp web/index.html web/style.css web/app.js web/rv32.js web/sfs.js \
   web/sfs2.js web/worker.js web/pipelines.js "$dist/"
touch "$dist/.nojekyll"

# stages.json: コンテンツに成果物のサイズと SHA-256 を付与する。
# stage014 / stage015 には適合台帳 (tests/stageNNN/ledger.txt) の実データを
# カバレッジとして取り込む (台帳の書式は同ファイル冒頭)。
# 取り込んだ組には kind='ledger' を付ける —— プレイグラウンドの
# probe 選択がこれを見て選択肢を作る (web/app.js)
python3 - "$dist" <<'EOF'
import hashlib, json, os, subprocess, sys

dist = sys.argv[1]
data = json.load(open('web/data/stages-content.json'))
by_id = {st['id']: st for st in data['stages']}

def read_ledger(path):
    items = []
    for ln in open(path):
        ln = ln.rstrip('\n')
        if not ln or ln.startswith('#'):
            continue
        parts = ln.split(None, 3)         # 説明欄は無い行もある
        name, state, expect = parts[:3]
        desc = parts[3] if len(parts) > 3 else ''
        note = (f'expected output: {expect}' if state == 'ok'
                else f'rejected by cc with exit code {expect}' if state == 'gap'
                else f'WRONG output: {expect}') + (f' — {desc}' if desc else '')
        items.append({'label': name, 'state': state, 'note': note})
    return items

LEDGERS = [
    ('stage014', 'tests/stage014/ledger.txt',
     'Conformance ledger — real-world C idioms (tests/stage014/ledger.txt)',
     'measured against cc14g through pp14 and ld14; `gap` means the compiler '
     'rejects it explicitly (never silently miscompiles)'),
    ('stage015', 'tests/stage015/ledger.txt',
     'Conformance ledger — 64-bit integers and floating point '
     '(tests/stage015/ledger.txt)',
     'measured against cc15p, linked with the runtime helpers rt64 and rtfp; '
     'the expectations are bit patterns, compared with the host\'s IEEE-754'),
]
for sid, path, title, note in LEDGERS:
    items = read_ledger(path)
    if not items:
        sys.exit(f'error: 台帳が空: {path}')
    for it in items:
        probe = f'tests/{sid}/probe/{it["label"]}.c'
        if not os.path.exists(probe):
            sys.exit(f'error: 台帳の probe が無い: {probe}')
    by_id[sid].setdefault('coverage', []).insert(0, {
        'kind': 'ledger', 'title': title, 'note': note, 'items': items,
    })

def artifact_path(name):
    # hex0.bin だけは git 管理の seed。他は tmp/build の生成物
    p = f'tmp/build/{name}'
    return p if os.path.exists(p) else f'stage001/{name}'

missing = []
for st in data['stages']:
    for a in st['artifacts']:
        p = artifact_path(a['file'])
        if not os.path.exists(p):
            missing.append(a['file'])
            continue
        blob = open(p, 'rb').read()
        a['size'] = len(blob)
        a['sha256'] = hashlib.sha256(blob).hexdigest()
if missing:
    sys.exit(f'error: 成果物が無い: {missing}')

data['commit'] = subprocess.check_output(
    ['git', 'rev-parse', 'HEAD'], text=True).strip()
json.dump(data, open(f'{dist}/data/stages.json', 'w'),
          ensure_ascii=False, indent=1)
print(f'wrote {dist}/data/stages.json')
EOF

# 参照される資産を集めて複写する。
#   assets/bin/<name>  : tmp/build/<name> (hex0.bin のみ stage001/)
#   files/<repo path>  : リポジトリのソース・サンプル
refs=$( { grep -oE "(assets/bin|files)/[A-Za-z0-9_./-]+" web/pipelines.js
          python3 -c "
import json
d = json.load(open('web/data/stages-content.json'))
for st in d['stages']:
    for a in st['artifacts']: print('assets/bin/' + a['file'])
    for s in st['sources']: print('files/' + s['path'])
"; } | sort -u)

for ref in $refs; do
    case "$ref" in
    assets/bin/*)
        name=${ref#assets/bin/}
        src=tmp/build/$name
        [ -f "$src" ] || src=stage001/$name
        ;;
    files/*)
        src=${ref#files/}
        ;;
    esac
    # テンプレート文字列 (`files/dir/${x}`) はディレクトリまでが抽出される。
    # その場合はディレクトリ全体を複写する
    if [ -d "$src" ]; then
        mkdir -p "$dist/$ref"
        cp -r "$src"/* "$dist/$ref/"
        continue
    fi
    [ -f "$src" ] || { echo "error: 参照先が無い: $ref -> $src" >&2; exit 1; }
    mkdir -p "$dist/$(dirname "$ref")"
    cp "$src" "$dist/$ref"
done

echo "site: $(find "$dist" -type f | wc -l) files, $(du -sh "$dist" | cut -f1)" >&2
