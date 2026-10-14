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
| linux54 | Linux 5.4.302 (Stage 18 / 19 の測定対象。RV32 のカーネルを我々のチェーンで組む)。stable の tag v5.4.302 を commit で固定 | https://github.com/gregkh/linux | commit 9e3157c56ec7917e6a80ea53a8bd752e0037f2cb |
| flex | flex 2.6.4。照合用の上流 kconfig (`conf`) を組むためだけに使う (`tools/kconfref.sh`)。stone のビルドチェーンにもカーネルにも入らない | https://github.com/westes/flex/releases/download/v2.6.4/flex-2.6.4.tar.gz | e87aae032bf07c26f85ac0ed3250998c37621d95f8bd748b31f15b33c45ee995 |
| psabi | RISC-V ELF psABI Specification 1.0 (AsciiDoc の原稿)。ncc1 / nas1 / nld1 の呼出し規約・ELF・再配置の一次資料 (docs/stage018-ncc.md) | https://github.com/riscv-non-isa/riscv-elf-psabi-doc | tag v1.0, commit 74936a9e25dec3c5b297b19e4c37e97d14868b22 |
| asmman | RISC-V Assembly Programmer's Manual (AsciiDoc の原稿)。疑似命令・修飾子・`.option` の一次資料 | https://github.com/riscv-non-isa/riscv-asm-manual | main, commit 7ae3a91c7baf12b1ac58d27885288a2450e6d2c6 |
| binutils | GNU binutils 2.40。**利用者向け文書 (`gas/doc/*.texi`・`ld/ld.texi`) だけを読む**。組まない・実行しない・ソースを参照しない | https://ftp.gnu.org/gnu/binutils/binutils-2.40.tar.bz2 | f8298eb153a4b37d112e945aa5cb2850040bcf26a3ea65b5a715c83afe05e48a |

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

**Linux も git で取る。** kernel.org (cdn / mirrors.edge / www / git) はこの作業環境の egress 制限で到達できないため，stable の保守者 Greg Kroah-Hartman が GitHub に置くミラー (gregkh/linux) から tag v5.4.302 の commit を取る (2026-10-09)。tag object は 8ffcca9e0f269ee86a682eaae7e49c5efae79fe9 である。**kernel.org の正規のリポジトリと同じ commit であることは未照合**であり，kernel.org に到達できるようになった時点で照合する。5.4 系は保守が終了しており，v5.4.302 が最終版である。

**5.4 系には修正されない既知の脆弱性が残る。** 我々はこのカーネルを QEMU の中で起動して試験するだけで，網に晒す用途に使わない。

**flex は照合の相手を組むためだけに取る** (2026-10-12)。Linux の kconfig は字句解析器を flex で作り，生成済みのものを同梱しないので，我々の kconf と突き合わせる上流の `conf` を組むのに要る。release の署名 (`.sig`) を Will Estes の鍵 (fingerprint 56C6 7868 E933 90AA 1039 AD1C E4B2 9C8D 6488 5307) で検証した。鍵は keyserver.ubuntu.com から取った (GitHub と keys.openpgp.org はこの作業環境の egress 制限で到達できない)。**この fingerprint を別経路の資料と突き合わせてはいない。** OSV で既知の脆弱性は CVE-2019-6293 の 1 件で，flex が細工された `.l` を処理すると再帰で stack を使い果たすものである (2.6.4 で未修正)。入力は Linux の `scripts/kconfig/lexer.l` だけなので該当しないと判断した。生成される字句解析器の CVE-2016-6354 は 2.6.1 で修正済みである。NVD はこの作業環境から到達できず照会していない。保守者の侵害や悪意ある publish の報告は見つからなかった。

**psabi・asmman・binutils は処理系の入力ではなく，Stage 18 の処理系を書くための文書として取る** (2026-10-14。docs/stage018-ncc.md の「一次資料」)。psabi は批准版 1.0 の tag (lightweight tag で，commit を直接指す) を，asmman は版の tag が無いので main の commit (2026-10-05。公開から 7 日を経たもの) を固定した。binutils は 2.40 の書庫の署名 (`.sig`) を Nick Clifton の鍵 (fingerprint 3A24 BC1E 8FB4 09FA 9F14 3718 13FC EF89 DD9E 3C4F) で検証してから SHA-256 を固定した。鍵は keyserver.ubuntu.com から取り，**別経路の資料との突き合わせはしていない**。書庫には as / ld のソースも入るが，[plan.md](plan.md) 2.1 (既存の as / ld を使わない，既存のコードを参照しない) により，展開したもののうち texinfo の文書だけを読む。3 点とも組まず実行もしないので，脆弱性の調査は対象外とした。

zlib の取得元は当初 www.zlib.net/zlib-1.3.1.tar.gz だったが，zlib.net は
新版が出ると旧版を fossils/ へ移す (404 になる) ため恒久 URL の方を
記録している。
