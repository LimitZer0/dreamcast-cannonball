import os
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sprcmp import *
S=3
items=[[('01609b02',1,False),('00c0a23a',1,False)],[('01119c97',50,True)],[('01a1958f',64,False)]]
data=[[obj(k,p,f) for k,p,f in it] for it in items]
lv={}
bg=(214,196,150)
frames=[]
N=70
for i in range(N):
    fr=0.07*(0.60/0.07)**(i/(N-1))
    W,H=200,110
    canvas=Image.new('RGB',(W*2+10,H),bg)
    for side in (0,1):
        x=10
        for srcs in data:
            fw,fh=srcs[0][0],srcs[0][1]
            ow=max(2,round(fw*fr)); oh=max(2,round(fh*fr))
            src=srcs[0]
            for s in srcs:
                if s[0]>=ow and s[1]>=oh: src=s
            w,h,px,P,sh=src
            if side==0: im=to_rgba(arcade(px,w,h,ow,oh),ow,oh,P,sh)
            else:
                k=id(src)
                if k not in lv: lv[k]=levels_for(px,w,h,P)
                im=to_rgba(smooth(px,w,h,ow,oh,P,lv[k]),ow,oh,P,sh)
            canvas.paste(im,(side*(W+10)+x,H-5-oh),im)
            x+=int(fw*0.6)+6
    d=ImageDraw.Draw(canvas)
    d.text((4,2),"ARCADE",fill=(40,40,40)); d.text((W+14,2),"SMOOTH",fill=(40,40,40))
    frames.append(canvas.resize((canvas.width*S,canvas.height*S),Image.NEAREST))
frames[0].save('sprite_scaling_approach.gif',save_all=True,append_images=frames[1:]+[frames[-1]]*15,duration=60,loop=0)
