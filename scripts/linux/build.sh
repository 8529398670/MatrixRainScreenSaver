#!/bin/sh
# Build build/matrixrain (X11). Needs:
#   sudo apt install build-essential pkg-config libx11-dev libxft-dev fonts-noto-cjk
#   sh scripts/linux/build.sh          # build
#   sh scripts/linux/build.sh --test   # run the engine tests, then build
set -eu
cd "$(dirname "$0")/../.."
if [ "${1:-}" = "--test" ]; then make test; fi
make linux
echo "Built: $(pwd)/build/matrixrain"
