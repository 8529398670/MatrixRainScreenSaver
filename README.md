# Matrix Rain screensaver

The digital rain from *The Matrix* (1999) as a real screensaver for
Windows, macOS and Debian/Ubuntu, built on the film's own pattern and
its exact 53 glyphs.

## Files
- `engine/` — the rain itself, portable C (see **Architecture**)
- `platform/windows/`, `platform/macos/`, `platform/linux/` — one thin shell per OS
- `tests/engine_test.c` — engine + config tests (`make test`, or any `build` script with `-Test` / `--test`)
- `Makefile` — Linux / macOS / MinGW-cross builds (the mac and Linux scripts call it)
- `.github/workflows/build.yml` — CI builds all three; tags `v*` get a release
- `config.yaml` — every setting, with comments
- `scripts/<os>/` — the same three scripts for each OS, run from anywhere:

  | | build | install (rebuilds first) | run now |
  |---|---|---|---|
  | `scripts/windows/` | `build.ps1` (`-Test`) | `install.ps1` (admin) | `run.bat` |
  | `scripts/macos/` | `build.sh` (`--test`) | `install.sh` | `run.sh` |
  | `scripts/linux/` | `build.sh` (`--test`) | `install.sh` (sudo) | `run.sh` |

  Windows: `powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1`.
  `install.ps1` puts it in System32 + the registry (10 min, no lock on
  resume) and points `MATRIXRAIN_CONFIG` at the repo's `config.yaml`.
  `scripts/linux/build-deb.sh` packages the Linux build as a `.deb`.
- `gitPushUpdate.bat` — commit everything, tag it `v1.0.N` (N = last
  commit message + 1), and push `main` to `origin`. Pass a message to
  use it instead: `gitPushUpdate.bat "msg"`.
- `fonts/matrix code nfi.ttf` — a fan font that was tried and not used (see Fonts)
- `fonts/vendor/` — git-ignored: fonts you're licensed to use but that
  can't be published, like MS Gothic (see Fonts). Filled by
  `scripts/fetch-fonts.sh` / `scripts\windows\fetch-fonts.ps1`.

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

There is one config file: `config.yaml` in the repo folder. Each OS's
install script points the installed screensaver at it, and the run
scripts use it too. Windows sets `MATRIXRAIN_CONFIG`; macOS and Linux link
the per-user config (see Install) to it. Lookup order: `MATRIXRAIN_CONFIG`
(or `--config FILE` on Linux), then `config.yaml` next to the program,
then the per-user file.

Keys: `speed_ms`, `font_name`, `font_size`, `cell_width_percent`,
`density_percent`, `trail_min`, `trail_max`, `trail_fade`,
`fall_speed_min`, `fall_speed_max`, `flip_percent`, `mutate_every_ticks`,
`changing_percent`, `glow_percent`, `cursor_percent`, `theme`, `themes`,
`exit_mouse_pixels`,
`wake_mouse_ms`, `wake_keys`, `debug_log`, and optional `glyphs` (override the glyph set).

