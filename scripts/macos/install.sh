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
# One config for everything: the sandboxed saver reads its container's
# config.yaml, so make that a link to the repo's config.yaml (a real file
# already there is kept as config.yaml.bak).
repo_config="$(pwd)/config.yaml"
user_dir="$HOME/Library/Containers/com.apple.ScreenSaver.Engine.legacyScreenSaver/Data/Library/Application Support/MatrixRain"
mkdir -p "$user_dir"
if [ -f "$user_dir/config.yaml" ] && [ ! -L "$user_dir/config.yaml" ]; then
    mv "$user_dir/config.yaml" "$user_dir/config.yaml.bak"
fi
ln -sfn "$repo_config" "$user_dir/config.yaml"
# Vendored fonts (fonts/vendor/, git-ignored): the sandboxed saver loads
# them from a copy beside its config.
mkdir -p "$user_dir/fonts"
for f in fonts/vendor/*.ttf fonts/vendor/*.ttc fonts/vendor/*.otf fonts/vendor/*.otc; do
    [ -f "$f" ] && cp "$f" "$user_dir/fonts/" && echo "Font: $(basename "$f")"
done
# The host process keeps the old build loaded until it quits.
killall legacyScreenSaver 2>/dev/null || true
echo "Installed: $dest/MatrixRain.saver"
echo "Pick it in System Settings > Screen Saver. The first time, macOS may"
echo "block it: System Settings > Privacy & Security > Open Anyway."
echo "Settings: $repo_config (also Screen Saver > Matrix Rain > Options)."
