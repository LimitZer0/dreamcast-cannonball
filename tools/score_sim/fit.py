import re,collections,math,statistics as st
d=collections.defaultdict(list)
for line in open(__import__('sys').argv[1] if len(__import__('sys').argv) > 1 else 'results_unscaled.txt'):
    m=re.match(r'J=(\d) T=(\d) R=(-?\d) G=(\d) RESULT score=(\d+)',line)
    if m: d[tuple(int(x) for x in m.groups()[:4])].append(int(m.group(5)))
mean={k:st.mean(v) for k,v in d.items()}
def fit(J,G,traffics):
    # log mean = a + t[i] + r[j]; least squares by alternating averages
    t=[0]*4; r={j:0 for j in traffics}; a=0
    for _ in range(200):
        a=st.mean(math.log(mean[(J,i,j,G)])-t[i]-r[j] for i in range(4) for j in traffics)
        for i in range(4): t[i]=st.mean(math.log(mean[(J,i,j,G)])-a-r[j] for j in traffics)
        for j in traffics: r[j]=st.mean(math.log(mean[(J,i,j,G)])-a-t[i] for i in range(4))
    # multipliers relative to normal (index 1)
    tm=[math.exp(t[1]-x) for x in t]; rm={j:math.exp(r[1]-r[j]) for j in traffics}
    resid=max(abs(math.log(mean[(J,i,j,G)])-a-t[i]-r[j]) for i in range(4) for j in traffics)
    return tm,rm,resid
for J,G,name in [(0,0,'US'),(1,0,'JP'),(0,1,'US grippy')]:
    tm,rm,res=fit(J,G,[0,1,2,3])
    off=mean[(J,1,1,G)]/mean[(J,1,-1,G)]
    offT=[mean[(J,i,1,G)]/mean[(J,i,-1,G)] for i in range(4)]
    print(f"{name:10} time x: " + ' '.join(f'{x:.2f}' for x in tm) + " | traffic x: " + ' '.join(f'{rm[j]:.2f}' for j in [0,1,2,3]) + f" | traffic-off x (per time setting): " + ' '.join(f'{x:.2f}' for x in offT) + f" | max model err {100*(math.exp(res)-1):.1f}%")
