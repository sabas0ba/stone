// sfs2 イメージの構築と読み出し (tools/sfs2.sh の JS 版)。
// 形式は docs/stage016-os.md 6.3:
//   スーパーブロック: 'sfs2' @0, total @4, tbloff (=32) @8, tblcnt @12, cursor @16
//   表項目 (64 バイト): name[48] (NUL 終端), parent u32, dataoff u32,
//                       len u32, flags u32 (1=有効, 2=ディレクトリ)
// 索引 0 はルート (名前は空・親は自分自身)。経路は親を辿って組み立てる。
// データは追記割付け・4 バイト境界。置き場所は sfs1 と同じ (SFS_OFFSET)。
//
// 項目の並び順は tools/sfs2.sh と同一にしてある (ディレクトリを深さ順・
// 経路順に，そのあとファイルを経路順)。同じ木からは同じイメージが出る。
import { SFS_OFFSET } from './sfs.js';

export { SFS_OFFSET };

const TBLOFF = 32;
const ENTSZ = 64;
const NAMEMAX = 47;
const F_USED = 1;
const F_DIR = 2;

const depth = (p) => p.split('/').length;

// 経路の親をすべて洗い出す ('a/b/c.h' -> 'a', 'a/b')
function ancestors(path) {
    const parts = path.split('/');
    const out = [];
    for (let i = 1; i < parts.length; i++) out.push(parts.slice(0, i).join('/'));
    return out;
}

// files: [{name: 'src/a/x.c', data}] | [{name: 'src/a', dir: true}]
// 親のディレクトリは明示しなくても補う。
// 返り値: Uint8Array (イメージ)
export function packSfs2(files, size = 4 << 20, maxEntries = 256) {
    const dataStart = TBLOFF + maxEntries * ENTSZ;
    if (dataStart >= size) throw new Error('size too small for the entry table');

    const dirs = new Set();
    const plain = [];
    for (const f of files) {
        const name = f.name.replace(/^\/+/, '');
        if (!name) throw new Error('empty name');
        for (const a of ancestors(name)) dirs.add(a);
        if (f.dir) dirs.add(name);
        else plain.push({ name, data: f.data });
    }

    // tools/sfs2.sh と同じ並び: ディレクトリは深さ順・経路順，次にファイル
    const dirList = [...dirs].sort((a, b) =>
        (depth(a) - depth(b)) || (a < b ? -1 : a > b ? 1 : 0));
    plain.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));

    const img = new Uint8Array(size);
    const dv = new DataView(img.buffer);
    const enc = new TextEncoder();
    img.set(enc.encode('sfs2'), 0);
    dv.setUint32(4, size, true);
    dv.setUint32(8, TBLOFF, true);
    dv.setUint32(12, maxEntries, true);

    const index = new Map([['', 0]]);       // 経路 -> 項目番号 (ルートは空文字)
    let n = 0;
    const put = (name, parent, off, len, flags) => {
        if (n >= maxEntries) throw new Error(`too many entries (max ${maxEntries})`);
        const eo = TBLOFF + n * ENTSZ;
        const nb = enc.encode(name);
        if (nb.length > NAMEMAX) throw new Error(`name too long (${nb.length} bytes): ${name}`);
        img.set(nb, eo);
        dv.setUint32(eo + 48, parent, true);
        dv.setUint32(eo + 52, off, true);
        dv.setUint32(eo + 56, len, true);
        dv.setUint32(eo + 60, flags, true);
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

    put('', 0, 0, 0, F_USED | F_DIR);       // 索引 0 = ルート (親は自分自身)
    for (const d of dirList) {
        const [pi, base] = parentOf(d);
        index.set(d, put(base, pi, 0, 0, F_USED | F_DIR));
    }
    let cur = dataStart;
    for (const f of plain) {
        const [pi, base] = parentOf(f.name);
        if (cur + f.data.length > size) throw new Error(`image full at: ${f.name}`);
        put(base, pi, cur, f.data.length, F_USED);
        img.set(f.data, cur);
        cur = (cur + f.data.length + 3) & ~3;
    }
    dv.setUint32(16, cur, true);
    return img;
}

// イメージ -> [{path, dir, data}] (ルートは含めない。経路順ではなく項目順)
export function unpackSfs2(img) {
    const dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
    const dec = new TextDecoder();
    if (dec.decode(img.subarray(0, 4)) !== 'sfs2') throw new Error('not an sfs2 image');
    const tbl = dv.getUint32(8, true);
    const cnt = dv.getUint32(12, true);
    const path = new Map([[0, '']]);
    const out = [];
    for (let i = 0; i < cnt; i++) {
        const eo = tbl + i * ENTSZ;
        const flags = dv.getUint32(eo + 60, true);
        if (!(flags & F_USED)) continue;
        let end = eo;
        while (end < eo + NAMEMAX + 1 && img[end] !== 0) end++;
        const name = dec.decode(img.subarray(eo, end));
        if (i === 0) continue;                      // ルート
        const parent = dv.getUint32(eo + 48, true);
        const base = path.get(parent);
        if (base === undefined) throw new Error(`bad parent in image: entry ${i}`);
        const full = base === '' ? name : `${base}/${name}`;
        path.set(i, full);
        if (flags & F_DIR) {
            out.push({ path: full, dir: true, data: new Uint8Array(0) });
        } else {
            const off = dv.getUint32(eo + 52, true);
            const len = dv.getUint32(eo + 56, true);
            out.push({ path: full, dir: false, data: img.slice(off, off + len) });
        }
    }
    return out;
}
