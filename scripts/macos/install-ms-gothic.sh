#!/bin/sh
# Install MS Gothic for this Mac user from Microsoft Office for Mac, which
# keeps its own private copy (msgothic.ttc) inside each Office app. Your
# Office license covers using it on your Mac; don't put it in the repo or
# share it (see README > Fonts).
#   sh scripts/macos/install-ms-gothic.sh            # install to ~/Library/Fonts
#   sh scripts/macos/install-ms-gothic.sh --use      # ...and set font_name: "MS Gothic"
#   sh scripts/macos/install-ms-gothic.sh --vendor   # ...and copy it to fonts/vendor/ too
# Options can be combined. Nothing needs reinstalling afterwards: the saver
# picks the font up the next time it starts.
set -eu
cd "$(dirname "$0")/../.."
use=0 vendor=0
for a in "$@"; do
    case "$a" in
        --use) use=1 ;;
        --vendor) vendor=1 ;;
        *) echo "unknown option: $a (use --use, --vendor)" >&2; exit 2 ;;
    esac
done

src=""
for app in "Microsoft Word" "Microsoft PowerPoint" "Microsoft Excel" "Microsoft Outlook" "Microsoft OneNote"; do
    f="/Applications/$app.app/Contents/Resources/DFonts/msgothic.ttc"
    if [ -f "$f" ]; then src="$f"; break; fi
done
dest="$HOME/Library/Fonts/msgothic.ttc"
if [ -z "$src" ]; then
    if [ -f "$dest" ]; then
        echo "Already installed: $dest"
        src="$dest"
    else
        echo "MS Gothic not found: no Microsoft Office for Mac app in /Applications has it." >&2
        echo "Copy msgothic.ttc from a Windows PC (C:\\Windows\\Fonts) to ~/Library/Fonts instead." >&2
        exit 1
    fi
elif cmp -s "$src" "$dest" 2>/dev/null; then
    echo "Already installed: $dest"
else
    mkdir -p "$HOME/Library/Fonts"
    cp "$src" "$dest"
    echo "Installed: $dest (from $src)"
fi

if [ "$vendor" = 1 ]; then
    mkdir -p fonts/vendor
    cp "$src" fonts/vendor/msgothic.ttc
    echo "Vendored: fonts/vendor/msgothic.ttc (git-ignored)"
fi

if [ "$use" = 1 ]; then
    if grep -q '^font_name:' config.yaml; then
        sed -i '' 's/^font_name:.*/font_name: "MS Gothic"/' config.yaml
    else
        printf 'font_name: "MS Gothic"\n' >> config.yaml
    fi
    echo "config.yaml: font_name: \"MS Gothic\""
else
    echo "To use it, set font_name: \"MS Gothic\" in config.yaml (or rerun with --use)."
fi
