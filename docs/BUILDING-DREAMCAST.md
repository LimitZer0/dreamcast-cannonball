# Building CannonBall for the Dreamcast

Both ways below make a bootable disc image, `cannonball.cdi`, for Flycast, a
GDEMU (or similar) or a CD-R. You need your own OutRun **revision B** ROMs.

## Recommended: EasyCompile (no installs)

EasyCompile is a web page that makes the disc on your computer, in the
browser. Nothing is installed and nothing is uploaded.

1. Download `easycompile.html` from the top of this repository (on GitHub:
   open the file and click **Download raw file**).
2. Open it in Chrome, Edge or Firefox.
3. Drop your OutRun ROM zip on the page (a zip or the loose files; names
   don't matter).
4. Optionally pick up to four songs to replace the tunes (MP3, OGG, WAV,
   FLAC or M4A). In the game, choose SETTINGS > SOUND > MUSIC SOURCE.
5. Click **Make disc**, then **Download cannonball.cdi**.

The page records the original music and sound effects from your ROMs (it
takes a few seconds), exactly like the full build below.

## Building it yourself

For changing the code, or building without the web page.

You need a Linux shell. On Windows, use **WSL** (Windows Subsystem for
Linux) with Ubuntu:

1. Right-click Start and open **Terminal (Admin)** (or **Windows PowerShell
   (Admin)**), then run:

   ```
   wsl --install -d Ubuntu
   ```

   Restart the PC if it asks.
2. Check it's there with `wsl -l -v`: Ubuntu should be listed. If it isn't,
   run `wsl --install -d Ubuntu` again, or install **Ubuntu** from the
   Microsoft Store.
3. Open **Ubuntu** from the Start menu (or type `wsl` in PowerShell). The
   first time, it asks you to create a username and password (only for
   Ubuntu; the build asks for this password when it installs packages).

If it fails with a virtualization error (such as `0x80370102`), turn on
virtualization in the PC's BIOS/UEFI settings (called Intel VT-x, AMD-V or
SVM), then try again.

Every command below goes into the Ubuntu window. (macOS works too but isn't
covered here; see the KallistiOS docs.)

### Quick way: one script

```
sudo apt install git
git clone -b dreamcast-pvr <this repository's URL> cannonball
```

Copy your OutRun **revision B** ROM files (unzipped) into
`cannonball/build-dc/cd/roms/`, add any custom songs to
`cannonball/build-dc/cd/music/` (see step 7 below), then run:

```
cannonball/build-dc/build.sh
```

The first run installs and builds everything below (it asks for your
password to install packages, and building the Dreamcast compiler can take
an hour). Later runs only rebuild the game and the disc, in a few minutes.
It finishes with `build-dc/cannonball.cdi`.

### By hand

The same steps the script runs. Steps 1 to 4 are a one-time setup; after
that, building is steps 6 and 7 (step 5 once, to get the code and the
recording tool).

#### 1. Packages

```
sudo apt update
sudo apt install build-essential git cmake texinfo libjpeg-dev libpng-dev \
    libelf-dev python3 wget genisoimage ffmpeg libsdl2-dev libboost-dev
```

`ffmpeg` is for converting music, and `libsdl2-dev` / `libboost-dev` are for
the tool that records the original sound (step 5).

#### 2. KallistiOS and its compiler

```
sudo mkdir -p /opt/toolchains/dc && sudo chown $USER /opt/toolchains/dc
cd /opt/toolchains/dc
git clone https://github.com/KallistiOS/KallistiOS kos
git clone https://github.com/KallistiOS/kos-ports

cd kos/utils/kos-chain
cp Makefile.dreamcast.cfg Makefile.cfg
make                          # builds the sh-elf compiler (long)
make distclean                # frees the ~7 GB of build leftovers

cd /opt/toolchains/dc/kos
cp doc/environ.sh.sample environ.sh
source environ.sh
make                          # builds KallistiOS
```

Every new terminal needs `source /opt/toolchains/dc/kos/environ.sh` before
building. To make that automatic:
`echo "source /opt/toolchains/dc/kos/environ.sh" >> ~/.bashrc`

If something fails here, the KallistiOS toolchain docs
(`kos/utils/kos-chain/README.md` and `doc/debian.md`) have the details.

#### 3. Libraries: GLdc and SDL2

```
cd /opt/toolchains/dc/kos-ports/libGL
make install clean            # GLdc (SDL2's Dreamcast driver uses it)

cd /opt/toolchains/dc
git clone -b dreamcastSDL2 https://github.com/GPF/SDL SDL2-dc
cd SDL2-dc/build-scripts
./dreamcast.sh install        # builds SDL2 and installs it into KallistiOS
```

#### 4. The disc tool (cdi4dc)

```
cd /opt/toolchains/dc
git clone https://github.com/Kazade/img4dc
cd img4dc && mkdir build && cd build && cmake .. && make
mkdir -p /opt/toolchains/dc/bin && cp cdi4dc/cdi4dc /opt/toolchains/dc/bin/
```

#### 5. The code and the recording tool

```
git clone -b dreamcast-pvr <this repository's URL> cannonball
cd cannonball
tools/render_audio.sh ~/cannonball-render
```

The recording tool records the original music and FM effects from your
ROMs when the disc is made. The Dreamcast plays these recordings instead of
emulating the sound chip, which is what keeps the game at 60 fps. It only
needs building once.

#### 6. Build the game

```
cd build-dc
./dreamcast.sh
```

This makes `build-dc/cannonball.elf`. (For a later update: `git pull`, then
`./dreamcast.sh` again.)

#### 7. Make the disc

1. **ROMs:** copy your OutRun **revision B** ROM files into `build-dc/cd/roms/`
   (unzipped; names don't matter, they're found by checksum).
2. **Custom music (optional):** put up to four songs in `build-dc/cd/music/`,
   named `1` to `4` or by title, in any format ffmpeg reads
   (e.g. `1.mp3` or `Passing Breeze.mp3`):
   1 Magical Sound Shower, 2 Passing Breeze, 3 Splash Wave,
   4 Last Wave (high score screen). In the game, choose
   SETTINGS > SOUND > MUSIC SOURCE.
3. **Make the disc** (from `build-dc`):

   ```
   RENDER_TOOL=~/cannonball-render/cannonball ./make_cdi.sh cannonball.elf cd cannonball.cdi
   ```

`cannonball.cdi` is in `build-dc/`. On Windows it's at
`\\wsl$\Ubuntu\home\<your name>\cannonball\build-dc\` in File Explorer.

## Settings

`build-dc/cd/config.xml` holds the default settings on the disc (display,
controls, sound, …). Players can change them in the game's menus; they save
to the memory card.
