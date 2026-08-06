#!/usr/bin/env python3
# asm 言語 (RV32IM アセンブラ) の参照実装 (独立監査用)。
# 仕様: docs/stage003-asm.md 2 章。
#
# tmp/build/asm.bin とは独立に，仕様書だけから書き起こした実装である。
# 全命令エンコードテストの期待値再導出と自己アセンブル固定点の再現に用いる
# (verify/audit/README.md)。ビルド経路には使用しない。
#
# 使用法: python3 asm.py < input > output
# 終了コード (仕様 2.6 と同一):
#   0 正常 / 1 不正な文字・構文 / 2 未知のニーモニック / 3 未定義ラベル /
#   4 重複定義 / 5 分岐オフセット範囲外・奇数 / 6 即値範囲外 / 7 不正レジスタ
import sys

BASE = 0x80000000
WS = b' \t\r\n,'
NAMEHEAD = frozenset(b'abcdefghijklmnopqrstuvwxyz_')
NAMECHAR = frozenset(b'abcdefghijklmnopqrstuvwxyz0123456789_')
DEC = frozenset(b'0123456789')
HEX = frozenset(b'0123456789abcdef')


class Err(Exception):
    def __init__(self, code):
        self.code = code


def tokenize(data):
    """トークン列: ('labeldef', name) / ('name', s) / ('num', v) /
    ('lparen',) / ('rparen',)。終端 '.' で打ち切る。"""
    toks = []
    i = 0
    n = len(data)
    while i < n:
        c = data[i]
        if c in WS:
            i += 1
        elif c == ord('#'):
            while i < n and data[i] != ord('\n'):
                i += 1
        elif c == ord('.'):
            return toks
        elif c == ord('('):
            toks.append(('lparen',))
            i += 1
        elif c == ord(')'):
            toks.append(('rparen',))
            i += 1
        elif c in NAMEHEAD:
            s = i
            i += 1
            while i < n and data[i] in NAMECHAR:
                i += 1
            name = bytes(data[s:i])
            if len(name) > 15:
                raise Err(1)
            if i < n and data[i] == ord(':'):
                toks.append(('labeldef', name))
                i += 1
            else:
                toks.append(('name', name))
        elif c in DEC or c == ord('-'):
            neg = c == ord('-')
            if neg:
                i += 1
                if i >= n or data[i] not in DEC:
                    raise Err(1)      # 数値の桁欠落
            if data[i] == ord('0') and i + 1 < n and data[i + 1] == ord('x'):
                i += 2
                s = i
                while i < n and data[i] in HEX:
                    i += 1
                if i == s:
                    raise Err(1)      # '0x' のみ
                v = int(data[s:i], 16)
            else:
                s = i
                while i < n and data[i] in DEC:
                    i += 1
                v = int(data[s:i])
            # 数値の直後に名前文字が続くのは不正 ('123abc' 等)
            if i < n and data[i] in NAMECHAR:
                raise Err(1)
            toks.append(('num', -v if neg else v))
        else:
            raise Err(1)
    raise Err(1)      # 終端 '.' を含まない入力 (参照実装ではエラー扱い)


# 命令表: 名前 -> (形式, base word)
R = {'add': (0, 0x00), 'sub': (0, 0x20), 'sll': (1, 0x00), 'slt': (2, 0x00),
     'sltu': (3, 0x00), 'xor': (4, 0x00), 'srl': (5, 0x00), 'sra': (5, 0x20),
     'or': (6, 0x00), 'and': (7, 0x00),
     'mul': (0, 0x01), 'mulh': (1, 0x01), 'mulhsu': (2, 0x01),
     'mulhu': (3, 0x01), 'div': (4, 0x01), 'divu': (5, 0x01),
     'rem': (6, 0x01), 'remu': (7, 0x01)}
I = {'addi': 0, 'slti': 2, 'sltiu': 3, 'xori': 4, 'ori': 6, 'andi': 7}
SH = {'slli': (1, 0x00), 'srli': (5, 0x00), 'srai': (5, 0x20)}
LD = {'lb': (0, 0x03), 'lh': (1, 0x03), 'lw': (2, 0x03), 'lbu': (4, 0x03),
      'lhu': (5, 0x03), 'jalr': (0, 0x67)}
S = {'sb': 0, 'sh': 1, 'sw': 2}
B = {'beq': 0, 'bne': 1, 'blt': 4, 'bge': 5, 'bltu': 6, 'bgeu': 7}
U = {'lui': 0x37, 'auipc': 0x17}
FX = {'fence': 0x0ff0000f, 'ecall': 0x00000073, 'ebreak': 0x00100073}


class Parser:
    def __init__(self, toks):
        self.toks = toks
        self.i = 0

    def next(self):
        if self.i >= len(self.toks):
            raise Err(1)
        t = self.toks[self.i]
        self.i += 1
        return t

    def reg(self):
        t = self.next()
        if t[0] != 'name' or len(t[1]) < 2 or t[1][0] != ord('x') \
                or not all(c in DEC for c in t[1][1:]):
            raise Err(7)
        v = int(t[1][1:])
        if v > 31:
            raise Err(7)
        return v

    def imm(self, lo, hi):
        t = self.next()
        if t[0] != 'num':
            raise Err(1)
        if not lo <= t[1] <= hi:
            raise Err(6)
        return t[1]

    def label(self):
        t = self.next()
        if t[0] != 'name':
            raise Err(1)
        return t[1]

    def expect(self, kind):
        if self.next()[0] != kind:
            raise Err(1)


