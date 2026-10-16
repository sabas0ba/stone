// sfs4 イメージの構築と読み出し (tools/sfs4.sh の JS 版)。
// 形式は docs/stage017-gcc.md 7.1: sfs3 との差は名前の枠 (48 -> 104 バイト，
// 項目 72 -> 128 バイト) だけなので，実装は web/sfs3.js の makeSfs を使う。
import { makeSfs } from './sfs3.js';

const sfs4 = makeSfs('sfs4', 128, 104);
export const packSfs4 = sfs4.pack;
export const unpackSfs4 = sfs4.unpack;
