"""Summarise zh-netlog files (-netlog output): stalls, latency, jitter, run-ahead, retries, and stall<->retry correlation.
usage: python -I scripts/netlog-summary.py host.log peer.log   (one or two logs)"""
import re, sys, statistics as st

def parse(path):
    secs, stalls, ra = [], [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        kind, *rest = line.split()
        kv = {}
        for tok in rest:
            if "=" in tok:
                k, v = tok.split("=", 1)
                try: kv[k] = float(v)
                except ValueError: kv[k] = v
        if kind == "sec": secs.append(kv)
        elif kind == "stall": stalls.append(kv)
        elif kind == "runahead": ra.append((rest[1], kv))
    return secs, stalls, ra

logs = {p: parse(p) for p in sys.argv[1:]}
for p, (secs, stalls, ra) in logs.items():
    print("=" * 100); print(p)
    dur = secs[-1]["t"] / 1000 if secs else 0
    print(f"duration {dur/60:.1f} min, last frame {secs[-1]['frame']:.0f}, sec records {len(secs)}")
    d = [s["ms"] for s in stalls]
    if d:
        print(f"stalls >=100ms: {len(d)}  total {sum(d)/1000:.1f} s ({100*sum(d)/1000/dur:.1f}% of the game)  "
              f"median {st.median(d):.0f} ms  min {min(d):.0f}  max {max(d):.0f}  "
              f"between 1700-2000ms: {sum(1 for x in d if 1700 <= x <= 2000)}")
        gaps = [(stalls[i+1]["t"] - stalls[i]["t"]) / 1000 for i in range(len(stalls) - 1)]
        print(f"gap between stalls: median {st.median(gaps):.1f} s, min {min(gaps):.1f}, max {max(gaps):.1f}")
    sm = [s["stall_ms"] for s in secs]
    print(f"stall_ms per sec: total {sum(sm)/1000:.1f} s; seconds with any stall {sum(1 for x in sm if x > 0)}/{len(sm)}")
    lat = [s for s in secs if s["lat_n"] > 0]
    if lat:
        w = sum(s["lat_n"] for s in lat)
        mean = sum(s["lat_mean"] * s["lat_n"] for s in lat) / w
        print(f"ack round trip: samples {w:.0f}, weighted mean {mean:.0f} ms, max of maxes {max(s['lat_max'] for s in lat):.0f} ms, "
              f"jitter median {st.median([s['jitter'] for s in lat]):.0f} ms, p90 {sorted(s['jitter'] for s in lat)[int(0.9*len(lat))]:.0f} ms")
    print(f"peer max latency (router view) median {st.median([s['lat_max_peer'] for s in secs]):.0f} ms")
    ras = [s["ra"] for s in secs]
    print(f"run-ahead: values {sorted(set(int(x) for x in ras))}, median {st.median(ras):.0f} frames, changes applied {sum(1 for k,_ in ra if k=='apply')}")
    print(f"retries total {sum(s['retries'] for s in secs):.0f}; packets out {sum(s['out_pps'] for s in secs):.0f} in {sum(s['in_pps'] for s in secs):.0f}; "
          f"out pps median {st.median([s['out_pps'] for s in secs]):.0f}")
    cush = [s["cushion"] for s in secs if s["cushion"] < 1e6]
    print(f"cushion median {st.median(cush):.1f} min {min(cush):.1f}")

# Correlate: for each stall on A, retries on B in the seconds whose frames bracket the stall (frame-aligned, clocks differ)
paths = list(logs)
if len(paths) == 2:
    for a, b in [(paths[0], paths[1]), (paths[1], paths[0])]:
        secsB = logs[b][0]
        hits = 0; total = 0
        for s in logs[a][1]:
            if s["ms"] < 1000: continue
            total += 1
            f = s["frame"]
            # the stalled side waits for a command the other side should have sent around frame f; B's seconds covering f-90..f+90
            window = [x for x in secsB if f - 90 <= x["frame"] <= f + 90]
            if any(x["retries"] > 0 for x in window): hits += 1
        print("=" * 100)
        print(f"long stalls (>=1s) in {a.split(chr(92))[-1]}: {total}; with a retry on the OTHER machine within +-3 s of game time: {hits}")
