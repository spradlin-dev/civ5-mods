import json, sys
truth = {(t["x"], t["y"]): t for t in json.load(open(sys.argv[1]))}
read = {(r["x"], r["y"]): r for r in json.load(open(sys.argv[2]))}
fields = ["unexplored", "fogged", "terrain", "hills", "feature", "resource", "improvement", "owner", "city", "road", "river", "units"]
def norm(v):
    if isinstance(v, list): return sorted(s.strip().lower() for s in v)
    if isinstance(v, str): return v.strip().lower()
    return v
def terrain_of(t):
    return "mountain" if t["mountain"] else t["terrain"]
total = {f: 0 for f in fields}; right = {f: 0 for f in fields}
missing = [k for k in truth if k not in read]
extra = [k for k in read if k not in truth]
wrong = []
for k, t in truth.items():
    if k not in read: continue
    r = read[k]
    for f in fields:
        tv = terrain_of(t) if f == "terrain" else t[f]
        rv = r.get(f, "" if isinstance(tv, str) else ([] if isinstance(tv, list) else False))
        if t["unexplored"] and f not in ("unexplored",): continue
        total[f] += 1
        ok = norm(tv) == norm(rv)
        if f == "units" and not ok:
            # count as right if relation+count match ignoring unit names
            ok = sorted(s.split()[0] for s in t[f]) == sorted(s.split()[0] for s in rv if s.strip())
        if f == "resource" and not ok and norm(tv) and norm(rv) and norm(rv).startswith(norm(tv)[:4]): ok = True
        if f == "improvement" and not ok and norm(tv) and norm(rv) and norm(rv)[:4] == norm(tv)[:4]: ok = True
        if ok: right[f] += 1
        else: wrong.append((k, f, tv, rv))
print("hexes in truth %d, transcribed %d, missing %d, extra %d" % (len(truth), len(read), len(missing), len(extra)))
for f in fields:
    print("%-12s %3d/%3d  %3d%%" % (f, right[f], total[f], 100 * right[f] // max(1, total[f])))
allr = sum(right.values()); allt = sum(total.values())
print("overall %d/%d = %d%%" % (allr, allt, 100 * allr // max(1, allt)))
print("\nmistakes (hex, field, truth, read):")
for w in wrong[:80]: print("  ", w)
if missing: print("missing hexes:", sorted(missing))
