#!/bin/sh
# Copy fonts you are licensed to use into fonts/vendor/ (git-ignored), so the
# saver can load them on machines where they aren't installed.
#   sh scripts/fetch-fonts.sh               # find MS Gothic on this machine
#   sh scripts/fetch-fonts.sh FILE...       # vendor these font files instead
# MS Gothic is looked for in Microsoft Office for Mac, the user and system
# font folders, and (under WSL) the Windows font folder.
set -eu
cd "$(dirname "$0")/.."
dest=fonts/vendor
mkdir -p "$dest"
if [ $# -gt 0 ]; then
    for f in "$@"; do cp "$f" "$dest/" && echo "Vendored: $dest/$(basename "$f")"; done
    exit 0
fi
for f in \
    "$HOME/Library/Fonts/msgothic.ttc" \
    "/Library/Fonts/msgothic.ttc" \
    "/Applications/Microsoft Word.app/Contents/Resources/DFonts/msgothic.ttc" \
    "/Applications/Microsoft PowerPoint.app/Contents/Resources/DFonts/msgothic.ttc" \
    "/Applications/Microsoft Excel.app/Contents/Resources/DFonts/msgothic.ttc" \
    "/Applications/Microsoft Outlook.app/Contents/Resources/DFonts/msgothic.ttc" \
    "/mnt/c/Windows/Fonts/msgothic.ttc" \
    "$HOME/.local/share/fonts/msgothic.ttc" \
    "$HOME/.fonts/msgothic.ttc"; do
    if [ -f "$f" ]; then
        cp "$f" "$dest/msgothic.ttc"
        echo "Vendored: $dest/msgothic.ttc (from $f)"
        exit 0
    fi
done
echo "MS Gothic not found. Copy msgothic.ttc from a Windows PC (C:\\Windows\\Fonts)" >&2
echo "and run: sh scripts/fetch-fonts.sh /path/to/msgothic.ttc" >&2
exit 1
