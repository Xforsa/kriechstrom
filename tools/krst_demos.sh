#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KRIECHSTROM: the site's sound demos. Renders them with the real sources (tests/krst_demos.c) into build/demos/*.wav and
# encodes them as AAC (build/demos/*.m4a, which site/build.py copies to audio/). Needs a C compiler and, for the .m4a,
# afconvert (macOS) or ffmpeg.
#   tools/krst_demos.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host build/demos
[ -d build/gen ] || { echo "no build/gen: run tests/run_tests.sh (or build.sh) first"; exit 1; }
cc -O2 -w -DKRST_PALETTE=0 -DKRST_LARGE_DEF=0 -DKRST_PANEL_DEF=0 \
    -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src -Ibuild/gen \
    -o build/host/krst_demos tests/krst_demos.c -lm
build/host/krst_demos build/demos
for w in build/demos/*.wav; do
    m="${w%.wav}.m4a"
    if command -v afconvert >/dev/null 2>&1; then
        afconvert -f m4af -d aac -b 128000 "$w" "$m"
    else
        ffmpeg -loglevel error -y -i "$w" -c:a aac -b:a 128k "$m"
    fi
done
ls -l build/demos/*.m4a
