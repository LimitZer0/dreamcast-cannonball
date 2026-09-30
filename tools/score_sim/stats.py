import sys,re,collections,statistics as st
d=collections.defaultdict(list)
for line in open(sys.argv[1]):
    m=re.match(r'J=(\d) T=(\d) R=(-?\d) G=(\d) RESULT score=(\d+) stage=(\d) completed=(\d)',line)
    if not m: continue
    k=tuple(int(x) for x in m.groups()[:4]); d[k].append((int(m.group(5)),int(m.group(6)),int(m.group(7))))
TN=['Easy','Normal','Hard','Hardest']; RN={-1:'Off',0:'Easy',1:'Normal',2:'Hard',3:'Hardest'}
print(f"{'tracks':6} {'time':8} {'traffic':8} {'grip':4} {'n':>4} {'mean':>9} {'median':>9} {'p90':>9} {'avgstg':>6} {'done%':>6}")
for k in sorted(d):
    v=d[k]; s=sorted(x[0] for x in v)
    print(f"{'JP' if k[0] else 'US':6} {TN[k[1]]:8} {RN[k[2]]:8} {k[3]:4} {len(v):4} {st.mean(s):9.0f} {st.median(s):9.0f} {s[int(len(s)*.9)]:9} {st.mean(x[1] for x in v):6.2f} {100*st.mean(x[2] for x in v):6.1f}")
