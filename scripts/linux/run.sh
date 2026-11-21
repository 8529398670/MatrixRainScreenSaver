#!/bin/sh
# Start the rain now: the local build if there is one, else the installed
# copy, with the repo's config.yaml. Defaults to --fullscreen; pass --window (or any matrixrain option)
# to change that.
#   sh scripts/linux/run.sh [--window | --fullscreen] [--config FILE]
cd "$(dirname "$0")/../.."
[ $# -gt 0 ] || set -- --fullscreen
# The repo's config.yaml, the same file the installed copy uses (a
# --config FILE given here comes later and wins).
set -- --config "$(pwd)/config.yaml" "$@"
if [ -x build/matrixrain ]; then exec build/matrixrain "$@"; fi
exec matrixrain "$@"
