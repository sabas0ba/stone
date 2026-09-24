# 一次資料

実体は `docs/external/` (git ignore) に保存する。取得時は本表の SHA-256 と照合すること。

| ファイル | 内容 | 取得元 | SHA-256 |
|---|---|---|---|
| riscv-spec-20191213.pdf | The RISC-V Instruction Set Manual Volume I: Unprivileged ISA (Ratified, 2019-12-13) | https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMAFDQC/riscv-spec-20191213.pdf | f392624cc815cd3f259413cbd9ae2f38678ee930878855a0f4673019410d7554 |
| riscv-privileged-20211203.pdf | The RISC-V Instruction Set Manual Volume II: Privileged Architecture (Ratified, v1.12, 2021-12-03) | https://github.com/riscv/riscv-isa-manual/releases/download/Priv-v1.12/riscv-privileged-20211203.pdf | fd3907c0e0b1e3df91bb5b54e78e575a77c1c144ab320d83e059feacdfa253bc |

## 外部プログラムのソース (Stage 14 以降の入力)

処理系への**入力**として読む外部ソース (docs/stage014-external.md 2.1)。
取得は `tools/fetch.sh <名前>` で行い，照合値の検査まで自動で済む。
一次資料と同じく実体は `docs/external/` に置き，repo には取り込まない。

| 名前 | 内容 | 取得元 | 照合値 (SHA-256 / git commit) |
|---|---|---|---|
| bzip2 | bzip2 1.0.8 (最初の 1 本の候補) | https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz | ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269 |
| zlib | zlib 1.3.1 (候補) | https://www.zlib.net/fossils/zlib-1.3.1.tar.gz | 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23 |
| tcc | Tiny C Compiler (Stage 15 の対象)。開発 branch mob を commit で固定 | https://github.com/TinyCC/tinycc | commit 2ba12e83b3599ca8f5d50c179fe5138fe956f0c9 |
| gcc47 | GCC 4.7.4 (Stage 17 の測定対象) | https://ftp.gnu.org/gnu/gcc/gcc-4.7.4/gcc-4.7.4.tar.bz2 | 92e61c6dc3a0a449e62d72a38185fda550168a86702dea07125ebd3ec3996282 |
| gmp | GMP 6.3.0 (GCC の前提。gcc/ の単位が `gmp.h` を読む) | https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.bz2 | ac28211a7cfb609bae2e2c8d6058d66c8fe96434f740cf6fe2e47b000d1c20cb |
| mpfr | MPFR 3.1.6 (GCC の前提。`real.h` が `mpfr.h` を読む) | https://ftp.gnu.org/gnu/mpfr/mpfr-3.1.6.tar.bz2 | cf4f4b2d80abb79e820e78c8077b6725bbbb4e8f41896783c899087be0e94068 |
| mpc | MPC 1.0.3 (GCC の前提) | https://ftp.gnu.org/gnu/mpc/mpc-1.0.3.tar.gz | 617decc6ea09889fb08ede330917a00b16809b8db88c29c31bfbb49cbf88ecc3 |

bzip2とzlibのSHA-256は公開済みの値を使用し、2026-08-10の取得時に照合した。GCC 4.7.4は事前に使用可能なSHA-256を記録していなかったため、GNU公式配布元をtrust bootstrapとして2026-08-31に初回取得し、そのSHA-256を固定した。以後、`fetch.sh`は記録値と一致しない取得物を削除して失敗する。

gmp・mpfr・mpc は 2026-09-24 に取得し、GNU の配布元が置く署名 (`.sig`) を GNU keyring (https://ftp.gnu.org/gnu/gnu-keyring.gpg) で `gpgv` により検証したうえで SHA-256 を固定した。署名者はそれぞれ Niels Möller (GMP)、Vincent Lefèvre (MPFR)、Andreas Enge (MPC) である。

**版は GCC 4.7.4 の指定 (`contrib/download_prerequisites`: GMP 4.3.2 / MPFR 2.4.2 / MPC 0.8.1) から変えている。** 理由は 2 つある。

- MPC 0.8.1 は GNU の配布元に無く (GNU の配布元にあるのは 1.0.1 以降)、置かれている gcc.gnu.org / multiprecision.org はこの作業環境の egress 制限で到達できない
- 指定版には既知の脆弱性がある。GMP 6.2.1 以前の CVE-2021-43618 (`mpz_inp_raw` の整数オーバーフロー) と、MPFR 3.1.2-p11 より前の CVE-2014-9474 (`mpfr_strtofr` のバッファオーバーフロー)。GCC を stone でビルドする段階ではこれらも stone でコンパイルする対象になるので、修正済みの版を採った

GCC 4.7.4 の要求は「GMP 4.3.2・MPFR 2.4.2・MPC 0.8.1 **以降**」であり、採った版はその範囲に入る。GNU MPC (libmpc) について既知の CVE は見つからなかった。

**tcc だけは書庫ではなく git で取る。** savannah の配布ファイルは要求ごとに
別のミラーへ転送し，転送先が一定しないため取得元を固定できない。git なら
取得元が 1 つに決まり，commit が tree の内容ハッシュとして照合値になる (git 自身が
取得時に検証する)。ただし **git の commit は SHA-1** であり，書庫の
SHA-256 より照合値としては弱い (docs/stage015-tcc.md 3 章)。

zlib の取得元は当初 www.zlib.net/zlib-1.3.1.tar.gz だったが，zlib.net は
新版が出ると旧版を fossils/ へ移す (404 になる) ため恒久 URL の方を
記録している。
