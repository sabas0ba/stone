#!/usr/bin/env python3
# hex1 言語の参照実装 (独立監査用)。仕様: docs/stage002-hex1.md 2 章。
#
# tmp/build/hex1.bin とは独立に，仕様書だけから書き起こした実装である。
# hex1(asm.hex1) 等の出力一致をもって記録 SHA-256 の裏を取る
# (verify/audit/README.md)。ビルド経路には使用しない。
#
# 使用法: python3 hex1.py < input > output
# 終了コード (仕様 2.3 と同一):
#   0 正常 / 1 不正文字 / 2 奇数桁で終端 / 3 未定義ラベル /
#   4 重複定義 / 5 オフセット範囲外・奇数
import sys

HEXDIGS = b'0123456789abcdef'
WS = b' \t\r\n'
NAMECHARS = frozenset(b'abcdefghijklmnopqrstuvwxyz0123456789_')
BASE = 0x80000000


class Err(Exception):
    def __init__(self, code):
        self.code = code


def tokenize(data):
    """入力を要素列へ変換する。
    要素: ('byte', v) / ('label', name) / ('bref'|'jref', name, tmpl) /
          ('wref', name)
    """
    elems = []
    i = 0
    n = len(data)

    def skip_ws_comment(i):
        while i < n:
            c = data[i]
            if c in WS:
                i += 1
            elif c == ord('#'):
                while i < n and data[i] != ord('\n'):
                    i += 1
            else:
                break
        return i

    def read_name(i):
        s = i
        while i < n and data[i] in NAMECHARS:
            i += 1
        name = data[s:i]
        if not 1 <= len(name) <= 15:
            raise Err(1)
        # name の直後は ws またはコメント開始でなければならない (仕様 2.2)
        if i < n and data[i] not in WS and data[i] != ord('#'):
            raise Err(1)
        return bytes(name), i

    def read_tmpl(i):
        # imm フィールドを 0 とした命令語 4 バイト (LE)。byte 間に ws/comment 可
        word = 0
        for k in range(4):
            i = skip_ws_comment(i)
            if i + 1 >= n or data[i] not in HEXDIGS:
                raise Err(1)
            if data[i + 1] not in HEXDIGS:
                raise Err(2 if data[i + 1] == ord('.') else 1)
            v = HEXDIGS.index(data[i]) * 16 + HEXDIGS.index(data[i + 1])
            word |= v << (8 * k)
            i += 2
        return word, i

    while i < n:
        c = data[i]
        if c in HEXDIGS:
            if i + 1 >= n:
                raise Err(1)
            c2 = data[i + 1]
            if c2 not in HEXDIGS:
                # 桁間には何も挟めない。終端 '.' はエラー 2 (hex0 と同一)
                raise Err(2 if c2 == ord('.') else 1)
            elems.append(('byte', HEXDIGS.index(c) * 16 + HEXDIGS.index(c2)))
            i += 2
        elif c == ord('.'):
            return elems
        elif c == ord('#'):
            while i < n and data[i] != ord('\n'):
                i += 1
        elif c in WS:
            i += 1
        elif c == ord(':'):
            name, i = read_name(i + 1)
            elems.append(('label', name))
        elif c == ord('!') or c == ord('$'):
            kind = 'bref' if c == ord('!') else 'jref'
            name, i = read_name(i + 1)
            tmpl, i = read_tmpl(i)
            elems.append((kind, name, tmpl))
        elif c == ord('&'):
            name, i = read_name(i + 1)
            elems.append(('wref', name))
        else:
            raise Err(1)
    raise Err(1)      # 終端 '.' を含まない入力 (参照実装ではエラー扱い)


def assemble(elems):
    # pass 1: アドレス計算とラベル登録
    labels = {}
    addr = 0
    for e in elems:
        if e[0] == 'label':
            if e[1] in labels:
                raise Err(4)
            labels[e[1]] = addr
        elif e[0] == 'byte':
            addr += 1
        else:
            addr += 4

    def resolve(name):
        if name not in labels:
            raise Err(3)
        return labels[name]

    # pass 2: 出力
    out = bytearray()
    addr = 0
    for e in elems:
        if e[0] == 'byte':
            out.append(e[1])
            addr += 1
        elif e[0] == 'label':
            pass
        elif e[0] == 'wref':
            out += (BASE + resolve(e[1])).to_bytes(4, 'little')
            addr += 4
        else:
            rel = resolve(e[1]) - addr
            if rel % 2 != 0:
                raise Err(5)
            if e[0] == 'bref':
                if not -4096 <= rel <= 4094:
                    raise Err(5)
                word = e[2] \
                    | ((rel >> 12) & 1) << 31 \
                    | ((rel >> 5) & 0x3f) << 25 \
                    | ((rel >> 1) & 0xf) << 8 \
                    | ((rel >> 11) & 1) << 7
            else:
                if not -1048576 <= rel <= 1048574:
                    raise Err(5)
                word = e[2] \
                    | ((rel >> 20) & 1) << 31 \
                    | ((rel >> 1) & 0x3ff) << 21 \
                    | ((rel >> 11) & 1) << 20 \
                    | ((rel >> 12) & 0xff) << 12
            out += word.to_bytes(4, 'little')
            addr += 4
    return bytes(out)


def main():
    data = sys.stdin.buffer.read()
    try:
        out = assemble(tokenize(data))
    except Err as e:
        return e.code
    sys.stdout.buffer.write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
