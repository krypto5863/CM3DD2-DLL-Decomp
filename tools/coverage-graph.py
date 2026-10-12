#!/usr/bin/env python3
"""Offline call-graph classification of the ORIGINAL cm3d2.dll (verified copy) for the 1:1 coverage ledger.

Usage: coverage-graph.py <orig.dll> <procs.json> <exports.csv> <out.csv>
procs.json: build-review/coverage/original-procedures-with-repo-reference.json (address list from REA list_procedures).
Edges: direct E8/E9 rel32 to a known procedure start inside the owning function's byte range (approximate: byte scan, not a
disassembly, so rare false edges are possible), plus vtable edges (a RIP-relative LEA to data whose consecutive qwords are
procedure starts). Indirect calls through registers are NOT resolved. Output is a hint, not proof.
"""
import csv, json, struct, sys
from collections import defaultdict, deque

dll, procs_json, exports_csv, out_csv = sys.argv[1:5]
data = open(dll, 'rb').read()
e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, e_lfanew + 6)[0]
optsz = struct.unpack_from('<H', data, e_lfanew + 20)[0]
image_base = struct.unpack_from('<Q', data, e_lfanew + 24 + 24)[0]
sec_off = e_lfanew + 24 + optsz
secs = []
for i in range(nsec):
    name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', data, sec_off + 40 * i)[:5]
    secs.append((name.rstrip(b'\0').decode(), va, max(vsize, rsize), raw, rsize))

def rva_to_off(rva):
    for n, va, sz, raw, rsz in secs:
        if va <= rva < va + sz and rva - va < rsz:
            return raw + rva - va
    return None

def read(addr, n):
    off = rva_to_off(addr - image_base)
    return None if off is None else data[off:off + n]

procs = json.load(open(procs_json))
starts = sorted(int(p['a'], 16) for p in procs)
startset = set(starts)
info = {int(p['a'], 16): p for p in procs}
text = [s for s in secs if s[0] == '.text'][0]
text_end = image_base + text[1] + text[2]
ends = {}
for i, a in enumerate(starts):
    ends[a] = starts[i + 1] if i + 1 < len(starts) else text_end

def qwords_at(addr, maxn=64):
    out = []
    for k in range(maxn):
        b = read(addr + 8 * k, 8)
        if b is None or len(b) < 8:
            break
        v = struct.unpack('<Q', b)[0]
        if v in startset:
            out.append(v)
        else:
            break
    return out

edges = defaultdict(set)
for a in starts:
    if not (image_base + text[1] <= a < text_end):
        continue
    body = read(a, ends[a] - a) or b''
    n = len(body)
    for i in range(n - 4):
        op = body[i]
        if op in (0xE8, 0xE9):
            rel = struct.unpack_from('<i', body, i + 1)[0]
            t = a + i + 5 + rel
            if t in startset and t != a:
                edges[a].add(t)
        elif op in (0x48, 0x4C) and i + 6 < n and body[i + 1] == 0x8D and (body[i + 2] & 0xC7) == 0x05:
            disp = struct.unpack_from('<i', body, i + 3)[0]
            d = a + i + 7 + disp
            for t in qwords_at(d):
                if t != a:
                    edges[a].add(t)

rows = list(csv.reader(open(exports_csv, encoding='utf-8-sig')))[1:]
own = {"DLL_AnmParse", "DLL_FileMemory", "DLL_FileSystemDelegate", "DDLL_FileSystemDelegate", "DLL_CSV", "DLL_EnumData",
       "DLL_FileSystem", "DLL_Win32File", "DLL_File", "DLL_HoleDevice", "DLL_MenuDataBase", "DLL_MenuParse"}
imported = {"DLL_KAG", "DLL_TJS", "DLL_TJSVariant", "DLL_OGG"}
group_roots = defaultdict(set)
for addr, name, group, _ in rows:
    group_roots[group].add(int(addr, 16))

reach = {}
for g, roots in group_roots.items():
    seen = set(r for r in roots if r in startset)
    dq = deque(seen)
    while dq:
        x = dq.popleft()
        for y in edges.get(x, ()):
            if y not in seen:
                seen.add(y)
                dq.append(y)
    reach[g] = seen

groups_of = defaultdict(set)
for g, s in reach.items():
    for a in s:
        groups_of[a].add(g)

def classify(a):
    gs = groups_of.get(a, set())
    if not gs:
        return 'unreached'
    if gs <= imported:
        return 'imported-only'
    if gs & imported and gs & own:
        return 'shared-with-imported'
    if len(gs) >= 4:
        return 'shared-library'
    return 'own' if gs & own else 'imported-only'

with open(out_csv, 'w', newline='', encoding='utf-8') as f:
    w = csv.writer(f)
    w.writerow(['address', 'size_estimate', 'class', 'groups', 'repo_reference'])
    for a in starts:
        gs = sorted(groups_of.get(a, ()))
        w.writerow([hex(a), ends[a] - a, classify(a), ';'.join(gs), 'yes' if info[a]['covered'] else 'NO'])
summary = defaultdict(lambda: [0, 0])
for a in starts:
    c = classify(a)
    summary[c][0] += 1
    summary[c][1] += 0 if info[a]['covered'] else 1
for c, (n, miss) in sorted(summary.items()):
    print(f'{c:22s} procedures={n:5d} without-repo-reference={miss}')
