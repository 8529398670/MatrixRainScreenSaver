#!/bin/sh
# Rebuild, then install MatrixRain.saver for this user
# (~/Library/Screen Savers). Pick it in System Settings > Screen Saver.
#   sh scripts/macos/install.sh
set -eu
cd "$(dirname "$0")/../.."
sh scripts/macos/build.sh
dest="$HOME/Library/Screen Savers"
mkdir -p "$dest"
rm -rf "$dest/MatrixRain.saver"
cp -R build/MatrixRain.saver "$dest/"
repo_config="$(pwd)/config.yaml"
# One config for everything. The saver looks for config.yaml inside its
# own bundle before its sandbox container, so link the installed bundle's
# to the repo's (and re-sign, as the bundle changed). This works over SSH
# too, where macOS blocks writing to the container.
ln -sfn "$repo_config" "$dest/MatrixRain.saver/Contents/Resources/config.yaml"
codesign --force --sign - "$dest/MatrixRain.saver" 2>/dev/null
# Also link the container's config.yaml (a real file already there is kept
# as config.yaml.bak), and copy vendored fonts (fonts/vendor/, git-ignored)
# beside it, where the sandboxed saver can read them. The bundled open
# fonts (fonts/open/) are inside the .saver already.
user_dir="$HOME/Library/Containers/com.apple.ScreenSaver.Engine.legacyScreenSaver/Data/Library/Application Support/MatrixRain"
container() {
    mkdir -p "$user_dir/fonts" || return 1
    if [ -f "$user_dir/config.yaml" ] && [ ! -L "$user_dir/config.yaml" ]; then
        mv "$user_dir/config.yaml" "$user_dir/config.yaml.bak" || return 1
    fi
    ln -sfn "$repo_config" "$user_dir/config.yaml" || return 1
    for f in fonts/vendor/*.ttf fonts/vendor/*.ttc fonts/vendor/*.otf fonts/vendor/*.otc; do
        if [ -f "$f" ]; then cp "$f" "$user_dir/fonts/" && echo "Font: $(basename "$f")" || return 1; fi
    done
}
if ! container 2>/dev/null; then
    echo "Note: couldn't write the screen saver's container (macOS blocks it over SSH)."
    echo "The bundle's config link is enough; run this once in Terminal to copy fonts/vendor/ too."
fi
# The host process keeps the old build loaded until it quits.
killall legacyScreenSaver 2>/dev/null || true
echo "Installed: $dest/MatrixRain.saver"
echo "Pick it in System Settings > Screen Saver. The first time, macOS may"
echo "block it: System Settings > Privacy & Security > Open Anyway."
echo "Settings: $repo_config (also Screen Saver > Matrix Rain > Options)."
