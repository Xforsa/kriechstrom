#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KRIECHSTROM: a release, ready for GitHub and the Firmware Hub.  tools/krst_release.sh 0.1
#   build/release-X.Y/  kriechstrom-X.Y.fwsc, the source tarball, SHA256SUMS, the licences, the notes (docs/release-X.Y.md)
#   build/site/         the site (site/build.py), for GitHub Pages (tools/krst_pages.sh)
set -e
cd "$(dirname "$0")/.."
V="${1:?version, e.g. 0.1}"
./build.sh --release "$V"
python3 site/build.py "build/kriechstrom-$V.fwsc" "$V"
R="build/release-$V"
cp "build/site/src/kriechstrom-$V-src.tar.gz" "$R/"
[ -f "docs/release-$V.md" ] && cp "docs/release-$V.md" "$R/RELEASE.md"
(cd "$R" && rm -f SHA256SUMS && shasum -a 256 kriechstrom-*.fwsc kriechstrom-*-src.tar.gz > SHA256SUMS)
echo "release: $R"; ls "$R"
