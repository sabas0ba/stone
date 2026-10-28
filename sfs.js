// sfs イメージの構築と読み出し (tools/sfs.sh の JS 版)。
// 形式は docs/stage012-os.md 4 章:
//   スーパーブロック: 'sfs1' @0, total @4, tbloff (=32) @8, tblcnt @12, cursor @16
//   表項目 (64 バイト): name[52] (NUL 終端), dataoff u32, len u32, flags u32 (1=有効)
// データは追記割付け・4 バイト境界。ゲスト物理 0x8400_0000 (RAM オフセット
// 0x0400_0000) に置かれる。

export const SFS_OFFSET = 0x04000000;       // RAM 先頭からのオフセット
const TBLOFF = 32;
const ENTSZ = 64;

// files: [{name, data: Uint8Array}] -> Uint8Array (イメージ)
export function packSfs(files, size = 4 << 20, maxFiles = 128) {
    const img = new Uint8Array(size);
    const dv = new DataView(img.buffer);
    const enc = new TextEncoder();
    img.set(enc.encode('sfs1'), 0);
    dv.setUint32(4, size, true);
    dv.setUint32(8, TBLOFF, true);
    dv.setUint32(12, maxFiles, true);
    let cur = TBLOFF + maxFiles * ENTSZ;
    if (files.length > maxFiles) throw new Error('too many files');
    files.forEach((f, i) => {
        const name = enc.encode(f.name);
        if (name.length > 51) throw new Error(`name too long: ${f.name}`);
        if (cur + f.data.length > size) throw new Error(`image full at: ${f.name}`);
        const eo = TBLOFF + i * ENTSZ;
        img.set(name, eo);
        dv.setUint32(eo + 52, cur, true);
        dv.setUint32(eo + 56, f.data.length, true);
        dv.setUint32(eo + 60, 1, true);
        img.set(f.data, cur);
        cur = (cur + f.data.length + 3) & ~3;
    });
    dv.setUint32(16, cur, true);
    return img;
}

// イメージ -> [{name, data}]
export function unpackSfs(img) {
    const dv = new DataView(img.buffer, img.byteOffset, img.byteLength);
    const dec = new TextDecoder();
    if (dec.decode(img.subarray(0, 4)) !== 'sfs1') throw new Error('not an sfs image');
    const tbl = dv.getUint32(8, true);
    const cnt = dv.getUint32(12, true);
    const files = [];
    for (let i = 0; i < cnt; i++) {
        const eo = tbl + i * ENTSZ;
        if (dv.getUint32(eo + 60, true) !== 1) continue;
        let end = eo;
        while (end < eo + 52 && img[end] !== 0) end++;
        const off = dv.getUint32(eo + 52, true);
        const len = dv.getUint32(eo + 56, true);
        files.push({
            name: dec.decode(img.subarray(eo, end)),
            data: img.slice(off, off + len),
        });
    }
    return files;
}
