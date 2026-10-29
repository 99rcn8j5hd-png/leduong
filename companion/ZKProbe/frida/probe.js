// probe.js — Frida: do GNames (FNamePool) + GObjects, khong can build dylib
// Dung: frida -U -f vn.vng.pubgmobile -l probe.js   (hoac attach vao game dang chay)
// Sau khi vao lobby: go  zkProbe()  trong console Frida.
//
// Luu y: chi de CHAN DOAN. Dung Frida lau de choi = signature qua ro, de bi quet.

'use strict';

const KEYWORDS = ['None', 'PlayerCameraManager', 'World', 'GameEngine',
                  'PlayerController', 'BP_PlayerCharacter_C', 'PersistentLevel',
                  'LocalPlayer', 'GameInstance', 'PlayerState'];

let scanBytes=0, scanDeadline=0;
function* chunkRanges(ranges) {
    for(const range of ranges) {
        for(let off=0;off<range.size;off+=256*1024) {
            const size=Math.min(256*1024,range.size-off);
            if(Date.now()>scanDeadline || scanBytes+size>2*1024*1024*1024)
                throw new Error('Scan budget exhausted; result INCOMPLETE');
            scanBytes+=size;
            yield {base:range.base.add(off),size};
        }
    }
}

function mainModule() {
    const mods = Process.enumerateModules();
    for (const m of mods) {
        if (m.name.indexOf('ShadowTrackerExtra') >= 0 || m.name.indexOf('PUBG') >= 0) return m;
    }
    return mods[0];
}

function hexOfBytes(u8) {
    let s = '';
    for (let i = 0; i < u8.length; i++) s += (i ? ' ' : '') + ('0' + u8[i].toString(16)).slice(-2);
    return s;
}

function ptrBytes(p) {
    const hex=p.toString().replace(/^0x/,'').padStart(16,'0');
    const out = [];
    for (let i = 0; i < 8; i++) {
        out.push(parseInt(hex.slice(14-2*i,16-2*i),16));
    }
    return hexOfBytes(out);
}

// ---- parse entry trong block 64KB ----
function parseEntry(u8, off) {
    if (!Number.isInteger(off) || off < 0 || off > u8.length || u8.length-off < 2) return null;
    const hdr = u8[off] | (u8[off + 1] << 8);
    const len = hdr >> 1;
    const wide = (hdr & 1) !== 0;
    if (len === 0 || len > 512) return null;
    const bytes = len * (wide ? 2 : 1);
    if (off + 2 + bytes > u8.length) return null;
    let s = '';
    if (!wide) {
        for (let i = 0; i < len; i++) {
            const c = u8[off + 2 + i];
            if (c < 0x20 || c > 0x7e) return null;
            s += String.fromCharCode(c);
        }
    } else {
        for (let i = 0; i < len; i++) {
            const w = u8[off + 2 + i * 2] | (u8[off + 3 + i * 2] << 8);
            if (w < 0x20 || w > 0x7e) return null;
            s += String.fromCharCode(w);
        }
    }
    return { name: s, next: off + ((2 + bytes + 1) & ~1) };
}

function walkBlock(u8, limit) {
    const out = [];
    let off = 0;
    while (off + 2 <= u8.length) {
        const e = parseEntry(u8, off);
        if (!e) break;
        out.push({ off: off, name: e.name });
        off = e.next;
        if (out.length >= (limit || 100000)) break;
    }
    return out;
}

function readBlock(u8ptr) {
    try {
        const ab = u8ptr.readByteArray(0x10000);
        return new Uint8Array(ab);
    } catch (e) {
        return null;
    }
}

// ---- 1. tim block0 (signature "None") ----
function findBlockCandidates() {
    const cands = [];
    const ranges = Process.enumerateRanges('rw-');
    for (const r of chunkRanges(ranges)) {
        let res = [];
        try {
            res = Memory.scanSync(r.base, r.size, '08 00 4E 6F 6E 65');
        } catch (e) {
            continue;
        }
        for (const m of res) {
            const u8 = readBlock(m.address);
            if (!u8) continue;
            const names = walkBlock(u8, 40);
            if (names.length < 40) continue;
            if (names[0].name !== 'None') continue;
            cands.push({ addr: m.address, names: names });
            if (cands.length > 64) return cands;
        }
    }
    return cands;
}

