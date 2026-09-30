#!/usr/bin/env python3
"""Builds qrleaderboard.html from qrleaderboard.src.html: embeds the setup
disc template (setup_disc/build.sh), a blank memory card image and the icon.

  ./make_tool.py <setup_template.cdi> <blank_vmu.bin>
"""
import base64, gzip, re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
cdi, vmu = sys.argv[1], sys.argv[2]
src = open(os.path.join(here, 'qrleaderboard.src.html')).read()
icon = open(os.path.join(here, 'setup_disc', 'qr_icon.h')).read()
pal = re.search(r'QR_ICON_PAL\[16\] = \{(.*?)\}', icon, re.S).group(1)
data = re.search(r'QR_ICON_DATA\[512\] = \{(.*?)\}', icon, re.S).group(1)
pal = '[' + ', '.join(x.strip() for x in pal.split(',') if x.strip()) + ']'
data = ''.join('%02x' % int(x, 16) for x in data.replace('\n', '').split(',') if x.strip())
assert len(data) == 1024
b64 = lambda p: base64.b64encode(gzip.compress(open(p, 'rb').read(), 9, mtime=0)).decode()
blank = open(vmu, 'rb').read()
assert len(blank) == 131072 and blank[255 * 512] == 0x55
out = (src.replace("'__SETUP_CDI_GZ__'", "'" + b64(cdi) + "'")
          .replace("'__BLANK_VMU_GZ__'", "'" + b64(vmu) + "'")
          .replace('__ICON_PAL__', pal)
          .replace("'__ICON_DATA__'", "'" + data + "'"))
open(os.path.join(here, 'qrleaderboard.html'), 'w').write(out)
print('wrote qrleaderboard.html', len(out), 'bytes')
