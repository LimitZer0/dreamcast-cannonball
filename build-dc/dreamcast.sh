  rm -rf CMakeCache.txt CMakeFiles

  source /opt/toolchains/dc/kos/environ.sh

  kos-cmake -S ../cmake -B . \
    -DTARGET=dreamcast.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL2_DIR=/opt/toolchains/dc/kos/addons/lib/dreamcast/cmake/SDL2

  cmake --build .
  