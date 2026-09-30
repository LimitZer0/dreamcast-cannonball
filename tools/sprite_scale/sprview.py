import struct,sys,glob
from PIL import Image
import os
D=os.environ.get('SPRS', 'sprs') + '/'
def load(key):
    fs=sorted(glob.glob(D+key+'_*.raw'), key=lambda f:-int(f.split('_')[-1][:-4]))
    b=open(fs[0],'rb').read(); w,h=struct.unpack('<HH',b[:4])
    return w,h,list(b[4:4+w*h])
def pal(p):
    b=open(D+'pal_%s.pal'%p,'rb').read(); return [tuple(b[i*3:i*3+3]) for i in range(16)]
def to_img(w,h,px,P,shadow=True,bg=None):
    im=Image.new('RGBA',(w,h),(0,0,0,0))
    for y in range(h):
        for x in range(w):
            c=px[y*w+x]
            if c in (0,15): continue
            if shadow and c==10: im.putpixel((x,y),(0,0,0,110)); continue
            im.putpixel((x,y),P[c]+(255,))
    return im
