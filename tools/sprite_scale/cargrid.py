import os
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sprcmp import *
S=4
cars=[("PORSCHE",[('0130157a',52,False),('00b0170b',51,False),('008012d7',51,False)]),
("BEETLE",[('00f0564f',15,False),('00b01ba9',14,False)]),
("BMW",[('00d051c0',34,False),('009054d9',34,False)]),
("CABRIO",[('00f034be',54,False),('00903866',53,False)]),
("PICKUP",[('011022f8',24,False),('00902424',24,False)]),
("SEDAN",[('00f052ba',35,False)]),
("FERRARI",[('01700721',40,False)])]
fracs=[0.6,0.45,0.35,0.25,0.18,0.12]
bg=(118,118,118)
blocks=[]
lvcache={}
for name,srcs in cars:
    sd=[obj(k,p,f) for k,p,f in srcs]
    fw,fh=sd[0][0],sd[0][1]
    cells=[]
    for fr in fracs:
        ow=max(2,round(88*fr*fw/88)); oh=max(2,round(fh*ow/fw))
        si=0
        for i,s in enumerate(sd):
            if s[0]>=ow and s[1]>=oh: si=i
        src=sd[si]
        w,h,px,P,sh=src
        a=to_rgba(arcade(px,w,h,ow,oh),ow,oh,P,sh)
        k=srcs[si][0]
        if k not in lvcache: lvcache[k]=levels_for(px,w,h,P)
        b=to_rgba(smooth(px,w,h,ow,oh,P,lvcache[k]),ow,oh,P,sh)
        cells.append((a,b))
    pad=6*S
    Hb=max(c[0].height for c in cells)*S
    Wb=sum(c[0].width*S+pad for c in cells)+140
    img=Image.new('RGB',(Wb,Hb*2+pad*2+24),bg); d=ImageDraw.Draw(img)
    d.text((6,4),name,fill=(255,255,255))
    d.text((6,24+Hb//2),"ARCADE",fill=(230,230,230)); d.text((6,24+Hb+pad+Hb//2),"SMOOTH",fill=(230,230,230))
    x=140
    for a,b in cells:
        A=a.resize((a.width*S,a.height*S),Image.NEAREST); B=b.resize((b.width*S,b.height*S),Image.NEAREST)
        img.paste(A,(x,24+Hb-A.height),A); img.paste(B,(x,24+2*Hb+pad-B.height),B); x+=A.width+pad
    blocks.append(img)
W=max(b.width for b in blocks); H=sum(b.height for b in blocks)
out=Image.new('RGB',(W,H),bg); y=0
for b in blocks: out.paste(b,(0,y)); y+=b.height
out.save('cars_scaling_compare.png'); print(out.size)
