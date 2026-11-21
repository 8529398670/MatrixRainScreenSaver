#!/bin/sh
# build-deb.sh - package build/matrixrain as a .deb (run `make linux` first).
#   sh scripts/linux/build-deb.sh [VERSION]
set -eu
cd "$(dirname "$0")/../.."
VERSION=${1:-$(git describe --tags --always 2>/dev/null | sed 's/^v//' || echo 0)}
case "$VERSION" in [0-9]*) ;; *) VERSION="0~$VERSION" ;; esac
ARCH=$(dpkg --print-architecture)
STAGE=build/deb-stage-$VERSION
if [ -e "$STAGE" ]; then
    echo "$STAGE already exists - delete it first" >&2
    exit 1
fi
make install-linux DESTDIR="$STAGE" PREFIX=/usr >/dev/null
mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: matrixrain
Version: $VERSION
Section: x11
Priority: optional
Architecture: $ARCH
Depends: libc6, libx11-6, libxft2, fonts-noto-cjk
Recommends: xscreensaver
Maintainer: MatrixRain contributors <noreply@users.noreply.github.com>
Description: Matrix digital rain screensaver on the film's own pattern
 Fixed grid, strings that grow downward, synchronized glyph swaps,
 deletion strings and sparse bright cursors. Works as an xscreensaver
 hack (matrixrain -root) or standalone (matrixrain --fullscreen), with a
 GNOME idle helper (matrixrain-idle) for Wayland sessions.
CONTROL
OUT="build/matrixrain_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$OUT"
echo "Built $OUT"
