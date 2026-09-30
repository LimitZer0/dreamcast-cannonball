import os
import sys, math, struct, glob
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); from sprview import load, pal, D
from PIL import Image, ImageDraw

def lum(c): return 0.3*c[0]+0.59*c[1]+0.11*c[2]

def arcade(px,w,h,ow,oh):
    # hardware: nearest, source = floor(k * zoom), zoom = src/out
    out=[0]*(ow*oh)
    for y in range(oh):
        sy=min(h-1,(y*h)//oh)
        for x in range(ow):
            sx=min(w-1,(x*w)//ow)
            out[y*ow+x]=px[sy*w+sx]
    return out

def reduce2(px,w,h,P):
    nw,nh=(w+1)//2,(h+1)//2
    out=[0]*(nw*nh)
    for y in range(nh):
        for x in range(nw):
            cnt={}
            opaque=0
            for dy in (0,1):
                for dx in (0,1):
                    sx,sy=2*x+dx,2*y+dy
                    if sx>=w or sy>=h: continue
                    c=px[sy*w+sx]
                    if c in (0,15): continue
                    opaque+=1; cnt[c]=cnt.get(c,0)+1
            if opaque>=2:
                top=max(cnt.values())
                cands=[c for c,n in cnt.items() if n==top]
                if len(cands)>1:
                    # tie: the colour closest to the block's average
                    tot=[0,0,0]
                    for c,n in cnt.items():
                        for i in range(3): tot[i]+=P[c][i]*n
                    avg=[t/opaque for t in tot]
                    cands.sort(key=lambda c:sum((P[c][i]-avg[i])**2 for i in range(3)))
                out[y*nw+x]=cands[0]
    return out,nw,nh

def smooth(px,w,h,ow,oh,P,levels):
    # pick the smallest copy still at least as big as the output
    lv=levels[0]
    for l in levels:
        if l[1]>=ow and l[2]>=oh: lv=l
    return arcade(lv[0],lv[1],lv[2],ow,oh)

def to_rgba(px,w,h,P,shadow):
    im=Image.new('RGBA',(w,h),(0,0,0,0))
    for y in range(h):
        for x in range(w):
            c=px[y*w+x]
            if c in (0,15): continue
            im.putpixel((x,y),(0,0,0,110) if (shadow and c==10) else P[c]+(255,))
    return im

def levels_for(px,w,h,P):
    L=[(px,w,h)]
    while L[-1][1]>8 and L[-1][2]>8:
        L.append(reduce2(L[-1][0],L[-1][1],L[-1][2],P))
    return L

def obj(key,p,flip,shadow=True):
    w,h,px=load(key); P=pal(p)
    if flip:
        px=[px[y*w+(w-1-x)] for y in range(h) for x in range(w)]
    return w,h,px,P,shadow
