#!/bin/sh
# コンテナ環境の操作コマンド (ローカル・CI 共用)。
#
# 使用法:
#   env.sh build            イメージをビルドし，導入パッケージを env/packages.lock と照合する
#   env.sh lock             env/packages.lock を再生成する (アーカイブ時点を変更した場合のみ)
#   env.sh run <cmd...>     repo を /work にマウントしてコンテナ内でコマンドを実行する
#   env.sh qemu <bin> [...] tools/run-qemu.sh をコンテナ内で実行する (stdin/stdout はそのまま接続)
#
# 環境変数:
#   STONE_ENGINE      コンテナエンジン (podman | docker)。未指定時は podman, docker の順に自動検出
#   STONE_IMAGE       イメージ名 (default: stone-env)
#   STONE_QEMU_TRACE      qemu: 実行トレースの記録先ファイル (tools/run-qemu.sh 参照)
#   STONE_QEMU_GDB        qemu: GDB stub の待受けポート。コンテナ外へは 127.0.0.1 のみに公開する
#   STONE_CONTAINER_NAME  qemu: コンテナ名。テストからの停止操作に使用する
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
engine=${STONE_ENGINE:-}
image=${STONE_IMAGE:-stone-env}

if [ -z "$engine" ]; then
    if command -v podman >/dev/null 2>&1; then
        engine=podman
    elif command -v docker >/dev/null 2>&1; then
        engine=docker
    else
        echo "error: podman も docker も見つからない" >&2
        exit 1
    fi
fi

# Git Bash (MSYS) ではパス自動変換を抑止し，マウント元は Windows 形式パスで渡す
host_root=$repo_root
case "$(uname -s)" in
MINGW*|MSYS*)
    export MSYS_NO_PATHCONV=1
    export MSYS2_ARG_CONV_EXCL='*'
    host_root=$(cd -- "$repo_root" && pwd -W)
    ;;
esac

installed_packages() {
    "$engine" run --rm "$image" dpkg-query -W -f '${Package}=${Version}\n'
}

cmd=${1:-}
[ $# -gt 0 ] && shift

case "$cmd" in
build)
    "$engine" build -t "$image" -f "$host_root/env/Containerfile" "$host_root/env"
    if ! installed_packages | diff -u "$repo_root/env/packages.lock" -; then
        echo "error: 導入パッケージが env/packages.lock と一致しない。" >&2
        echo "       意図した更新の場合は 'env.sh lock' で再生成し commit する。" >&2
        exit 1
    fi
    ;;
lock)
    installed_packages > "$repo_root/env/packages.lock"
    echo "wrote env/packages.lock" >&2
    ;;
run)
    exec "$engine" run --rm -i -v "$host_root:/work" "$image" "$@"
    ;;
qemu)
    exec "$engine" run --rm -i -v "$host_root:/work" \
        ${STONE_CONTAINER_NAME:+--name "$STONE_CONTAINER_NAME"} \
        ${STONE_QEMU_GDB:+-p "127.0.0.1:$STONE_QEMU_GDB:$STONE_QEMU_GDB" -e STONE_QEMU_GDB} \
        -e STONE_QEMU_TRACE \
        "$image" sh tools/run-qemu.sh "$@"
    ;;
*)
    echo "usage: env.sh {build|lock|run|qemu} ..." >&2
    exit 2
    ;;
esac