// ---- 2. tim mang Blocks[] tro ve block0 ----
function findBlocksArray(cands, base) {
    const pats = cands.map(c => ptrBytes(c.addr));
    let best = null;

    // quet trong data cua main module truoc
    const own = Process.enumerateRanges('rw-').filter(r => {
        try { return r.base.compare(base) >= 0 && r.base.compare(base.add(0x10000000)) < 0; }
        catch (e) { return false; }
    });

    function search(ranges) {
        for (const r of chunkRanges(ranges)) {
            for (let k = 0; k < pats.length; k++) {
                let res = [];
                try {
                    res = Memory.scanSync(r.base, r.size, pats[k]);
                } catch (e) {
                    continue;
                }
                for (const m of res) {
                    const blocksAddr = m.address;
                    // poolScore: block0 phai la cua cand nay + walk ok
                    const blk0 = blocksAddr.readPointer();
                    if (blk0.isNull()) continue;
                    const u8 = readBlock(blk0);
                    if (!u8) continue;
                    const names = walkBlock(u8, 200);
                    if (names.length < 50 || names[0].name !== 'None') continue;

                    // dem tong so ten tren vai block
                    let total = 0;
                    for (let bi = 0; bi < 16; bi++) {
                        const p = blocksAddr.add(bi * 8).readPointer();
                        if (p.isNull()) break;
                        const u = readBlock(p);
                        if (!u) break;
                        const n = walkBlock(u, 100000).length;
                        if (n === 0) break;
                        total += n;
                    }
                    if (total < 500) continue;
                    if (!best || total > best.total) best = { blocksAddr: blocksAddr, total: total };
                }
            }
            if (best) return true;
        }
        return false;
    }

    if (search(own)) return best;
    search(Process.enumerateRanges('rw-'));
    return best;
}

// ---- 3. dump toan bo ten ----
function allNames(blocksAddr) {
    const out = [];
    for (let bi = 0; bi < 512; bi++) {
        const p = blocksAddr.add(bi * 8).readPointer();
        if (p.isNull()) break;
        const u8 = readBlock(p);
        if (!u8) break;
        const names = walkBlock(u8, 500000);
        if (!names.length) break;
        for (const n of names) {
            if(out.length>=100000) return out; // bounded diagnostic sample, not a full dump
            out.push({ id: (bi << 16) | n.off, name: n.name });
        }
    }
    return out;
}

function fnameOf(blocksAddr, id) {
    const bi = (id >>> 16) & 0xFFFF;
    const off = id & 0xFFFF;
    const p = blocksAddr.add(bi * 8).readPointer();
    if (p.isNull()) return '';
    const u8 = readBlock(p);
    if (!u8) return '';
    const e = parseEntry(u8, off);
    return e ? e.name : '';
}

function className(blocksAddr, obj) {
    try {
        const cls = obj.add(0x10).readPointer();
        if (cls.isNull()) return '';
        const id = cls.add(0x18).readS32();
        return fnameOf(blocksAddr, id);
    } catch (e) { return ''; }
}

