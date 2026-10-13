#!/usr/bin/env bash
# Installs everything the pipeline needs into ~/tools, without root:
#   ARM GNU toolchain 13.3, Renode 1.17 (portable), a Python venv with Robot
#   Framework + gcovr, and a local SDL2 for the simulator window.
# Optional system packages (need sudo) are listed at the end.
set -euo pipefail

TOOLS_DIR=${TOOLS_DIR:-$HOME/tools}
ARM_URL=https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz
RENODE_URL=https://github.com/renode/renode/releases/download/v1.17.0/renode-1.17.0.linux-portable.tar.gz
mkdir -p "$TOOLS_DIR"
cd "$TOOLS_DIR"

if [ ! -x arm-gnu/bin/arm-none-eabi-gcc ]; then
    echo "== ARM GNU toolchain"
    curl -sSL -o arm.tar.xz "$ARM_URL"
    mkdir -p arm-gnu && tar -xJf arm.tar.xz -C arm-gnu --strip-components=1 && rm arm.tar.xz
fi

if [ ! -x renode/renode ]; then
    echo "== Renode"
    curl -sSL -o renode.tar.gz "$RENODE_URL"
    mkdir -p renode && tar -xzf renode.tar.gz -C renode --strip-components=1 && rm renode.tar.gz
fi

if [ ! -x venv/bin/python ]; then
    echo "== Python venv (Robot Framework, gcovr)"
    if python3 -m venv venv 2> /dev/null; then
        :
    else
        # Ubuntu without python3-venv: create it without pip and bootstrap pip.
        python3 -m venv --without-pip venv
        curl -sSL -o get-pip.py https://bootstrap.pypa.io/get-pip.py
        venv/bin/python get-pip.py -q && rm get-pip.py
    fi
fi
venv/bin/pip install -q -r renode/tests/requirements.txt gcovr patchelf clang-format==23.1.3

if [ ! -f sdl2/usr/include/SDL2/SDL.h ] && ! pkg-config --exists sdl2 2> /dev/null; then
    echo "== SDL2 (local copy of the Ubuntu packages)"
    mkdir -p sdl2-debs sdl2
    (cd sdl2-debs && apt download libsdl2-2.0-0 libsdl2-dev libasound2t64 libpulse0 libsamplerate0 \
        libxss1 libdecor-0-0 libsndfile1 libasyncns0 libflac12t64 libvorbisenc2 libvorbis0a libogg0 \
        libopus0 libmpg123-0t64 libmp3lame0)
    for d in sdl2-debs/*.deb; do dpkg-deb -x "$d" sdl2; done
    # libpulse looks for libpulsecommon in /usr/lib/.../pulseaudio; point it at the local copy.
    L=sdl2/usr/lib/x86_64-linux-gnu
    for f in "$L"/libpulse.so.0.* "$L"/libpulse-simple.so.0.* "$L"/pulseaudio/libpulsecommon-*.so; do
        venv/bin/patchelf --set-rpath '$ORIGIN:$ORIGIN/pulseaudio:$ORIGIN/..' "$f"
    done
fi

if [ ! -x cppcheck/usr/bin/cppcheck ] && ! command -v cppcheck > /dev/null; then
    echo "== cppcheck (local copy of the Ubuntu package)"
    mkdir -p cppcheck-debs cppcheck
    (cd cppcheck-debs && apt download cppcheck libtinyxml2-10)
    for d in cppcheck-debs/*.deb; do dpkg-deb -x "$d" cppcheck; done
    venv/bin/patchelf --set-rpath '$ORIGIN/../lib/x86_64-linux-gnu' cppcheck/usr/bin/cppcheck
    cp -r cppcheck/usr/lib/x86_64-linux-gnu/cppcheck/cfg cppcheck/usr/bin/
fi

echo
echo "Tools installed in $TOOLS_DIR."
echo "Optional (sudo): sudo apt install cppcheck clang-format gdb-multiarch libsdl2-dev"
