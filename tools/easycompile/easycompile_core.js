// ---------------------------------------------------------------------------
// EasyCompile core: builds a bootable CannonBall CDI from the player's OutRun
// ROMs (and optional music) in the browser. Also loadable from Node for tests
// (module.exports). make_tool.py inlines this file into easycompile.html.
//
//   ROMs   : zips or loose files, matched by CRC-32 (any file names)
//   Music  : 44.1 kHz stereo 16-bit PCM in, Yamaha ADPCM WAV out
//            (the same as ffmpeg -c:a adpcm_yamaha, which the game plays)
//   Disc   : ISO 9660 at LBA 11702 with IP.BIN in the first 16 sectors,
//            wrapped as an audio/data CDI (the same layout as cdi4dc)
// ---------------------------------------------------------------------------
const EasyCore = (() => {
    // --- checksums ------------------------------------------------------------
    const crcT = new Uint32Array(256);
    for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; crcT[i] = c >>> 0; }
    const crc32 = (b) => { let c = 0xFFFFFFFF; for (let i = 0; i < b.length; i++) c = crcT[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };

    // --- the OutRun revision B set (from src/main/roms.cpp) -------------------
    // [name, size, crc, group]: group 0 required, 1 Japanese tracks, 2 fixed sound ROM
    const ROMS = [
        ['epr-10380b.133', 0x10000, 0x1f6cadad, 0], ['epr-10382b.118', 0x10000, 0xc4c3fa1a, 0],
        ['epr-10381b.132', 0x10000, 0xbe8c412b, 0], ['epr-10383b.117', 0x10000, 0x10a2014a, 0],
        ['epr-10327a.76', 0x10000, 0xe28a5baf, 0], ['epr-10329a.58', 0x10000, 0xda131c81, 0],
        ['epr-10328a.75', 0x10000, 0xd5ec5e5d, 0], ['epr-10330a.57', 0x10000, 0xba9ec82a, 0],
        ['opr-10268.99', 0x08000, 0x95344b04, 0], ['opr-10232.102', 0x08000, 0x776ba1eb, 0],
        ['opr-10267.100', 0x08000, 0xa85bb823, 0], ['opr-10231.103', 0x08000, 0x8908bcbf, 0],
        ['opr-10266.101', 0x08000, 0x9f6f1a74, 0], ['opr-10230.104', 0x08000, 0x686f5e50, 0],
        ['mpr-10371.9', 0x20000, 0x7cc86208, 0], ['mpr-10373.10', 0x20000, 0xb0d26ac9, 0],
        ['mpr-10375.11', 0x20000, 0x59b60bd7, 0], ['mpr-10377.12', 0x20000, 0x17a1b04a, 0],
        ['mpr-10372.13', 0x20000, 0xb557078c, 0], ['mpr-10374.14', 0x20000, 0x8051e517, 0],
        ['mpr-10376.15', 0x20000, 0xf3b8f318, 0], ['mpr-10378.16', 0x20000, 0xa1062984, 0],
        ['opr-10186.47', 0x08000, 0x22794426, 0], ['opr-10185.11', 0x08000, 0x22794426, 0],
        ['epr-10187.88', 0x08000, 0xa10abaa9, 0],
        ['opr-10193.66', 0x08000, 0xbcd10dde, 0], ['opr-10192.67', 0x08000, 0x770f1270, 0],
        ['opr-10191.68', 0x08000, 0x20a284ab, 0], ['opr-10190.69', 0x08000, 0x7cab70e2, 0],
        ['opr-10189.70', 0x08000, 0x01366b54, 0], ['opr-10188.71', 0x08000, 0xbad30ad9, 0],
        ['epr-10380.133', 0x10000, 0xe339e87a, 1], ['epr-10382.118', 0x10000, 0x65248dd5, 1],
        ['epr-10381.132', 0x10000, 0xbe8c412b, 1], ['epr-10383.117', 0x10000, 0xdcc586e7, 1],
        ['epr-10327.76', 0x10000, 0xda99d855, 1], ['epr-10329.58', 0x10000, 0xfe0fa5e2, 1],
        ['epr-10328.75', 0x10000, 0x3c0e9a7f, 1], ['epr-10330.57', 0x10000, 0x59786e99, 1],
        ['opr-10188.71f', 0x08000, 0x37598616, 2],
        ['opr-10188.71f', 0x08000, 0xc2de09b2, 2],     // the other repaired version the game accepts
    ];

    // --- zip reading (stored or deflate; zips inside zips too) ----------------
    const rd16 = (b, o) => b[o] | (b[o + 1] << 8);
    const rd32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
    async function inflateRaw(data) {
        const stream = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
        return new Uint8Array(await new Response(stream).arrayBuffer());
    }
    const isZip = (b) => b.length > 22 && b[0] === 0x50 && b[1] === 0x4B && b[2] === 3 && b[3] === 4;
    async function unzip(b) {
        let eocd = -1;
        for (let i = b.length - 22; i >= Math.max(0, b.length - 65557); i--)
            if (rd32(b, i) === 0x06054B50) { eocd = i; break; }
        if (eocd < 0) throw new Error('not a zip file');
        const count = rd16(b, eocd + 10);
        let p = rd32(b, eocd + 16);
        const out = [];
        for (let n = 0; n < count; n++) {
            if (rd32(b, p) !== 0x02014B50) break;
            const method = rd16(b, p + 10), csize = rd32(b, p + 20), nlen = rd16(b, p + 28);
            const xlen = rd16(b, p + 30), clen = rd16(b, p + 32), lho = rd32(b, p + 42);
            const name = new TextDecoder().decode(b.subarray(p + 46, p + 46 + nlen));
            p += 46 + nlen + xlen + clen;
            if (name.endsWith('/')) continue;
            const data0 = lho + 30 + rd16(b, lho + 26) + rd16(b, lho + 28);
            const raw = b.subarray(data0, data0 + csize);
            let data;
            if (method === 0) data = raw;
            else if (method === 8) data = await inflateRaw(raw);
            else throw new Error(`${name}: unsupported zip compression (re-zip it normally)`);
            out.push({ name: name.split('/').pop(), data });
        }
        return out;
    }

    // Sort any dropped files into ROMs by CRC. Returns {found: Map name->data, unknown: [names]}
    async function collectRoms(files, found = new Map(), unknown = []) {
        const byCrc = new Map();
        for (const r of ROMS) { const k = r[2] + ':' + r[1]; (byCrc.get(k) || byCrc.set(k, []).get(k)).push(r[0]); }
        const visit = async (name, data, depth) => {
            if (isZip(data) && depth < 3) {
                for (const e of await unzip(data)) await visit(e.name, e.data, depth + 1);
                return;
            }
            const names = byCrc.get(crc32(data) + ':' + data.length);
            if (names) names.forEach(n => found.set(n, data));
            else unknown.push(name);
        };
        for (const f of files) await visit(f.name, f.data, 0);
        return { found, unknown };
    }
    function romStatus(found) {
        const missing = ROMS.filter(r => r[3] === 0 && !found.has(r[0])).map(r => r[0]);
        const jap = ROMS.filter(r => r[3] === 1).every(r => found.has(r[0]));
        const fixedSound = found.has('opr-10188.71f');
        return { missing, required: ROMS.filter(r => r[3] === 0).length, jap, fixedSound };
    }

    // --- Yamaha ADPCM (as ffmpeg's adpcm_yamaha encoder) ----------------------
    const DIFF = [1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15];
    const SCALE = [230, 230, 230, 230, 307, 409, 512, 614, 230, 230, 230, 230, 307, 409, 512, 614];
    // left, right: Int16Array (same length). Returns a WAV file.
    function adpcmWav(left, right, rate = 44100) {
        const n = left.length, out = new Uint8Array(58 + n);
        const st = [{ p: 0, s: 127 }, { p: 0, s: 127 }];
        const enc = (c, x) => {
            const d = x - c.p;
            const nib = Math.min(7, Math.trunc(Math.abs(d) * 4 / c.s)) + (d < 0 ? 8 : 0);
            c.p += Math.trunc(c.s * DIFF[nib] / 8);
            c.p = c.p < -32768 ? -32768 : c.p > 32767 ? 32767 : c.p;
            c.s = (c.s * SCALE[nib]) >> 8;
            c.s = c.s < 127 ? 127 : c.s > 24576 ? 24576 : c.s;
            return nib;
        };
        for (let i = 0; i < n; i++) out[58 + i] = enc(st[0], left[i]) | (enc(st[1], right[i]) << 4);
        const w = (o, s) => { for (let i = 0; i < s.length; i++) out[o + i] = s.charCodeAt(i); };
        const le32 = (o, v) => { out[o] = v & 255; out[o + 1] = (v >>> 8) & 255; out[o + 2] = (v >>> 16) & 255; out[o + 3] = (v >>> 24) & 255; };
        const le16 = (o, v) => { out[o] = v & 255; out[o + 1] = (v >> 8) & 255; };
        w(0, 'RIFF'); le32(4, 50 + n); w(8, 'WAVE');
        w(12, 'fmt '); le32(16, 18); le16(20, 0x20); le16(22, 2); le32(24, rate); le32(28, rate);
        le16(32, 1024); le16(34, 4); le16(36, 0);
        w(38, 'fact'); le32(42, 4); le32(46, n);
        w(50, 'data'); le32(54, n);
        return out;
    }
    // 16-bit PCM WAV (as the recording tool writes) -> {rate, left, right}
    function readPcmWav(b) {
        const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
        let p = 12, rate = 0, ch = 0, data = null;
        while (p + 8 <= b.length) {
            const id = String.fromCharCode(b[p], b[p + 1], b[p + 2], b[p + 3]), sz = dv.getUint32(p + 4, true);
            if (id === 'fmt ') { ch = dv.getUint16(p + 10, true); rate = dv.getUint32(p + 12, true); }
            if (id === 'data') data = [p + 8, sz];
            p += 8 + sz + (sz & 1);
        }
        if (!data || !ch) throw new Error('bad recording');
        const n = Math.floor(data[1] / (2 * ch)), l = new Int16Array(n), r = new Int16Array(n);
        for (let i = 0; i < n; i++) {
            l[i] = dv.getInt16(data[0] + i * 2 * ch, true);
            r[i] = ch > 1 ? dv.getInt16(data[0] + i * 2 * ch + 2, true) : l[i];
        }
        return { rate, left: l, right: r };
    }
    // Float32 channels (already 44.1 kHz) to Int16
    function toInt16(f) {
        const o = new Int16Array(f.length);
        for (let i = 0; i < f.length; i++) { const v = Math.round(f[i] * 32767); o[i] = v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
        return o;
    }

    // --- ISO 9660 image (no Joliet/Rock Ridge: upper-case names, ";1") -------
    // tree: { name: Uint8Array | subtree }. Returns Uint8Array of the session
    // (IP.BIN in sectors 0-15), with every address offset by `lba` (11702).
    function buildIso(tree, ipbin, volume, lba = 11702) {
        const S = 2048;
        const isoName = (n, dir) => n.toUpperCase() + (dir ? '' : ';1');
        // Flatten directories (breadth-first for the path table)
        const dirs = [{ name: '', node: tree, parent: 0 }];
        for (let i = 0; i < dirs.length; i++)
            for (const k of Object.keys(dirs[i].node).sort((a, b) => a.toUpperCase() < b.toUpperCase() ? -1 : 1))
                if (!(dirs[i].node[k] instanceof Uint8Array)) dirs.push({ name: k, node: dirs[i].node[k], parent: i });
        const list = (d) => Object.keys(d.node)
            .map(k => ({ k, dir: !(d.node[k] instanceof Uint8Array), n: isoName(k, !(d.node[k] instanceof Uint8Array)) }))
            .sort((a, b) => a.n < b.n ? -1 : a.n > b.n ? 1 : 0);
        for (const d of dirs) d.entries = list(d);
        const entries = (d) => d.entries;
        const recLen = (nlen) => 33 + nlen + ((nlen & 1) ? 0 : 1);
        // Sizes: each directory's records, never across a sector boundary
        for (const d of dirs) {
            let size = 0, used = 0;
            const add = (len) => { if (used + len > S) { size += S; used = 0; } used += len; };
            add(34); add(34);
            for (const e of entries(d)) add(recLen(e.n.length));
            d.sectors = size / S + 1;
        }
        // Path table
        const ptLen = dirs.reduce((a, d) => a + 8 + (d.name ? d.name.length : 1) + ((d.name ? d.name.length : 1) & 1), 0);
        const ptSectors = Math.ceil(ptLen / S);
        let next = 18 + 2 * ptSectors;
        for (const d of dirs) { d.lba = next; next += d.sectors; }
        const files = [];
        for (const d of dirs) for (const e of entries(d)) if (!e.dir) {
            const data = d.node[e.k];
            e.lba = next; files.push({ d, e, data, lba: next });
            next += Math.max(1, Math.ceil(data.length / S));
        }
        const total = next;
        const img = new Uint8Array(total * S);
        img.set(ipbin.subarray(0, 16 * S));
        const b733 = (o, v) => { for (let i = 0; i < 4; i++) { img[o + i] = (v >>> (8 * i)) & 255; img[o + 7 - i] = (v >>> (8 * i)) & 255; } };
        const b723 = (o, v) => { img[o] = v & 255; img[o + 1] = v >> 8; img[o + 2] = v >> 8; img[o + 3] = v & 255; };
        const str = (o, s, n, pad = 0x20) => { for (let i = 0; i < n; i++) img[o + i] = i < s.length ? s.charCodeAt(i) : pad; };
        const now = new Date();
        const recDate = (o) => {
            img[o] = now.getUTCFullYear() - 1900; img[o + 1] = now.getUTCMonth() + 1; img[o + 2] = now.getUTCDate();
            img[o + 3] = now.getUTCHours(); img[o + 4] = now.getUTCMinutes(); img[o + 5] = now.getUTCSeconds(); img[o + 6] = 0;
        };
        const record = (o, extent, size, dir, nameBytes) => {
            const len = recLen(nameBytes.length);
            img[o] = len; img[o + 1] = 0; b733(o + 2, extent + lba); b733(o + 10, size);
            recDate(o + 18); img[o + 25] = dir ? 2 : 0; img[o + 26] = 0; img[o + 27] = 0; b723(o + 28, 1);
            img[o + 32] = nameBytes.length; img.set(nameBytes, o + 33);
            return len;
        };
        const enc = (s) => Uint8Array.from(s, c => c.charCodeAt(0));
        // Directories
        for (const d of dirs) {
            let o = d.lba * S, used = 0;
            const put = (extent, size, dir, nb) => {
                const len = recLen(nb.length);
                if (used + len > S) { o += S - used; used = 0; }
                record(o, extent, size, dir, nb); o += len; used += len;
            };
            const par = dirs[d.parent];
            put(d.lba, d.sectors * S, true, new Uint8Array([0]));
            put(par.lba, par.sectors * S, true, new Uint8Array([1]));
            for (const e of entries(d)) {
                if (e.dir) { const sub = dirs.find(x => x.node === d.node[e.k]); put(sub.lba, sub.sectors * S, true, enc(e.n)); }
                else put(e.lba, d.node[e.k].length, false, enc(e.n));
            }
        }
        // Path tables (L at 18, M after it)
        const pt = (o, big) => {
            dirs.forEach((d, i) => {
                const nm = d.name ? enc(d.name.toUpperCase()) : new Uint8Array([0]);
                img[o] = nm.length; img[o + 1] = 0;
                const v = d.lba + lba, p = d.parent + 1;
                if (big) { img[o + 2] = v >>> 24; img[o + 3] = (v >>> 16) & 255; img[o + 4] = (v >>> 8) & 255; img[o + 5] = v & 255; img[o + 6] = p >> 8; img[o + 7] = p & 255; }
                else { img[o + 2] = v & 255; img[o + 3] = (v >>> 8) & 255; img[o + 4] = (v >>> 16) & 255; img[o + 5] = v >>> 24; img[o + 6] = p & 255; img[o + 7] = p >> 8; }
                img.set(nm, o + 8); o += 8 + nm.length + (nm.length & 1);
            });
        };
        pt(18 * S, false); pt((18 + ptSectors) * S, true);
        // Files
        for (const f of files) img.set(f.data, f.lba * S);
        // Primary volume descriptor + terminator
        const v = 16 * S;
        img[v] = 1; str(v + 1, 'CD001', 5); img[v + 6] = 1;
        str(v + 8, '', 32); str(v + 40, volume, 32);
        b733(v + 80, total); b723(v + 120, 1); b723(v + 124, 1); b723(v + 128, S);
        b733(v + 132, ptLen);
        const l = 18 + lba, m = 18 + ptSectors + lba;
        img[v + 140] = l & 255; img[v + 141] = (l >>> 8) & 255; img[v + 142] = (l >>> 16) & 255; img[v + 143] = l >>> 24;
        img[v + 148] = m >>> 24; img[v + 149] = (m >>> 16) & 255; img[v + 150] = (m >>> 8) & 255; img[v + 151] = m & 255;
        record(v + 156, dirs[0].lba, dirs[0].sectors * S, true, new Uint8Array([0]));
        str(v + 190, '', 128); str(v + 318, '', 128); str(v + 446, '', 128); str(v + 574, 'EASYCOMPILE', 128);
        str(v + 702, '', 37); str(v + 739, '', 37); str(v + 776, '', 37);
        const ds = now.toISOString().replace(/[-:T]/g, '').slice(0, 14) + '00';
        str(v + 813, ds, 16); img[v + 829] = 0; str(v + 830, ds, 16); img[v + 846] = 0;
        str(v + 847, '0000000000000000', 16); str(v + 864, '0000000000000000', 16);
        img[v + 881] = 1;
        img[17 * S] = 255; str(17 * S + 1, 'CD001', 5); img[17 * S + 6] = 1;
        return img;
    }

    // --- CD-ROM mode 2 form 1 EDC / ECC -------------------------------------
    const eccF = new Uint8Array(256), eccB = new Uint8Array(256), edcT = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
        const j = ((i << 1) ^ (i & 0x80 ? 0x11D : 0)) & 0xFF;
        eccF[i] = j; eccB[i ^ j] = i;
        let e = i; for (let k = 0; k < 8; k++) e = (e >>> 1) ^ (e & 1 ? 0xD8018001 : 0);
        edcT[i] = e >>> 0;
    }
    function eccPQ(s, majorCount, minorCount, majorMult, minorInc, dest) {
        const size = majorCount * minorCount;
        for (let major = 0; major < majorCount; major++) {
            let index = (major >> 1) * majorMult + (major & 1), a = 0, b = 0;
            for (let minor = 0; minor < minorCount; minor++) {
                const t = s[12 + index];
                index += minorInc; if (index >= size) index -= size;
                a ^= t; b ^= t; a = eccF[a];
            }
            a = eccB[eccF[a] ^ b];
            s[12 + dest + major] = a; s[12 + dest + major + majorCount] = a ^ b;
        }
    }

    // --- CDI (audio/data, as cdi4dc) ------------------------------------------
    // tpl: prefix (audio track + gaps), gapTail (2 sectors), header (691 bytes)
    function buildCdi(iso, tpl) {
        const secs = iso.length / 2048, blocks = Math.max(secs + 2, 302);
        const dataLen = blocks * 2336;
        const out = new Uint8Array(tpl.prefix.length + dataLen + tpl.header.length);
        out.set(tpl.prefix);
        const raw = new Uint8Array(2352);
        let o = tpl.prefix.length;
        for (let s = 0; s < secs; s++) {
            raw.fill(0);
            raw.set(iso.subarray(s * 2048, s * 2048 + 2048), 24);
            let e = 0;
            for (let i = 16; i < 2072; i++) e = (e >>> 8) ^ edcT[(e ^ raw[i]) & 255];
            raw[2072] = e & 255; raw[2073] = (e >>> 8) & 255; raw[2074] = (e >>> 16) & 255; raw[2075] = e >>> 24;
            eccPQ(raw, 86, 24, 2, 86, 2076 - 12);
            eccPQ(raw, 52, 43, 86, 88, 2248 - 12);
            out.set(raw.subarray(16, 2352), o); o += 2336;
        }
        // (short images are padded with empty sectors up to 300)
        o = tpl.prefix.length + (blocks - 2) * 2336;
        out.set(tpl.gapTail, o); o += tpl.gapTail.length;
        const h = tpl.header.slice();
        const put = (at, v) => { h[at] = v & 255; h[at + 1] = (v >>> 8) & 255; h[at + 2] = (v >>> 16) & 255; h[at + 3] = (v >>> 24) & 255; };
        put(349, blocks); put(379, blocks + 150); put(408, blocks + 150); put(608, 11702 + blocks + 150);
        out.set(h, o);
        return out;
    }

    return { ROMS, crc32, unzip, collectRoms, romStatus, adpcmWav, readPcmWav, toInt16, buildIso, buildCdi };
})();
if (typeof module !== 'undefined') module.exports = EasyCore;
