#!/usr/bin/env python3
"""The cache lines one audio callback touches (tools/cachetrace.sh makes the trace): cachetrace.py nm.txt trace.txt

The trace holds, from the first traced callback on, "M" at each callback start, "I addr" per instruction fetch and
"L|S addr bytes" per data access. Printed, for the steady callbacks (the last one alone, the others agree):
  - code: the 32-byte lines fetched, the bytes they span, the lines of the per-sample loops (fetched at least once per
    sample) and the lines per function;
  - data: the lines read or written, per symbol (the stack apart: on the OP-1 it is not the plugin's);
  - the BF524 caches fed the plugin alone, callback after callback: the I-cache (16 KB, 4-way, 32-byte lines, LRU)
    and the D-cache (16 KB, 2-way, 32-byte lines), its misses per callback once warm, for every placement of the
    image within a line (heap blocks fall anywhere) and, as the worst set, the most lines one set must hold.
The OP-1 runs firmware and the UI between callbacks, so what the plugin keeps warm depends on them; fewer lines and
fewer per set is what this tool can measure."""
import sys
from collections import Counter, OrderedDict, defaultdict

LINE = 32

def symbols(path):
    out = []
    for l in open(path):
        f = l.split()
        if len(f) == 4: out.append((int(f[0], 16), int(f[1], 16), f[2], f[3]))
        elif len(f) == 3: out.append((int(f[0], 16), 0, f[1], f[2]))
    return sorted(out)

def owner(syms, a):
    best = None
    for s in syms:
        if s[0] > a: break
        best = s
    return best[3].lstrip('_') if best else '?'

def callbacks(path):
    cbs, cur = [], None
    for l in open(path):
        f = l.split()
        try:                                                 # (the program's own output can cut into a line)
            if f[0] == 'M': cur = {'I': [], 'D': []}; cbs.append(cur)
            elif f[0] == 'I': cur['I'].append(int(f[1], 16))
            elif f[0] in ('L', 'S'): cur['D'].append((int(f[1], 16), int(f[2])))
        except (ValueError, IndexError): pass
    return cbs[:-1]                                          # the last mark ends the traced callbacks

class Cache:
    def __init__(self, size, ways):
        self.sets, self.ways = size // LINE // ways, ways
        self.s = [OrderedDict() for _ in range(self.sets)]
    def touch(self, line):
        s = self.s[line % self.sets]
        if line in s: s.move_to_end(line); return 0
        s[line] = 1
        if len(s) > self.ways: s.popitem(last=False)
        return 1

def run_cache(streams, size, ways, off):
    c, miss = Cache(size, ways), []
    for st in streams: miss.append(sum(c.touch((a + off) // LINE) for a in st))
    return miss

def worst_set(lines, size, ways):
    sets = size // LINE // ways
    return max(Counter(l % sets for l in lines).values())

def main():
    syms, cbs = symbols(sys.argv[1]), callbacks(sys.argv[2])
    text = [s for s in syms if s[2] in 'tT']
    stack = next((s for s in syms if s[3] == '_bench_stack'), None)
    in_stack = (lambda a: stack[0] <= a < stack[0] + stack[1]) if stack and stack[1] else (lambda a: False)
    last = cbs[-1]
    print(f"{len(cbs)} callbacks traced, 3 voices each; the last one:")
    fetch = Counter(a // LINE for a in last['I'])
    ilines = sorted(fetch)
    print(f"  code: {len(last['I'])} fetches, {len(ilines)} lines ({len(ilines) * LINE} B) "
          f"over {(ilines[-1] - ilines[0] + 1) * LINE} B")
    hot = [l for l in ilines if fetch[l] >= 3 * 128]
    print(f"  code in the sample loops (fetched >= once per sample per voice): {len(hot)} lines")
    per = defaultdict(set)
    for l in ilines: per[owner(text, l * LINE)].add(l)
    for f, ls in sorted(per.items(), key=lambda kv: -len(kv[1])):
        h = sum(1 for l in ls if fetch[l] >= 3 * 128)
        print(f"    {len(ls):4d} lines ({h:3d} in loops)  {f}")
    dl = defaultdict(set)
    for a, n in last['D']:
        for l in range(a // LINE, (a + n - 1) // LINE + 1):
            dl['(stack)' if in_stack(a) else owner(syms, a)].add(l)
    nd = sum(len(v) for k, v in dl.items() if k != '(stack)')
    print(f"  data: {len(last['D'])} accesses, {nd} lines outside the stack, "
          f"{len(dl.get('(stack)', ()))} stack lines")
    for k, ls in sorted(dl.items(), key=lambda kv: -len(kv[1])):
        print(f"    {len(ls):4d} lines  {k}")
    istreams = [cb['I'] for cb in cbs]
    dstreams = [[a for a, n in cb['D'] if not in_stack(a)] for cb in cbs]
    for name, st, size, ways in (('I-cache 16 KB 4-way', istreams, 16384, 4), ('D-cache 16 KB 2-way', dstreams, 16384, 2)):
        res = []
        for off in range(0, LINE, 4):
            m = run_cache(st, size, ways, off)
            ls = {(a + off) // LINE for a in st[-1]}
            res.append((m[-1], worst_set(ls, size, ways), len(ls)))
        miss = [r[0] for r in res]; ws = [r[1] for r in res]; nl = [r[2] for r in res]
        print(f"  {name}: warm misses per callback {min(miss)}..{max(miss)}, "
              f"lines {min(nl)}..{max(nl)}, most lines in one set {min(ws)}..{max(ws)} (of {ways} ways), "
              f"over the 8 placements in a line")

if __name__ == '__main__':
    main()
