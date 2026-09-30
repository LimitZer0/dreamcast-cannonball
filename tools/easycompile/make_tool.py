#!/usr/bin/env python3
"""Builds easycompile.html: easycompile.src.html with easycompile_core.js
inlined and the finished game embedded (gzip + base64):

  1ST_READ.BIN   the scrambled game (see build-dc/make_cdi.sh)
  IP.BIN         the boot sector
  config.xml     build-dc/cd/config.xml
  res/           tilemap.bin, tilepatch.bin, gamecontrollerdb.txt
  cdi/           the CDI layout (cdi_template.gz, from cdi4dc)

and the sound recording tool compiled to WebAssembly (wasm/cbrender.js,
made by wasm/build_wasm.sh), which records the original music and effects
from the player's ROMs in the browser.

  ./make_tool.py <1ST_READ.BIN> <IP.BIN> [version]

No ROMs or ROM-derived audio go in: players add their own ROMs on the page.
"""
import base64, gzip, os, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..'))
first_read, ipbin = sys.argv[1], sys.argv[2]
if len(sys.argv) > 3:
    version = sys.argv[3]
else:
    try:
        version = subprocess.check_output(['git', '-C', repo, 'log', '-1', '--format=%cd', '--date=short'], text=True).strip()
    except Exception:
        version = 'unknown'

files = {}
files['1ST_READ.BIN'] = open(first_read, 'rb').read()
files['IP.BIN'] = open(ipbin, 'rb').read()
assert len(files['IP.BIN']) == 32768, 'IP.BIN must be 32 KB'
files['config.xml'] = open(os.path.join(repo, 'build-dc', 'cd', 'config.xml'), 'rb').read()
for name in ('tilemap.bin', 'tilepatch.bin', 'gamecontrollerdb.txt'):
    files['res/' + name] = open(os.path.join(repo, 'build-dc', 'cd', 'res', name), 'rb').read()
t = gzip.decompress(open(os.path.join(here, 'cdi_template.gz'), 'rb').read())
a, b, c = (int.from_bytes(t[i:i + 4], 'little') for i in (0, 4, 8))
files['cdi/prefix'] = t[12:12 + a]
files['cdi/gaptail'] = t[12 + a:12 + a + b]
files['cdi/header'] = t[12 + a + b:12 + a + b + c]

pack = len(files).to_bytes(4, 'little')
for name, data in files.items():
    n = name.encode()
    pack += len(n).to_bytes(2, 'little') + n + len(data).to_bytes(4, 'little') + data
game = base64.b64encode(gzip.compress(pack, 9, mtime=0)).decode()

render = base64.b64encode(gzip.compress(open(os.path.join(here, 'wasm', 'cbrender.js'), 'rb').read(), 9, mtime=0)).decode()

src = open(os.path.join(here, 'easycompile.src.html')).read()
core = open(os.path.join(here, 'easycompile_core.js')).read()
out = (src.replace('__CORE_JS__', core)
          .replace("'__GAME_GZ__'", "'" + game + "'")
          .replace("'__RENDER_GZ__'", "'" + render + "'")
          .replace('__VERSION__', version))
open(os.path.join(here, 'easycompile.html'), 'w').write(out)
open(os.path.join(repo, 'easycompile.html'), 'w').write(out)   # copy at the top of the repo
print('wrote easycompile.html', len(out), 'bytes, game', version)
