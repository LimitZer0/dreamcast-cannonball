import os
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sprcmp import *
from PIL import ImageFont
S=4
objs=[ # name, [(key,pal,flip) biggest first]
 ("PALM",   [('01609b02',1,False),('00c0a23a',1,False)]),
 ("ROCK",   [('02c1c027',10,False),('0161cf73',10,False)]),
 ("SIGN",   [('01119c97',50,True)]),
 ("ICE CREAM",[('01a1958f',64,False)]),
 ("BOAT HOUSE",[('0212152e',55,True)]),
]
fracs=[0.5,0.35,0.25,0.18,0.12,0.08]
bg=(214,196,150)
cols=[]
for name,srcs in objs:
    srcdata=[obj(k,p,f) for k,p,f in srcs]
    full_w,full_h=srcdata[0][0],srcdata[0][1]
    cells=[]
    for fr in fracs:
        ow=max(2,round(full_w*fr)); oh=max(2,round(full_h*fr))
        # the game's own choice: smallest ROM copy at least as big as the output
        src=srcdata[0]
        for s in srcdata:
            if s[0]>=ow and s[1]>=oh: src=s
        w,h,px,P,sh=src
        a=to_rgba(arcade(px,w,h,ow,oh),ow,oh,P,sh)
        L=levels_for(px,w,h,P)
        b=to_rgba(smooth(px,w,h,ow,oh,P,L),ow,oh,P,sh)
        cells.append((a,b))
    cols.append((name,cells))
# layout: one block per object: row arcade / row smooth, columns sizes
pad=6*S
blocks=[]
for name,cells in cols:
    Wb=sum(c[0].width*S+pad for c in cells)+pad
    Hb=max(c[0].height for c in cells)*S
    img=Image.new('RGB',(Wb+140,Hb*2+pad*3+30),bg)
    d=ImageDraw.Draw(img)
    d.text((6,4),name,fill=(40,40,40))
    d.text((6,30+Hb//2),"ARCADE",fill=(40,40,40)); d.text((6,30+Hb+pad+Hb//2),"SMOOTH",fill=(40,40,40))
    x=140
    for a,b in cells:
        A=a.resize((a.width*S,a.height*S),Image.NEAREST); B=b.resize((b.width*S,b.height*S),Image.NEAREST)
        img.paste(A,(x,30+Hb-A.height),A); img.paste(B,(x,30+2*Hb+pad-B.height),B)
        x+=A.width+pad
    blocks.append(img)
W=max(b.width for b in blocks); H=sum(b.height for b in blocks)
out=Image.new('RGB',(W,H),bg); y=0
for b in blocks: out.paste(b,(0,y)); y+=b.height
d=ImageDraw.Draw(out)
out.save('sprite_scaling_compare.png'); print(out.size)
