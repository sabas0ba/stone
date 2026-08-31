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

# manifest: 名前 URL 印
#   URL は '|' で区切って**複数書ける**。前から順に試し，SHA-256 が
#   合ったものを使う。**印が記録であって URL は記録ではない** ——
#   同じ書庫がどこから来ても，印が合えば同じ物である。
#
#   写しを足すのは取得先が落ちたとき・網が拒むときのためである。
#   実際 zlib.net はこの作業環境から引くと HTML の関門を返してきて
#   書庫が取れなかった (docs/stage017-cc.md 32 章)。madler/zlib の
#   release にある書庫は**記録した SHA-256 とバイト単位で同じ**だった
#   ので写しとして足した。**印を書き換えたのではない。**
#
#   書庫 (.tar.gz / .tar.bz2) なら印は SHA-256。
#   それ以外は git の取得元とみなし，印は commit とする。git の commit は
#   木と履歴の内容ハッシュなので，書庫の SHA-256 と同じ役目を果たす
#   (git 自身が取得時に検証する)。**ただし git の commit は SHA-1 である**
#   ことは意識しておく (docs/stage015-tcc.md 3 章)
manifest() {
    cat <<'EOF'
bzip2 https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
zlib https://www.zlib.net/fossils/zlib-1.3.1.tar.gz|https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
tcc https://github.com/TinyCC/tinycc mob:2ba12e83b3599ca8f5d50c179fe5138fe956f0c9
gcc47 https://ftp.gnu.org/gnu/gcc/gcc-4.7.4/gcc-4.7.4.tar.bz2 92e61c6dc3a0a449e62d72a38185fda550168a86702dea07125ebd3ec3996282
EOF
}

name=${1:-}
if [ -z "$name" ]; then
    echo "取得できる外部ソース (fetch.sh <名前>):"
    manifest | while read -r n u s; do
        st="未取得"
        [ -d "$ext/$n" ] && st="取得済み ($ext/$n)"
        printf '  %-8s %s  [%s]\n' "$n" "${u%%|*}" "$st"
    done
    exit 0
fi

line=$(manifest | awk -v n="$name" '$1 == n { print $2, $3 }')
if [ -z "$line" ]; then
    echo "fetch.sh: 未知の名前: $name" >&2
    exit 2
fi
urls=${line%% *}
want=${line##* }
# 形式の判定には先頭の URL を使う (写しは同じ物なので同じ形式である)
url=${urls%%|*}

# 書庫の形式は URL の末尾で決める (tar.gz / tar.bz2)。
# どちらでもなければ git の取得元とみなす
case "$url" in
*.tar.bz2) ext_sfx=tar.bz2; taropt=-xjf ;;
*.tar.gz|*.tgz) ext_sfx=tar.gz; taropt=-xzf ;;
*)
    # git: 印は「枝またはタグ:commit」。**commit を直接取りに行く**。
    # 枝の先頭を取ると，枝が動いた瞬間に照合が壊れて取得できなくなる
    # (mob のような開発枝は日々動く)。名札は記録のためだけに持つ
    lbl=${want%%:*}
    com=${want##*:}
    echo "fetch: $url ($lbl $com)" >&2
    rm -rf "$ext/$name"
    mkdir -p "$ext/$name"
    ( cd "$ext/$name" \
      && git init -q . \
      && git remote add origin "$url" \
      && git fetch -q --depth 1 origin "$com" \
      && git checkout -q FETCH_HEAD )
    got=$(cd "$ext/$name" && git rev-parse HEAD)
    if [ "$got" != "$com" ]; then
        rm -rf "$ext/$name"
        echo "fetch.sh: commit が一致しない" >&2
        echo "  期待: $com" >&2
        echo "  実際: $got" >&2
        exit 1
    fi
    echo "fetched: $ext/$name (commit 照合済み)" >&2
    exit 0
    ;;
esac

mkdir -p "$ext"
tarball="$ext/$name.$ext_sfx"
# **写しを前から順に試す。** 印が合った時点で採る。1 つ目が落ちていても
# 網に拒まれていても，印が合う物が手に入れば同じ作業ができる
got=
_rest=$urls
while [ -n "$_rest" ]; do
    _u=${_rest%%|*}
    case "$_rest" in *\|*) _rest=${_rest#*|} ;; *) _rest= ;; esac
    echo "fetch: $_u" >&2
    if ! curl -fL --proto '=https' -o "$tarball" "$_u"; then
        echo "  取れなかった (次の写しを試す)" >&2
        rm -f "$tarball"
        continue
    fi
    got=$(sha256sum "$tarball" | cut -d' ' -f1)
    [ "$got" = "$want" ] && break
    echo "  SHA-256 が一致しない (次の写しを試す)" >&2
    echo "    期待: $want" >&2
    echo "    実際: $got" >&2
    rm -f "$tarball"
    got=
done
if [ "$got" != "$want" ]; then
    rm -f "$tarball"
    echo "fetch.sh: どの取得先からも印の合う書庫が取れなかった" >&2
    echo "  期待: $want" >&2
    exit 1
fi
rm -rf "$ext/$name"
mkdir -p "$ext/$name"
# 書庫内の uid/gid は配布側の作業環境に依存する。復元すると user namespace
# 内や非 root 環境で展開に失敗するため，内容と mode だけを取り出す。
tar --no-same-owner "$taropt" "$tarball" -C "$ext/$name" --strip-components=1
echo "fetched: $ext/$name (SHA-256 照合済み)" >&2