def enc_b(f3, rs1, rs2, rel):
    if rel % 2 != 0 or not -4096 <= rel <= 4094:
        raise Err(5)
    return ((rel >> 12) & 1) << 31 | ((rel >> 5) & 0x3f) << 25 \
        | rs2 << 20 | rs1 << 15 | f3 << 12 \
        | ((rel >> 1) & 0xf) << 8 | ((rel >> 11) & 1) << 7 | 0x63


def enc_j(rd, rel):
    if rel % 2 != 0 or not -1048576 <= rel <= 1048574:
        raise Err(5)
    return ((rel >> 20) & 1) << 31 | ((rel >> 1) & 0x3ff) << 21 \
        | ((rel >> 11) & 1) << 20 | ((rel >> 12) & 0xff) << 12 \
        | rd << 7 | 0x6f


def enc_i(op, f3, rd, rs1, imm):
    return (imm & 0xfff) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op


def li_words(rd, imm):
    hi = ((imm + 0x800) >> 12) & 0xfffff
    return [hi << 12 | rd << 7 | 0x37, enc_i(0x13, 0, rd, rd, imm & 0xfff)]


def assemble(toks):
    # 各パスは同一の走査 (仕様 3.1)。emit=None が pass 1 (サイズ計算のみ)
    def scan(labels, emit):
        p = Parser(toks)
        addr = 0

        def resolve(name):
            if emit is None:
                return 0
            if name not in labels:
                raise Err(3)
            return labels[name]

        def put(words):
            nonlocal addr
            if emit is not None:
                for w in words:
                    emit.extend((w & 0xffffffff).to_bytes(4, 'little'))
            addr += 4 * len(words)

        while p.i < len(p.toks):
            t = p.next()
            if t[0] == 'labeldef':
                if emit is None:
                    if t[1] in labels:
                        raise Err(4)
                    labels[t[1]] = addr
                continue
            if t[0] != 'name':
                raise Err(1)
            m = t[1].decode()
            if m in R:
                f3, f7 = R[m]
                rd, rs1, rs2 = p.reg(), p.reg(), p.reg()
                put([f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12
                     | rd << 7 | 0x33])
            elif m in I:
                rd, rs1 = p.reg(), p.reg()
                put([enc_i(0x13, I[m], rd, rs1, p.imm(-2048, 2047))])
            elif m in SH:
                f3, f7 = SH[m]
                rd, rs1 = p.reg(), p.reg()
                put([f7 << 25 | p.imm(0, 31) << 20 | rs1 << 15 | f3 << 12
                     | rd << 7 | 0x13])
            elif m in LD:
                f3, op = LD[m]
                rd = p.reg()
                imm = p.imm(-2048, 2047)
                p.expect('lparen')
                rs1 = p.reg()
                p.expect('rparen')
                put([enc_i(op, f3, rd, rs1, imm)])
            elif m in S:
                rs2 = p.reg()
                imm = p.imm(-2048, 2047)
                p.expect('lparen')
                rs1 = p.reg()
                p.expect('rparen')
                put([((imm >> 5) & 0x7f) << 25 | rs2 << 20 | rs1 << 15
                     | S[m] << 12 | (imm & 0x1f) << 7 | 0x23])
            elif m in B:
                rs1, rs2 = p.reg(), p.reg()
                rel = resolve(p.label()) - addr
                put([enc_b(B[m], rs1, rs2, rel) if emit is not None else 0])
            elif m in U:
                rd = p.reg()
                put([p.imm(0, 0xfffff) << 12 | rd << 7 | U[m]])
            elif m == 'jal':
                rd = p.reg()
                rel = resolve(p.label()) - addr
                put([enc_j(rd, rel) if emit is not None else 0])
            elif m in FX:
                put([FX[m]])
            elif m == 'nop':
                put([enc_i(0x13, 0, 0, 0, 0)])
            elif m == 'mv':
                rd, rs = p.reg(), p.reg()
                put([enc_i(0x13, 0, rd, rs, 0)])
            elif m == 'li':
                rd = p.reg()
                put(li_words(rd, p.imm(-0x80000000, 0xffffffff)))
            elif m == 'la':
                rd = p.reg()
                target = resolve(p.label())
                put(li_words(rd, BASE + target) if emit is not None
                    else [0, 0])
            elif m == 'j' or m == 'call':
                rd = 0 if m == 'j' else 1
                rel = resolve(p.label()) - addr
                put([enc_j(rd, rel) if emit is not None else 0])
            elif m == 'jr' or m == 'ret':
                rs = p.reg() if m == 'jr' else 1
                put([enc_i(0x67, 0, 0, rs, 0)])
            elif m == 'beqz' or m == 'bnez':
                rs = p.reg()
                rel = resolve(p.label()) - addr
                f3 = 0 if m == 'beqz' else 1
                put([enc_b(f3, rs, 0, rel) if emit is not None else 0])
            elif m == 'word':
                t = p.next()
                if t[0] == 'num':
                    if not -0x80000000 <= t[1] <= 0xffffffff:
                        raise Err(6)
                    v = t[1]
                elif t[0] == 'name':
                    v = 0 if emit is None else BASE + resolve(t[1])
                else:
                    raise Err(1)
                put([v])
            elif m == 'byte':
                v = p.imm(-128, 255)
                if emit is not None:
                    emit.append(v & 0xff)
                addr += 1
            else:
                raise Err(2)
        return addr

    labels = {}
    scan(labels, None)              # pass 1
    out = bytearray()
    scan(labels, out)               # pass 2
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
