# Matrix Rain screensaver

The digital rain from *The Matrix* (1999) as a real screensaver for
Windows, macOS and Debian/Ubuntu, built on the film's own pattern and
its exact 53 glyphs.

## Files
- `engine/` — the rain itself, portable C (see **Architecture**)
- `platform/windows/`, `platform/macos/`, `platform/linux/` — one thin shell per OS
- `tests/engine_test.c` — engine + config tests (`build.ps1 -Test`, `make test`)
- `Makefile` — Linux / macOS / MinGW-cross builds; `scripts/build-deb.sh`
- `.github/workflows/build.yml` — CI builds all three; tags `v*` get a release
- `config.yaml` — every setting, with comments
- `build.ps1` — Windows build: `powershell -ExecutionPolicy Bypass -File build.ps1` (`-Test` also runs the tests)
- `install.ps1` — rebuilds, then installs it as the Windows screensaver
  (System32 + registry, 10 min, no lock on resume) and points
  `MATRIXRAIN_CONFIG` at this folder's `config.yaml`. Needs admin.
- `run.bat` — start the rain right away (local build, else the installed copy)
- `gitPushUpdate.bat` — commit everything, tag it `v1.0.N` (N = last
  commit message + 1), and push `main` to `origin`. Pass a message to
  use it instead: `gitPushUpdate.bat "msg"`.
- `fonts/matrix code nfi.ttf` — a fan font that was tried and not used (see Fonts)

## Architecture
A portable C engine with a thin native shell per OS. The animation is
identical everywhere because the engine also draws every frame.

```
engine/rain.c     the simulation: grid, strings, synchronized swaps, deletion
                  strings, cursors, stammer. No OS calls, seedable PRNG.
engine/render.c   draws a frame into a 32-bit pixel buffer from per-glyph
                  coverage masks - identical output on every OS.
engine/config.c   config.yaml reader/writer.
engine/paths.c    where config.yaml / the log live on each OS.
platform/windows  Win32 .scr: GDI rasterizes the glyphs once, SetDIBitsToDevice shows frames
platform/macos    ScreenSaverView .saver: Core Text glyphs, CGImage frames
platform/linux    X11 + Xft: xscreensaver hack, or standalone --fullscreen
```

Version 5 replaced an earlier single-file C# / WinForms build. Along
the way it fixed a hardcoded log path, blur on scaled displays,
multi-monitor bounds, startup columns that were lit end to end, and the
cost of ~10k GDI+ `DrawString` calls per frame (now a glyph atlas that
repaints only the cells that changed).

Tested on Windows 11: 9.7M engine checks pass (also under address/UB
sanitizers on Linux); `/s` fullscreen, single instance, and the `/c`
dialog. The Linux build was run under WSLg. Not yet tried: `/p` in
Screen Saver Settings, multiple monitors, and macOS on real hardware.

## Settings — config.yaml
Edit `config.yaml`, save, and the next run picks it up; no restart or
reinstall. A bad line is ignored and the rest still applies.

Keys: `speed_ms`, `font_name`, `font_size`, `density_percent`,
`trail_min`, `trail_max`, `fall_speed_min`, `fall_speed_max`,
`flip_percent`, `mutate_every_ticks`, `cursor_percent`, `head_color`,
`rain_color`, `tail_color`, `background_color`, `exit_mouse_pixels`,
`debug_log`, and optional `glyphs` (override the glyph set).

**Lookup order (all OSes):** the `MATRIXRAIN_CONFIG` env var →
`config.yaml` next to the binary → the per-user file
(`%APPDATA%\MatrixRain\config.yaml`,
`~/Library/Application Support/MatrixRain/config.yaml`,
`~/.config/matrixrain/config.yaml`). If none exists, a commented
default is written to the per-user file.

The file is UTF-8 so the katakana in `glyphs:` survive. Note that
PowerShell 5.1's `Get-Content` misreads it unless you pass
`-Encoding UTF8`.

Logging is off unless `debug_log: true`; then it goes to
`%LOCALAPPDATA%\MatrixRain\MatrixRain.log`, `~/Library/Logs/MatrixRain`,
or `~/.local/state/matrixrain`.

## Install

### Windows
Needs a C compiler. `build.ps1` uses the first it finds: Visual Studio
Build Tools (MSVC), MinGW-w64 `gcc` on PATH, or the MinGW cross compiler
inside WSL (`sudo apt install gcc-mingw-w64-x86-64 make`). Then run
`install.ps1` from an admin PowerShell, and sign out and back in once.
Ready-built `.scr` files are attached to each GitHub release.

