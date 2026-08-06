#!/usr/bin/env python3
# hex0 言語の参照実装 (独立監査用)。仕様: docs/stage001-hex0.md 2 章。
#
# stage001/hex0.bin とは独立に，仕様書だけから書き起こした実装である。
# 両者の出力一致をもって「listing・バイナリ・仕様の三者整合」の裏を取る
# (verify/audit/README.md)。ビルド経路には使用しない。
#
# 使用法: python3 hex0.py < input > output
# 終了コード: 0 正常 / 1 不正文字 / 2 奇数桁で終端 (仕様 2.3 と同一)
import sys


def run(data, out):
    hexdigs = b'0123456789abcdef'
    ws = b' \t\r\n'
    hi = -1          # 組立て中の上位桁 (-1: 上位桁待ち)
    i = 0
    n = len(data)
    while i < n:
        c = data[i]
        if c in hexdigs:
            v = hexdigs.index(c)
            if hi < 0:
                hi = v
            else:
                out.append(hi * 16 + v)
                hi = -1
            i += 1
            continue
        if c == ord('.'):
            # 桁間の終端はエラー 2
            return 2 if hi >= 0 else 0
        # 桁間 (上位桁のみ読んだ状態) には何も挟めない (仕様 2.1)
        if hi >= 0:
            return 1
        if c == ord('#'):
            while i < n and data[i] != ord('\n'):
                i += 1
            continue
        if c in ws:
            i += 1
            continue
        return 1
    # 終端 '.' を含まない入力。実機は入力を待ち続けるが，参照実装は
    # ファイル入力なのでエラーとして扱う (監査対象のソースには現れない)
    return 1


def main():
    data = sys.stdin.buffer.read()
    out = bytearray()
    rc = run(data, out)
    sys.stdout.buffer.write(bytes(out))
    return rc


if __name__ == '__main__':
    sys.exit(main())
