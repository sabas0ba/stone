#!/bin/sh
# ld18 (stage015/ld18.sc) で 'E' 形式の実行ファイルを組む。
#
#   ld18.sh [-s <データスタックのバイト数>] -o <出力> <入力...>
#
# 入力の並びの中で -L 以降はライブラリの部品 (未定義の名前を定義するとき
# だけ組む)，-N 以降はふたたび必ず組むものになる。組むオブジェクトは
# **入力の順に置く**。前置部の syscall スタブを jal で呼ぶ libc の部品は
# 前置部から 1 MiB 以内に要るので，libc と実行時ルーチンを先に並べる
# (stage015/ld18.sc の @section order)。
#
# ld18 は入出力を QEMU の RAM で受け渡す (@section io)。ここで入力を
# 詰めて RAM ファイルの 0x9000_0000 に当たる位置へ書き，走らせた後に
# 0xa800_0000 の頭を見てイメージを取り出す。RAM ファイルは tmp/ld18 に置く
# (疎ファイルなので実際に使うのは触れたページだけである)。
#
# 環境変数: STONE_ENGINE (tools/env.sh), STONE_LD18_TIMEOUT (秒。既定 3600)
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

die() {
    echo "ld18.sh: $*" >&2
    exit 1
}

dstk=262144
out=""
while [ $# -gt 0 ]; do
    case $1 in
    -s) dstk=$2; shift 2 ;;
    -o) out=$2; shift 2 ;;
    *) break ;;
    esac
done
[ -n "$out" ] || die "usage: ld18.sh [-s bytes] -o out [-L|-N] obj..."
[ -s tmp/build/ld18.bin ] || die "tmp/build/ld18.bin が無い (sh tools/build.sh stage015)"

w=tmp/ld18
mkdir -p "$w"
blob=$w/in.blob
ram=$w/ram
list=$w/in.list

# 32 bit をリトルエンディアンで書く
le32() {
    # shellcheck disable=SC2059
    printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $(($1 & 255)) $((($1 >> 8) & 255)) \
        $((($1 >> 16) & 255)) $((($1 >> 24) & 255)))"
}

# 記録の列を作る。頭の「全体の長さ」は後で書き戻す
lib=0
n=0
rm -f "$list.tmp"
{
    printf 'LDI1'
    le32 0
    le32 69                    # 'E'
    le32 "$dstk"
    for f in "$@"; do
        case $f in
        -L) lib=1; continue ;;
        -N) lib=0; continue ;;
        esac
        [ -s "$f" ] || die "入力が無い: $f"
        sz=$(wc -c < "$f" | tr -d ' ')
        le32 "$sz"
        le32 "$lib"
        cat "$f"
        pad=$(((4 - sz % 4) % 4))
        [ "$pad" -eq 0 ] || head -c "$pad" /dev/zero
        printf '%d\t%s\t%s\n' "$n" "$lib" "$f" >> "$list.tmp"
        n=$((n + 1))
    done
} > "$blob.tmp" || die "入力を詰められなかった"
total=$(wc -c < "$blob.tmp" | tr -d ' ')
[ "$total" -le 402653184 ] || die "入力が 384 MiB を超える ($total バイト)"
# **iflag=fullblock を付ける。** パイプから読む dd は 1 回の read で返った分
# だけを 1 塊とするので，printf が 2 回に分けて書くと前半の 4 バイトしか
# 書かれず，長さの欄が 0 のまま残る
{ printf 'LDI1'; le32 "$total"; } \
    | dd of="$blob.tmp" bs=8 count=1 iflag=fullblock conv=notrunc 2> /dev/null
mv "$blob.tmp" "$blob"
mv "$list.tmp" "$list"

# RAM ファイル (1 GiB の疎ファイル) の 256 MiB の位置 (0x9000_0000) へ入力を置く
rm -f "$ram"
truncate -s 1G "$ram"
dd if="$blob" of="$ram" bs=1M seek=256 conv=notrunc 2> /dev/null

rc=0
STONE_QEMU_RAMFILE="$ram" STONE_QEMU_RAM=1G \
    STONE_QEMU_TIMEOUT="${STONE_LD18_TIMEOUT:-3600}" \
    sh tools/env.sh qemu tmp/build/ld18.bin < /dev/null > "$w/diag" || rc=$?
cat "$w/diag" >&2
if [ "$rc" -ne 0 ]; then
    # 診断の「object N」は入力の並びの番号である。対応は tmp/ld18/in.list
    echo "ld18.sh: ld18 が $rc で止まった (オブジェクトの番号は $list)" >&2
    exit "$rc"
fi

# 0xa800_0000 (RAM の 640 MiB) の頭: 'LD18' とイメージの長さ
magic=$(dd if="$ram" bs=1M skip=640 count=1 2> /dev/null | head -c 4)
[ "$magic" = LD18 ] || die "出力の頭が無い (ld18 が書き終えていない)"
len=$(dd if="$ram" bs=1M skip=640 count=1 2> /dev/null | od -An -tu4 -j 4 -N 4 | tr -d ' ')
dd if="$ram" bs=4096 skip=$((640 * 256 + 1)) count=$(((len + 4095) / 4096)) 2> /dev/null \
    | head -c "$len" > "$out.tmp"
[ "$(wc -c < "$out.tmp" | tr -d ' ')" -eq "$len" ] || die "出力を取り出せなかった"
mv "$out.tmp" "$out"
rm -f "$ram"
echo "ld18.sh: $out ($len バイト)" >&2
