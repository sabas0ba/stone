#!/bin/sh
# 外部ソースの取得と固定 (docs/stage014-external.md 2.1)。
#
#   fetch.sh          一覧を表示する
#   fetch.sh <名前>   取得して SHA-256 を照合し，docs/external/<名前>/ へ展開する
#
# 取得先と SHA-256 は本ファイル末尾の表 (manifest) に持つ。外部ソースは
# repo に取り込まない。実体は docs/external/ (git ignore) に置き，
# ここに記録した URL と SHA-256 が「どれを使ったか」の記録になる
# (docs/SOURCES.md も参照)。
#
# 照合に失敗したら取得物を消して失敗する。壊れた素材や差し替えられた
# 素材で作業を始めないためである。
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ext="$repo_root/docs/external"

# manifest: 名前 URL SHA-256 (書庫の形式は URL の拡張子で見分ける)
manifest() {
    cat <<'EOF'
bzip2 https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
zlib https://www.zlib.net/fossils/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
tcc https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2 de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c
EOF
}

name=${1:-}
if [ -z "$name" ]; then
    echo "取得できる外部ソース (fetch.sh <名前>):"
    manifest | while read -r n u s; do
        st="未取得"
        [ -d "$ext/$n" ] && st="取得済み ($ext/$n)"
        printf '  %-8s %s  [%s]\n' "$n" "$u" "$st"
    done
    exit 0
fi

line=$(manifest | awk -v n="$name" '$1 == n { print $2, $3 }')
if [ -z "$line" ]; then
    echo "fetch.sh: 未知の名前: $name" >&2
    exit 2
fi
url=${line%% *}
want=${line##* }

# 書庫の形式は URL の末尾で決める (tar.gz / tar.bz2)
case "$url" in
*.tar.bz2) ext_sfx=tar.bz2; taropt=-xjf ;;
*.tar.gz|*.tgz) ext_sfx=tar.gz; taropt=-xzf ;;
*) echo "fetch.sh: 未知の書庫形式: $url" >&2; exit 2 ;;
esac

mkdir -p "$ext"
tarball="$ext/$name.$ext_sfx"
echo "fetch: $url" >&2
curl -fL --proto '=https' -o "$tarball" "$url"
got=$(sha256sum "$tarball" | cut -d' ' -f1)
if [ "$got" != "$want" ]; then
    rm -f "$tarball"
    echo "fetch.sh: SHA-256 が一致しない" >&2
    echo "  期待: $want" >&2
    echo "  実際: $got" >&2
    exit 1
fi
rm -rf "$ext/$name"
mkdir -p "$ext/$name"
tar "$taropt" "$tarball" -C "$ext/$name" --strip-components=1
echo "fetched: $ext/$name (SHA-256 照合済み)" >&2
