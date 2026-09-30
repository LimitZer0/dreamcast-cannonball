import os
import sys,json,struct; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sprcmp import arcade, reduce2, levels_for, smooth, to_rgba
from PIL import Image, ImageDraw
T=os.environ.get('SIMRUN','.')+'/'
rows=json.load(open(T+'tt/table.json'))
kpal={0:'25',1:'23',2:'14',3:'37',4:'34',5:'38'}
def pal(p):
    b=open(T+'dense/sprs/pal_%s.pal'%p,'rb').read(); return [tuple(b[i*3:i*3+3]) for i in range(16)]
def raw(key,h):
    b=open(T+'tt/%08x_%d.raw'%(key,h),'rb').read(); w,hh=struct.unpack('<HH',b[:4]); return w,hh,list(b[4:4+w*hh])
cars=[]
for kind in range(6):
    P=pal(kpal[kind]); sizes=[]
    for k in range(5):
        r=[x for x in rows if x[1]==kind and x[3]==1 and x[4]==16 and x[5]==k][0]
        w,h,px=raw(r[6],r[7])
        if h>0 and w>0: sizes.append((w,h,px,P,True))
    cars.append(sizes)
# ROM sizes sheet
bg=(118,118,118)
blocks=[]
for i,s in enumerate(cars):
    ims=[to_rgba(px,w,h,P,sh) for w,h,px,P,sh in s]
    Wb=sum(im.width+8 for im in ims)+60; Hb=max(im.height for im in ims)+8
    B=Image.new('RGB',(Wb,Hb),bg); d=ImageDraw.Draw(B); d.text((2,Hb//2-5),"CAR %d"%(i+1),fill=(235,235,235)); x=60
    for im in ims: B.paste(im,(x,Hb-4-im.height),im); x+=im.width+8
    blocks.append(B)
W=max(b.width for b in blocks); H=sum(b.height for b in blocks)
S=Image.new('RGB',(W,H),bg); y=0
for b in blocks: S.paste(b,(0,y)); y+=b.height
S.resize((W*3,H*3),Image.NEAREST).save('traffic_rom_sizes.png')
# animation
lv={}
MAXF=1.0
slot=[s[0][0]+10 for s in cars]
W=sum(slot)+64; PH=max(s[0][1] for s in cars)+12
frames=[]; N=110
for i in range(N):
    fr=0.06*(MAXF/0.06)**(i/(N-1))
    cv=Image.new('RGB',(W,PH*2+4),bg); d=ImageDraw.Draw(cv)
    for side in (0,1):
        x=62; y0=side*(PH+4)
        d.text((3,y0+PH//2-4),"ARCADE" if side==0 else "FROM BIG",fill=(235,235,235))
        for ci,sz in enumerate(cars):
            fw=sz[0][0]; ow=max(2,round(fw*fr))
            if side==0:
                si=0
                for j,s in enumerate(sz):
                    if s[0]>=ow: si=j
            else: si=0
            w,h,px,P,sh=sz[si]
            oh=max(2,round(h*ow/w))
            if side==0: im=to_rgba(arcade(px,w,h,ow,oh),ow,oh,P,sh)
            else:
                if ci not in lv: lv[ci]=levels_for(px,w,h,P)
                im=to_rgba(smooth(px,w,h,ow,oh,P,lv[ci]),ow,oh,P,sh)
            cv.paste(im,(x+(slot[ci]-ow)//2,y0+PH-3-oh),im)
            x+=slot[ci]
    d.line((0,PH+1,W,PH+1),fill=(90,90,90),width=2)
    frames.append(cv.resize((cv.width*3,cv.height*3),Image.NEAREST))
frames[0].save('traffic_cars_arcade_vs_from_big.gif',save_all=True,append_images=frames[1:]+[frames[-1]]*20,duration=60,loop=0)
fs=[frames[i] for i in (35,55,75)]
w,h=fs[0].size; o=Image.new('RGB',(w,h*3))
for i,f in enumerate(fs): o.paste(f,(0,i*h))
o.save('tc_check.png')