- `MatrixRain.scr /s` runs it, `/c` opens settings (speed, glyph size,
  column density), `/p <hwnd>` draws the Screen Saver Settings preview.
- It exits on a mouse move over `exit_mouse_pixels`, a click, or a key.
- The process is named `MatrixRain.scr`, so `Get-Process MatrixRain`
  finds nothing; use `tasklist /FI "IMAGENAME eq MatrixRain.scr"`.

### macOS
On a Mac with the Xcode command line tools: `make macos` →
`build/MatrixRain.saver` (universal arm64 + x86_64, ad-hoc signed), or
download `MatrixRain.saver.zip` from a release. Double-click the
`.saver` to install. It isn't notarized, so the first time macOS blocks
it: System Settings → Privacy & Security → "Open Anyway". Settings:
Screen Saver → Matrix Rain → Options (same three sliders, plus "Open
config.yaml"). On Sonoma and later the screensaver runs sandboxed, so
its config lives in
`~/Library/Containers/com.apple.ScreenSaver.Engine.legacyScreenSaver/Data/Library/Application Support/MatrixRain/config.yaml`.

### Debian / Ubuntu
```
sudo apt install build-essential pkg-config libx11-dev libxft-dev fonts-noto-cjk
make linux && sudo make install-linux      # or: make deb && sudo apt install ./build/matrixrain_*.deb
matrixrain --window                        # try it
```
- **xscreensaver** (X11 sessions): add `matrixrain -root` to the
  `programs:` list in `~/.xscreensaver`, or pick it in `xscreensaver-settings`.
- **GNOME (Ubuntu's default, Wayland)** has no plug-in screensaver slot
  at all. `matrixrain-idle` watches GNOME's idle timer and runs
  `matrixrain --fullscreen` after 10 minutes:
  `systemctl --user enable --now matrixrain-idle.service`. It is not a
  lock screen; GNOME's own blank/lock timers still apply, so set "Screen
  Blank" longer than 10 minutes.

## The film's pattern
Most Matrix screensavers slide glyphs down the screen like a waterfall.
The film doesn't do that — frame-by-frame analysis (matrix.watch,
Carl Newton) shows the real pattern, and the engine follows it:

- **Nothing moves.** Every glyph sits in a fixed cell on a grid and
  stays there. A "string" grows by writing a new glyph in the cell
  below, one row per tick — a typewriter printing downward. What your
  eye reads as falling is the bottom edge advancing.
- **Invisible prefixes.** About 1 in 10 strings travels 1–20 blank
  cells before its first visible glyph, so strings seem to appear out
  of nowhere partway down the screen.
- **Synchronized mutation.** After ~3 frames a glyph can swap in
  place, cross-dissolving (old + new at 50% for one frame). Every
  changing glyph on the whole screen swaps on the SAME tick, then all
  hold still — the grid breathes in unison instead of fizzing like
  static. (`mutate_every_ticks`, default 3.)
- **Deletion strings.** Invisible strings travel down columns writing
  blanks, erasing visible strings from the top down. Trails don't
  fade out — they get actively dissolved from above. (A deletion
  string often follows a finished string to eat what's left.)
- **Cursors: only ~1 in 5 strings.** The bright white-green head is
  a cursor, and most strings don't have one — that's what gives the
  rain depth. (`cursor_percent`, default 20.)
- **Cursor stammer.** Every so often ALL cursors hesitate on the same
  tick, falling one row behind, like the system briefly stalled.
- **Cadence.** A string crosses the screen in about 6.7 seconds at
  the film's default speed — `fall_speed_min/max` default 0.30/0.55
  rows per tick at 50 ms ticks.
- **The sound** (not part of the screensaver, but part of the film's
  effect): sound designer Dane A. Davis — who won the Academy Award
  for Best Sound Editing for the film — built the rain's sound from
  digitized recordings of real raindrops hitting window panes, pitched
  and layered to match the density and rhythm of the glyphs.

## The film's glyphs — exactly 53
The digital rain in The Matrix (1999) uses a custom typeface designed
by production designer Simon Whiteley: mirror images of half-width
katakana, mixed with Western Latin letters and numerals. Whiteley has
said the katakana came from Japanese cookbooks and children's
alphabet books — the "sushi recipes" story (Snopes rates the literal
version "Mixture": the cookbooks were one input, the glyphs were
hand-drawn and stylised). **No official version of the film's
typeface was ever released** — so every font is an imitation, and the
closest anyone can get is the glyph *set* itself.

Carl Newton went through the opening sequence of the first film frame
by frame and wrote down every glyph he could identify:
https://carlnewton.github.io/digital-rain-analysis/ — **53 glyphs**:

| Group | Count | The glyphs |
| --- | --- | --- |
| Half-width katakana (all flipped horizontally) | 30 | ﾊ ﾋ ｼ ﾂ ｳ ｰ ﾅ ﾐ ﾓ ﾆ ｻ ﾜ ｵ ﾘ ﾎ ﾏ ｴ ｷ ﾑ ﾃ ｹ ﾒ ｶ ﾕ ﾗ ｾ ﾈ ｽ ﾀ ﾇ |
| Kanji (Kangxi radical) | 1 | 日 (sun/day) — the only kanji in the rain |
| Arabic numerals | 10 | 0 1 2 3 4 5 6 7 8 9 |
| Latin letter | 1 | Z — the only Latin letter |
| Symbols | 11 | * + : = . < > " ｜ ¦ _ |

Quirks from the analysis: there doesn't appear to be a separate 6 and
9 — they share one shape. Some digits are flipped per glyph: 2, 5 and
9 horizontally; 3 and 6 vertically; 0, 1, 4, 7, 8 not flipped. The
katakana are flipped horizontally (that's what makes them look alien to
Japanese readers). A second frame-by-frame pass (opening + dream scene)
found a couple more katakana (ｱ among them), so 53 is the opening
sequence's count, not necessarily every glyph in the whole trilogy.
The green matches old monochrome CRT text.

**This build rains exactly those 53**: `config.yaml` carries them in
its `glyphs:` line, with `flip_percent: 50` so half the glyphs are
mirrored like the film's. Comment the `glyphs:` line out to go back to
the built-in set (all 56 half-width katakana U+FF66–U+FF9D + 0–9 + Z +
symbols).

## Fonts
- **MS Gothic (Windows default)** — ships with Windows, a real Japanese
  font with full katakana/kanji coverage, including all 53 of the
  film's glyphs. macOS uses Hiragino Sans and Linux Noto Sans CJK JP;
  any glyph a font lacks falls back to another font that has it.
- **Matrix Code NFI (tried, rejected)** — fan-made font from dafont
  (Norfok Incredible Font Design, free for personal use). Only 98
  characters, all Latin A–Z / a–z / 0–9 / punctuation drawn to *look*
  katakana-ish — it has no katakana at all, so the film's glyph set
  can't be drawn in it.
- **Rezmason Matrix-Code, classic (not yet picked)** —
  https://github.com/Rezmason/matrix — free under the MIT license.
  Its glyphs were traced from cleaned-up vectors from an old SWF on
  the promotional site for the official Matrix product *Path of Neo*,
  so this is the closest to the film's actual drawn glyphs anywhere.
  60 glyphs: 34 full katakana (U+30A0–30FF, *not* half-width), 9
  digits, a few symbols. Because it uses full katakana, picking it
  means rewriting the `glyphs:` line to its full-katakana subset — the
  current half-width 53 won't render in it.
- **Rezmason Matrix-Resurrected (not yet picked)** — same repo, the
  expanded 135-symbol set from *The Matrix Resurrections* (2021),
  recovered from trailers and a watch ad. 141 glyphs, all in the
  Private Use Area (U+E900+), so the `glyphs:` line would need those
  code points instead of normal characters.

## Sources
- **The 53 glyphs + how the rain behaves** (frame-by-frame analysis
  of the first film's opening sequence): Carl Newton, "An analysis of
  the behaviour of the digital rain in The Matrix" —
  https://carlnewton.github.io/digital-rain-analysis/
- **The display pattern** (fixed grid, invisible prefixes,
  synchronized swaps, deletion strings, cursors, stammer, sound):
  "How the Matrix Digital Rain Works: A Frame-by-Frame Analysis" —
  https://matrix.watch/articles/how-matrix-digital-rain-works.html
- **The sushi-recipes origin** (the cookbooks, the alphabet books, the
  hand-drawing, and Snopes' "Mixture" rating of the literal claim):
  https://lenz.io/c/matrix-1999-digital-rain-code-japanese-sushi-recipes-8f97f343
- **Overview + the unreleased typeface**: Wikipedia, "Matrix digital
  rain" — https://en.wikipedia.org/wiki/Matrix_digital_rain
- **The Rezmason fonts** (Matrix-Code classic + Matrix-Resurrected,
  MIT license): https://github.com/Rezmason/matrix
- **The Matrix Code NFI font file** (fan font from dafont, tried and
  rejected): https://dl.dafont.com/dl/?f=matrix_code_nfi
