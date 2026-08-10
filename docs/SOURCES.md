# 一次資料

実体は `docs/external/` (git ignore) に保存する。取得時は本表の SHA-256 と照合すること。

| ファイル | 内容 | 取得元 | SHA-256 |
|---|---|---|---|
| riscv-spec-20191213.pdf | The RISC-V Instruction Set Manual Volume I: Unprivileged ISA (Ratified, 2019-12-13) | https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMAFDQC/riscv-spec-20191213.pdf | f392624cc815cd3f259413cbd9ae2f38678ee930878855a0f4673019410d7554 |
| riscv-privileged-20211203.pdf | The RISC-V Instruction Set Manual Volume II: Privileged Architecture (Ratified, v1.12, 2021-12-03) | https://github.com/riscv/riscv-isa-manual/releases/download/Priv-v1.12/riscv-privileged-20211203.pdf | fd3907c0e0b1e3df91bb5b54e78e575a77c1c144ab320d83e059feacdfa253bc |

## 外部プログラムのソース (Stage 14 以降の入力)

処理系への**入力**として読む外部ソース (docs/stage014-external.md 2.1)。
取得は `tools/fetch.sh <名前>` で行い，SHA-256 の照合まで自動で済む。
一次資料と同じく実体は `docs/external/` に置き，repo には取り込まない。

| 名前 | 内容 | 取得元 | SHA-256 |
|---|---|---|---|
| bzip2 | bzip2 1.0.8 (最初の 1 本の候補) | https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz | ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269 |
| zlib | zlib 1.3.1 (候補) | https://www.zlib.net/fossils/zlib-1.3.1.tar.gz | 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23 |

SHA-256 は公開されている配布告知の値を記録し，最初の取得 (2026-08-10)
で照合が成立したことをもって確定した (fetch.sh は不一致なら取得物を
消して失敗する)。zlib の取得元は当初 www.zlib.net/zlib-1.3.1.tar.gz
だったが，zlib.net は新版が出ると旧版を fossils/ へ移す (404 になる)
ため恒久 URL の方を記録している。

