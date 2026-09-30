import os
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import sprview; sprview.D=os.environ.get('SPRS','sprs')+'/'
import sprcmp; sprcmp.D=sprview.D
from sprcmp import *
S=3
cars=[("FERRARI",25,['016001e4']),
("PORSCHE",38,['0130157a','01101176','00b0170b','009012da','00701777','00401c34','003017a0']),
("BEETLE",30,['00f0564f','00d05533','00905741','00605600','00405782']),
("SEDAN",9,['00f052ba','00d051c0','00905390','00705514','00505528','002053dd']),
("CABRIO",11,['00f034be','009035a2','006038a3','004038bb','002035f7']),
("PICKUP",35,['00f02001','00d01d6b','00902108','00502152']),
("TRUCK",40,['00f03a89','00d038ca','00903c07','00703a11','00403ebd'])]
sd=[]
for _,p,ks in cars:
    L=[obj(k,p,False) for k in ks]
    bw,bh=L[0][0],L[0][1]
    # keep only sizes whose full drawing was seen (far ones are often cut off at the horizon)
    sd.append([s for s in L if s[1] >= 0.62*bh*s[0]/bw])
lv={}; bg=(118,118,118); MAXF=0.75
slot=[int(s[0][0]*MAXF)+8 for s in sd]
W=sum(slot)+60; PH=int(max(s[0][1] for s in sd)*MAXF)+16
frames=[]; N=100
for i in range(N):
    fr=0.08*(MAXF/0.08)**(i/(N-1))
    cv=Image.new('RGB',(W,PH*2+4),bg); d=ImageDraw.Draw(cv)
    for side in (0,1):
        x=58; y0=side*(PH+4)
        d.text((3,y0+PH//2-4),"ARCADE" if side==0 else "FROM BIG",fill=(235,235,235))
        for ci,srcs in enumerate(sd):
            fw,fh=srcs[0][0],srcs[0][1]
            ow=max(2,round(fw*fr)); oh=max(2,round(fh*fr))
            si=0
            for j,s in enumerate(srcs):
                if s[0]>=ow: si=j
            if side==1: si=0     # smooth: always shrink the full-size drawing
            w,h,px,P,sh=srcs[si]
            oh=max(2,round(h*ow/w))     # the drawing's own proportions
            if side==0: im=to_rgba(arcade(px,w,h,ow,oh),ow,oh,P,sh)
            else:
                k=(ci,si)
                if k not in lv: lv[k]=levels_for(px,w,h,P)
                im=to_rgba(smooth(px,w,h,ow,oh,P,lv[k]),ow,oh,P,sh)
            cv.paste(im,(x+(slot[ci]-ow)//2,y0+PH-3-oh),im)
            x+=slot[ci]
    d.line((0,PH+1,W,PH+1),fill=(90,90,90),width=2)
    frames.append(cv.resize((cv.width*S,cv.height*S),Image.NEAREST))
frames[0].save('all_cars_from_big.gif',save_all=True,append_images=frames[1:]+[frames[-1]]*20,duration=60,loop=0)
row=[]
for (n,p,ks),srcs in zip(cars,sd):
    ims=[to_rgba(px,w,h,P,sh) for w,h,px,P,sh in srcs]
    Wr=sum(im.width+6 for im in ims)+70; Hr=max(im.height for im in ims)+14
    r=Image.new('RGB',(Wr,Hr),bg); dd=ImageDraw.Draw(r); dd.text((2,Hr//2-5),n,fill=(235,235,235)); x=70
    for im in ims: r.paste(im,(x,Hr-4-im.height),im); x+=im.width+6
    row.append(r)
Wt=max(r.width for r in row); Ht=sum(r.height for r in row)
T=Image.new('RGB',(Wt,Ht),bg); y=0
for r in row: T.paste(r,(0,y)); y+=r.height
pass

fs=[frames[i] for i in (40,55,70)]
w,h=fs[0].size; o=Image.new('RGB',(w,h*3))
for i,f in enumerate(fs): o.paste(f,(0,i*h))
o.save('fb_check.png')
