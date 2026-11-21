# fonts/vendor

Fonts you are licensed to use but that can't go in the public repo, such as
MS Gothic (which ships with Windows and Microsoft Office). Everything in this
folder except this README is ignored by git.

Fill it with:

    sh scripts/fetch-fonts.sh                          # macOS / Linux / WSL
    powershell -File scripts\windows\fetch-fonts.ps1   # Windows

Or drop `.ttf` / `.ttc` / `.otf` files in by hand.

The saver loads these fonts for itself only (nothing is installed system
wide), from:

- `fonts/vendor/` beside the config.yaml it reads, after following a symlink.
  That is this folder whenever the config points at the repo, as the install
  scripts set up.
- `fonts/` beside the per-user config file. The install scripts copy this
  folder there. The macOS saver runs sandboxed and reads its fonts from that
  copy.

Builds and packages never contain them, so a .saver, .scr or .deb you share
is clean. Set `font_name:` in config.yaml to the font's family name, e.g.
`MS Gothic`.