**Themes:** the colours come from a named theme. Change the one
`theme:` line at the top of `config.yaml` to swap them all: `default`
(the film's green), `wsu-green` (Wright State Green #046A38 rain, Gold
#CBA052 heads on Black #101820) or `wsu-gold` (Gold #CBA052 rain, Yellow
#F8E08E heads), or `wsu`, which mixes the two: each falling string is
randomly green or gold. The themes are spelled out in the `themes:` block at the
end of the file; copy one, rename it, change its `head`, `rain`, `tail`
and `background` colours, and point `theme:` at it. A theme can also
mix up to four others: in place of its colours, list each one with its
share of the falling strings,

```yaml
  wsu:
    wsu-green: 0.75
    wsu-gold: 0.25
```

(three green strings to every gold one; the background comes from the
first). Leave a share empty and it gets what the others leave:
`wsu-green: 0.95` with a bare `wsu-gold:` gives gold the other 0.05. A file with no
`theme:` line still uses the old top-level `*_color` keys. On macOS the
theme is also in Options.

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
Needs a C compiler. `scripts\windows\build.ps1` uses the first it finds: Visual Studio
Build Tools (MSVC), MinGW-w64 `gcc` on PATH, or the MinGW cross compiler
inside WSL (`sudo apt install gcc-mingw-w64-x86-64 make`). Then run
`scripts\windows\install.ps1` from an admin PowerShell, and sign out and back in once.
Ready-built `.scr` files are attached to each GitHub release.

- `MatrixRain.scr /s` runs it, `/c` opens settings (speed, glyph size,
  column density), `/p <hwnd>` draws the Screen Saver Settings preview.
  `/r out.bmp [ticks] [width] [height]` renders a frame to a file
  without opening a window (for checking the look). Run it from a copy
  renamed to `.exe`: Windows launches any `.scr` started through the
  shell as `/s`, whatever the arguments.
- It exits once the mouse has kept moving for `wake_mouse_ms` (moves under
  `exit_mouse_pixels` are ignored), or after `wake_keys` key presses or
  clicks within 2 seconds of each other. With `wake_mouse_ms: 0` and
  `wake_keys: 1` any move, click or key wakes it. The Windows build reads
  these two settings; the macOS and Linux builds still wake at once.
- The process is named `MatrixRain.scr`, so `Get-Process MatrixRain`
  finds nothing; use `tasklist /FI "IMAGENAME eq MatrixRain.scr"`.

### macOS
On a Mac with the Xcode command line tools: `sh scripts/macos/install.sh`
builds it and copies it to `~/Library/Screen Savers` (`make macos` alone →
`build/MatrixRain.saver` (universal arm64 + x86_64, ad-hoc signed), or
download `MatrixRain.saver.zip` from a release and double-click the
`.saver`). It isn't notarized, so the first time macOS blocks
it: System Settings → Privacy & Security → "Open Anyway". Settings:
Screen Saver → Matrix Rain → Options (same three sliders, a Theme menu,
plus "Open config.yaml"). On Sonoma and later the screensaver runs sandboxed, so
it reads
`~/Library/Containers/com.apple.ScreenSaver.Engine.legacyScreenSaver/Data/Library/Application Support/MatrixRain/config.yaml`;
`install.sh` makes that a link to the repo's `config.yaml`, so the repo
file is the one to edit (a real file already there is kept as
`config.yaml.bak`).
When you wake the Mac the saver stops and its `legacyScreenSaver` host
process quits (Sonoma and later otherwise leave it animating invisibly
in the background, which eats CPU and slows unlocking).
macOS draws its own date and time over the screensaver on the main
display. To turn that off:
`defaults -currentHost write com.apple.screensaver showClock -bool false`
(`true` brings it back).

### Debian / Ubuntu
```
sudo apt install build-essential pkg-config libx11-dev libxft-dev fonts-noto-cjk
sh scripts/linux/install.sh                # = make linux && sudo make install-linux; or: make deb && sudo apt install ./build/matrixrain_*.deb
matrixrain --window                        # try it
```
- `install.sh` links `~/.config/matrixrain/config.yaml` to the repo's
  `config.yaml` (a real file already there is kept as `config.yaml.bak`),
  and `scripts/linux/run.sh` passes the repo file with `--config`, so
  everything reads the one file.
- **xscreensaver** (X11 sessions): add `matrixrain -root` to the
  `programs:` list in `~/.xscreensaver`, or pick it in `xscreensaver-settings`.
- **GNOME (Ubuntu's default, Wayland)** has no plug-in screensaver slot
  at all. `matrixrain-idle` watches GNOME's idle timer and runs
  `matrixrain --fullscreen` after 10 minutes:
  `systemctl --user enable --now matrixrain-idle.service`. It is not a
  lock screen; GNOME's own blank/lock timers still apply, so set "Screen
  Blank" longer than 10 minutes.

## The monitors turn off (or the computer sleeps) during the rain

That isn't the screensaver. Every OS has separate power timers that turn
the display off and put the whole computer to sleep, and they win over
any screensaver: when the display-off timer fires, the monitors go dark
even though the rain is still running. With the usual defaults (rain
after a few minutes, display off after 10) you see the rain for a few
minutes and then black.

Rule of thumb: set **display off** (and **sleep**) to longer than the
screensaver delay plus however long you want to watch the rain, or to
**Never**. The settings below cover the computer while it's plugged in; laptops
have a separate on-battery value, which is best left short.

### macOS
- System Settings → **Lock Screen** → "Turn display off on power adapter
  when inactive" → Never (or a long time). On the same page, "Start
  Screen Saver when inactive" is the rain's delay.
- System Settings → **Energy** (desktops) or **Battery → Options**
  (laptops) → "Prevent automatic sleeping when the display is off", if
  you leave the display on Never but still don't want the Mac sleeping.
- Terminal (`-c` = on the power adapter; `0` = never):

  ```bash
  pmset -g                          # current values: displaysleep, sleep
  sudo pmset -c displaysleep 0      # never turn the display off
  sudo pmset -c sleep 0             # never sleep the Mac
  ```

  Check what happened after the fact with
  `pmset -g log | grep -E "Display is turned off|Entering Sleep"`.
  If `sleep` shows "prevented by caffeinate" or similar, something
  (an app, a build) is holding it awake right now; the real value
  applies again once that ends.

### Windows
- Settings → System → **Power & battery** (Windows 11) or **Power &
  sleep** (Windows 10) → "When plugged in, turn off my screen after" and
  "...put my device to sleep after" → Never (or longer than the rain).
- `scripts\windows\install.ps1` sets the screensaver to start after 10
  minutes, and Windows often turns the screen off at 10 minutes too, so
  the rain may never show. Command prompt (`0` = never, values are minutes):

  ```bat
  powercfg /change monitor-timeout-ac 0
  powercfg /change standby-timeout-ac 0
  ```

  (`-dc` instead of `-ac` for on battery.) `powercfg /requests` shows
  what is keeping the computer awake, if anything.

### Linux
- **GNOME**: Settings → **Power** → "Screen Blank" → Never (or longer
  than `matrixrain-idle`'s 10 minutes), and "Automatic Suspend" → off
  when plugged in. Or:

  ```bash
  gsettings set org.gnome.desktop.session idle-delay 0
  gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-ac-type 'nothing'
  ```

  `idle-delay` is seconds (`0` = never). Screen Blank is also what
  starts GNOME's lock, so turning it off means GNOME stops locking on
  its own; lock with Super+L.
- **KDE Plasma**: System Settings → **Power Management** → "Screen
  Energy Saving" / "Turn off screen" and "Suspend session" for "On AC
  Power".
- **xscreensaver**: `xscreensaver-settings` → Advanced → **Display Power
  Management** → untick "Power Management Enabled", or set "Off After"
  well past "Blank After". (In `~/.xscreensaver`: `dpmsEnabled: False`.)
- **Plain X11**: `xset q` shows the DPMS timers; `xset -dpms` turns them
  off for this session.

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
- **Synchronized mutation.** Changing glyphs hold for 3 frames, then
  swap in place with one frame where old and new overlap at 50% each
  (fully lit where their strokes cross). Every changing glyph on the
  whole screen swaps on the SAME frame — the grid breathes in unison
  instead of fizzing like static. (`mutate_every_ticks`, default 4.)
- **Only some strings change.** Whole strings are either "changing"
  (every glyph swaps on every swap frame) or still (they barely
  change). The film's split isn't measured; `changing_percent`,
  default 40, is a guess to tune by eye.
- **Deletion strings, no fading.** Every string is followed by an
  invisible deletion string at about the same speed, writing blanks.
  That is what ends a trail: in the first film trails stay one
  brightness and get erased from the top, they don't dim. (`trail_fade:
  true` switches to the sequels' dimming trails.)
- **Cursors: only ~1 in 5 strings.** The bright white-green head is
  a cursor, and most strings don't have one — that's what gives the
  rain depth. When the head moves down, the cell it left keeps half the
  highlight for one frame (Newton's "partially highlighted" frame).
  (`cursor_percent`, default 20.)
- **Cursor stammer.** Every so often ALL cursors hesitate on the same
  tick, falling one row behind, like the system briefly stalled.
- **Glow.** The film's look came largely from compositing: blurs and
  glows over the code. The renderer adds a soft bloom (`glow_percent`,
  0 turns it off; it costs roughly 10 ms per 1080p frame).
- **Tall cells.** The glyphs are taller than they are wide; columns are
  75% of the glyph height (`cell_width_percent`).
- **Speed.** One tick is one film frame (`speed_ms: 42`, 24 fps), and
  strings grow 0.5–1 row per frame. Newton's frame counts suggest about
  one row per frame; the exact speed hasn't been measured against the
  footage. (An earlier "6.7 seconds per screen" figure came from the
  Matrix Desktop app, not the film.)
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
its `glyphs:` line, and draws each the way the film does: every
katakana mirrored (`flip_percent: 100`), 2 5 9 mirrored, 3 6 flipped
vertically, everything else as drawn. Comment the `glyphs:` line out to go back to
the built-in set (all 56 half-width katakana U+FF66–U+FF9D + 0–9 + Z +
symbols).

## Fonts
- **MS Gothic (Windows default)** — ships with Windows, a real Japanese
  font with full katakana/kanji coverage, including all 53 of the
  film's glyphs. Where `font_name:` isn't installed, macOS draws digits
  and symbols in its system font and katakana in Hiragino Sans, and
  Linux uses Noto Sans CJK JP; any glyph a font lacks falls back to
  another font that has it.
- **Vendoring MS Gothic on macOS / Linux** — it's Microsoft's, licensed
  with Windows and Office, so it can't go in this repo. To use your own
  copy anyway, run `sh scripts/fetch-fonts.sh`. It finds `msgothic.ttc`
  in Office for Mac, your font folders, or `C:\Windows\Fonts` under WSL,
  or you pass it a file. The font goes in `fonts/vendor/`, which git
  ignores. Then reinstall (`scripts/<os>/install.sh`). The saver loads
  everything in `fonts/vendor/` for itself only, from beside the
  config it reads (and from a `fonts/` copy beside the per-user config,
  which is how the sandboxed macOS saver gets it). Builds and packages
  never include these fonts.
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
