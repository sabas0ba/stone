// sfs3 / sfs4 イメージの構築と読み出し (tools/sfs3.sh / tools/sfs4.sh の JS 版)。
//
// sfs3 (docs/stage017-cc.md 11.3) は sfs2 との差が**項目が更新時刻を持つ**
// ことだけである。項目は 64 -> 72 バイトに広がり，末尾に mtlo / mthi
// (epoch からのナノ秒を u32 2 本で持つ) が付いた。
// sfs4 (docs/stage017-gcc.md 7.1) は sfs3 との差が**名前の枠が広がった**
// ことだけである。項目は 72 -> 128 バイト，名前は 48 -> 104 バイト。
// 頭の 32 バイトと，名前より後ろの 6 語の順はどちらも同じ。
//
//   スーパーブロック: magic @0, total @4, tbloff (=32) @8, tblcnt @12, cursor @16
//   表項目: name[namesz] (NUL 終端), parent u32, dataoff u32, len u32,
//           flags u32 (1=有効, 2=ディレクトリ), mtlo u32, mthi u32
//
// 項目の並び順はホスト側のツールと同一にしてある (ディレクトリを深さ順・
// 経路順に，そのあとファイルを経路順)。同じツリーと同じ時刻からは同じ
// イメージが出る (web/test-emu.mjs が tools/sfs3.sh / sfs4.sh と突き合わせる)。
//
// 時刻は BigInt のナノ秒で受け渡す。ホストのツールは触れたファイルの mtime
// を使うが，ここでは与えられたものだけを書く (無ければ 0)。
import { SFS_OFFSET } from './sfs.js';

export { SFS_OFFSET };

const TBLOFF = 32;
const F_USED = 1;
const F_DIR = 2;

const depth = (p) => p.split('/').length;

// 経路の親をすべて列挙する ('a/b/c.h' -> 'a', 'a/b')
function ancestors(path) {
    const parts = path.split('/');
    const out = [];
    for (let i = 1; i < parts.length; i++) out.push(parts.slice(0, i).join('/'));
    return out;
}

const toNs = (v) => (v == null ? 0n : BigInt(v));