function runProbe() {
    console.log('[model] EXPERIMENTAL: headerShift=1, byte-offset IDs. Not verified for this game.');
    const mod = mainModule();
    const base = mod.base;
    console.log('\n================ ZK PROBE (frida) ================');
    console.log('[env] main = ' + mod.name + ' base=' + base);

    const cands = findBlockCandidates();
    console.log('[names] block0 candidates = ' + cands.length);
    if (!cands.length) { console.log('[names] GNames NOT FOUND'); return; }

    const pool = findBlocksArray(cands, base);
    if (!pool) { console.log('[names] Blocks[] NOT FOUND'); return; }

    const blocks = pool.blocksAddr;
    console.log('[names] blocks  = ' + blocks);
    console.log('[names] rvaBlocks = 0x' + blocks.sub(base).toString(16));
    console.log('[names] total names ~ ' + pool.total);

    const names = allNames(blocks);
    console.log('[names] dumped ' + names.length);

    console.log('[names] -- keyword ids --');
    for (const k of KEYWORDS) {
        const hit = names.find(n => n.name === k);
        const pad = (k + '                        ').slice(0, 24);
        console.log('   ' + pad + (hit ? 'id=0x' + hit.id.toString(16) : 'NOT FOUND'));
    }

    // ---- GObjects ----
    const ranges = Process.enumerateRanges('rw-');
    let bestArr = null;
    const probes = [1, 2, 3, 10, 50, 200, 1000, 5000, 20000, 60000, 130000];

    for (const r of chunkRanges(ranges)) {
        let ab;
        try { ab = r.base.readByteArray(r.size); } catch (e) { continue; }
        const u8 = new Uint8Array(ab);
        const dv = new DataView(ab);

        for (let i = 0; i + 0x20 <= u8.length; i += 8) {
            let objectsPtr;
            try { objectsPtr = r.base.add(i).readPointer(); } catch (e) { continue; }
            if (objectsPtr.isNull() || objectsPtr.compare(ptr('0x10000000')) < 0) continue;

            const maxEl = dv.getInt32(i + 8, true);
            const numEl = dv.getInt32(i + 12, true);
            const maxCh = dv.getInt32(i + 16, true);
            const numCh = dv.getInt32(i + 20, true);

            if (numEl < 1000 || numEl > maxEl || maxEl > 5000000) continue;
            if (maxCh <= 0 || maxCh > 4096) continue;
            if (numCh <= 0 || numCh > maxCh) continue;

            for (const itemSize of [0x10, 0x18]) {
                let ok = 0, tried = 0;
                for (const idx of probes) {
                    if (idx >= numEl) continue;
                    tried++;
                    try {
                        const chunk = objectsPtr.add((idx >>> 16) * 8).readPointer();
                        if (chunk.isNull()) continue;
                        const obj = chunk.add((idx & 0xFFFF) * itemSize).readPointer();
                        if (obj.isNull()) continue;
                        const cls = className(blocks, obj);
                        if (cls.length >= 2 && /^[A-Za-z][A-Za-z0-9_]*$/.test(cls)) ok++;
                    } catch (e) { }
                }
                if (!tried) continue;
                const score = Math.floor(ok * 100 / tried);
                if (score >= 80 && ok >= 4 && (!bestArr || score > bestArr.score)) {
                    bestArr = { addr: r.base.add(i), objectsPtr, numEl, maxEl, maxCh, numCh, itemSize, score };
                }
            }
        }
        if (bestArr) break;
    }

    if (!bestArr) {
        console.log('[objs] GObjects NOT FOUND');
    } else {
        console.log('[objs] array    = ' + bestArr.addr + '  rva=0x' + bestArr.addr.sub(base).toString(16));
        console.log('[objs] numElements = ' + bestArr.numEl + '  itemSize=0x' + bestArr.itemSize.toString(16));
        let shown = 0;
        for (let i = 1; i < bestArr.numEl && shown < 200; i++) {
            try {
                const chunk = bestArr.objectsPtr.add((i >>> 16) * 8).readPointer();
                if (chunk.isNull()) continue;
                const obj = chunk.add((i & 0xFFFF) * bestArr.itemSize).readPointer();
                if (obj.isNull()) continue;
                const cls = className(blocks, obj);
                if (!cls) continue;
                if (!KEYWORDS.some(k => cls.indexOf(k) >= 0)) continue;
                const nmId = obj.add(0x18).readS32();
                console.log('   [' + i + '] ' + cls + ' :: ' + fnameOf(blocks, nmId) + ' @ ' + obj);
                shown++;
            } catch (e) { }
        }
    }

    console.log('\n===== UNVERIFIED CANDIDATES =====');
    console.log('GNAMES_BLOCKS_RVA = 0x' + blocks.sub(base).toString(16));
    if (bestArr) console.log('GOBJECTS_RVA      = 0x' + bestArr.addr.sub(base).toString(16));
    console.log('==============================\n');
}

let probeRunning=false;
function zkProbe() {
    if(probeRunning) { console.log('Probe already running'); return; }
    probeRunning=true;
    scanBytes=0; scanDeadline=Date.now()+90000;
    try { return runProbe(); } catch(e) { console.log('[error] '+e); } finally { probeRunning=false; }
}
if(typeof module!=='undefined' && module.exports) {
    module.exports={parseEntry,walkBlock,ptrBytes};
} else {
    console.log('ZKProbe loaded; invoke zkProbe() explicitly when ready. No automatic scan.');
}
