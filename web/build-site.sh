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

cp web/index.html web/style.css web/app.js web/rv32.js web/worker.js \
   web/pipelines.js "$dist/"
touch "$dist/.nojekyll"

# stages.json: コンテンツに成果物のサイズと SHA-256 を付与する。
# stage014 には適合台帳 (tests/stage014/ledger.txt) の実データを
# カバレッジとして取り込む (台帳の書式は同ファイル冒頭)
python3 - "$dist" <<'EOF'
import hashlib, json, os, subprocess, sys

dist = sys.argv[1]
data = json.load(open('web/data/stages-content.json'))

items = []
for ln in open('tests/stage014/ledger.txt'):
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
st14 = data['stages'][13]
st14.setdefault('coverage', []).insert(0, {
    'title': 'Conformance ledger — real-world C idioms (tests/stage014/ledger.txt)',
    'note': 'measured against the newest generation; `gap` means the compiler '
            'rejects it explicitly (never silently miscompiles)',
    'items': items,
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
