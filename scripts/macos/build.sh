#!/bin/sh
# Build build/MatrixRain.saver (universal, ad-hoc signed). Needs the Xcode
# command line tools.
#   sh scripts/macos/build.sh          # build
#   sh scripts/macos/build.sh --test   # run the engine tests, then build
set -eu
cd "$(dirname "$0")/../.."
if [ "${1:-}" = "--test" ]; then make test; fi
make macos
echo "Built: $(pwd)/build/MatrixRain.saver"
