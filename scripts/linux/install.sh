#!/bin/sh
# Rebuild, then install matrixrain system-wide (xscreensaver hack +
# GNOME idle helper) and point its config at the repo's config.yaml.
# Asks for sudo for the install step.
#   sh scripts/linux/install.sh
set -eu
cd "$(dirname "$0")/../.."
sh scripts/linux/build.sh
sudo make install-linux
# One config for everything: link the per-user config to the repo's
# config.yaml (a real file already there is kept as config.yaml.bak).
repo_config="$(pwd)/config.yaml"
user_dir="${XDG_CONFIG_HOME:-$HOME/.config}/matrixrain"
mkdir -p "$user_dir"
if [ -f "$user_dir/config.yaml" ] && [ ! -L "$user_dir/config.yaml" ]; then
    mv "$user_dir/config.yaml" "$user_dir/config.yaml.bak"
fi
ln -sfn "$repo_config" "$user_dir/config.yaml"
# Vendored fonts (fonts/vendor/, git-ignored), copied beside the config.
mkdir -p "$user_dir/fonts"
for f in fonts/vendor/*.ttf fonts/vendor/*.ttc fonts/vendor/*.otf fonts/vendor/*.otc; do
    [ -f "$f" ] && cp "$f" "$user_dir/fonts/" && echo "Font: $(basename "$f")"
done
echo "Settings: $repo_config"