// 1 つの形式 (magic・項目の幅・名前の枠) に対する pack / unpack を作る
export function makeSfs(magic, entsz, namesz) {
    const namemax = namesz - 1;
    const E_PAR = namesz, E_OFF = namesz + 4, E_LEN = namesz + 8;
    const E_FLAG = namesz + 12, E_MTLO = namesz + 16, E_MTHI = namesz + 20;

    // files: [{name: 'src/a/x.c', data, mtime?}] | [{name: 'src/a', dir: true, mtime?}]
    // 親のディレクトリは明示しなくても補う (時刻は 0)。
    // opts.rootMtime: ルート (索引 0) の時刻
    // 返り値: Uint8Array (イメージ)
    function pack(files, size = 4 << 20, maxEntries = 256, opts = {}) {
        const dataStart = TBLOFF + maxEntries * entsz;
        if (dataStart >= size) throw new Error('size too small for the entry table');

        const dirs = new Map();                 // 経路 -> 時刻
        const plain = [];
        for (const f of files) {
            const name = f.name.replace(/^\/+/, '');
            if (!name) throw new Error('empty name');
            for (const a of ancestors(name)) if (!dirs.has(a)) dirs.set(a, 0n);
            if (f.dir) dirs.set(name, toNs(f.mtime));
            else plain.push({ name, data: f.data, mtime: toNs(f.mtime) });
        }

        // ホスト側と同じ並び: ディレクトリは深さ順・経路順，次にファイル
        const dirList = [...dirs.keys()].sort((a, b) =>
            (depth(a) - depth(b)) || (a < b ? -1 : a > b ? 1 : 0));
        plain.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));

        const img = new Uint8Array(size);
        const dv = new DataView(img.buffer);
        const enc = new TextEncoder();
        img.set(enc.encode(magic), 0);
        dv.setUint32(4, size, true);
        dv.setUint32(8, TBLOFF, true);
        dv.setUint32(12, maxEntries, true);

        const index = new Map([['', 0]]);       // 経路 -> 項目番号 (ルートは空文字)
        let n = 0;
        const put = (name, parent, off, len, flags, ns) => {
            if (n >= maxEntries) throw new Error(`too many entries (max ${maxEntries})`);
            const eo = TBLOFF + n * entsz;
            const nb = enc.encode(name);
            if (nb.length > namemax) throw new Error(`name too long (${nb.length} bytes): ${name}`);
            img.set(nb, eo);
            dv.setUint32(eo + E_PAR, parent, true);
            dv.setUint32(eo + E_OFF, off, true);
            dv.setUint32(eo + E_LEN, len, true);
            dv.setUint32(eo + E_FLAG, flags, true);
            dv.setUint32(eo + E_MTLO, Number(ns & 0xffffffffn), true);
            dv.setUint32(eo + E_MTHI, Number((ns >> 32n) & 0xffffffffn), true);
            return n++;
        };
        const split = (p) => {
            const k = p.lastIndexOf('/');
            return k < 0 ? ['', p] : [p.slice(0, k), p.slice(k + 1)];
        };
        const parentOf = (p) => {
            const [dir, base] = split(p);
            const pi = index.get(dir);
            if (pi === undefined) throw new Error(`parent not found: ${dir} (for ${p})`);
            return [pi, base];
        };

        put('', 0, 0, 0, F_USED | F_DIR, toNs(opts.rootMtime));   // 索引 0 = ルート
        for (const d of dirList) {
            const [pi, base] = parentOf(d);
            index.set(d, put(base, pi, 0, 0, F_USED | F_DIR, dirs.get(d)));
        }
        let cur = dataStart;
        for (const f of plain) {
            const [pi, base] = parentOf(f.name);
            if (cur + f.data.length > size) throw new Error(`image full at: ${f.name}`);
            put(base, pi, cur, f.data.length, F_USED, f.mtime);
            img.set(f.data, cur);
            cur = (cur + f.data.length + 3) & ~3;
        }
        dv.setUint32(16, cur, true);
        return img;
    }

    // イメージ -> [{path, dir, data, mtime}] (ルートは含めない。項目順)
    function unpack(img) {
        const dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
        const dec = new TextDecoder();
        if (dec.decode(img.subarray(0, 4)) !== magic) throw new Error(`not an ${magic} image`);
        const tbl = dv.getUint32(8, true);
        const cnt = dv.getUint32(12, true);
        const path = new Map([[0, '']]);
        const out = [];
        for (let i = 0; i < cnt; i++) {
            const eo = tbl + i * entsz;
            const flags = dv.getUint32(eo + E_FLAG, true);
            if (!(flags & F_USED)) continue;
            let end = eo;
            while (end < eo + namesz && img[end] !== 0) end++;
            const name = dec.decode(img.subarray(eo, end));
            if (i === 0) continue;                      // ルート
            const parent = dv.getUint32(eo + E_PAR, true);
            const base = path.get(parent);
            if (base === undefined) throw new Error(`bad parent in image: entry ${i}`);
            const full = base === '' ? name : `${base}/${name}`;
            path.set(i, full);
            const mtime = (BigInt(dv.getUint32(eo + E_MTHI, true)) << 32n)
                | BigInt(dv.getUint32(eo + E_MTLO, true));
            if (flags & F_DIR) {
                out.push({ path: full, dir: true, data: new Uint8Array(0), mtime });
            } else {
                const off = dv.getUint32(eo + E_OFF, true);
                const len = dv.getUint32(eo + E_LEN, true);
                out.push({ path: full, dir: false, data: img.slice(off, off + len), mtime });
            }
        }
        return out;
    }

    return { pack, unpack };
}

const sfs3 = makeSfs('sfs3', 72, 48);
export const packSfs3 = sfs3.pack;
export const unpackSfs3 = sfs3.unpack;
