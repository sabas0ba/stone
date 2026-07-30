#!/bin/sh
# コンテナ環境の操作コマンド (ローカル・CI 共用)。
#
# 使用法:
#   env.sh build            イメージを用意し，導入パッケージを env/packages.lock と照合する
#                           (env/Containerfile から作った像が既にあればビルドを省く)
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
#   STONE_REBUILD         build: 像が最新でもビルドし直す (env/ をいじりながら試すとき)
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

# 像の素性を表す印。env/Containerfile は base image の digest と
# snapshot の日付まで固定してあるので，この 1 枚が決まれば像は一意に決まる。
# 作った像にラベルとして焼き，次回はそれを見てビルドを省く
env_stamp() {
    sha256sum "$repo_root/env/Containerfile" | cut -d' ' -f1
}
image_stamp() {
    "$engine" inspect --format '{{ index .Config.Labels "stone.env" }}' "$image" 2>/dev/null || true
}

cmd=${1:-}
[ $# -gt 0 ] && shift

case "$cmd" in
build)
    # 像が既に手元にあり，env/Containerfile から作ったものなら作り直さない。
    # CI では像を丸ごとキャッシュして持ち込むので，ここが効いて毎回の
    # apt-get (snapshot.debian.org からの取得) が丸ごと省ける。
    # packages.lock の照合は像を作ったかどうかに関わらず必ず行う
    stamp=$(env_stamp)
    if [ -n "${STONE_REBUILD:-}" ] || [ "$(image_stamp)" != "$stamp" ]; then
        "$engine" build -t "$image" --label "stone.env=$stamp" \
            -f "$host_root/env/Containerfile" "$host_root/env"
    else
        echo "image $image is up to date (stone.env=$stamp)" >&2
    fi
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
        -e STONE_QEMU_TRACE -e STONE_QEMU_RAMFILE \
        "$image" sh tools/run-qemu.sh "$@"
    ;;
*)
    echo "usage: env.sh {build|lock|run|qemu} ..." >&2
    exit 2
    ;;
esac
