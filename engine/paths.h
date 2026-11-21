/* paths.h - the only OS-specific part of the engine: where config.yaml and
 * the log live, and UTF-8 file access. All paths are UTF-8. */
#ifndef MATRIXRAIN_PATHS_H
#define MATRIXRAIN_PATHS_H

#include <stddef.h>
#include <stdio.h>
#include "config.h"

FILE *rain_fopen(const char *path, const char *mode);
int rain_file_exists(const char *path);
int rain_mkdirs_for(const char *filepath); /* create the parent directories */

/* Per-OS user config file:
 *   Windows %APPDATA%\MatrixRain\config.yaml
 *   macOS   ~/Library/Application Support/MatrixRain/config.yaml
 *   Linux   $XDG_CONFIG_HOME/matrixrain/config.yaml (~/.config/...) */
int rain_user_config_path(char *out, size_t n);

/* Lookup order: $MATRIXRAIN_CONFIG, then config.yaml in exe_dir (may be
 * NULL), then the user config file. Returns out, or NULL if none exists. */
const char *rain_resolve_config(char *out, size_t n, const char *exe_dir);

/* Defaults, then the resolved config.yaml (writing a commented default to
 * the user path if none exists). loaded_from gets the path used. Opens the
 * log when debug_log is on. */
void rain_load_config(rain_config *cfg, const char *exe_dir, char *loaded_from, size_t n);

/* Folders to load extra fonts from (the bundled open fonts, and any
 * vendored ones), most specific first. Only folders that exist are
 * returned, each once:
 *   - `fonts` beside the user config file (install scripts copy fonts
 *     there: %APPDATA%\MatrixRain\fonts, ~/Library/.../MatrixRain/fonts,
 *     ~/.config/matrixrain/fonts)
 *   - `fonts/vendor` and `fonts/open` beside the config.yaml in use, after
 *     following a symlink: the repo's own folders when the config is the
 *     repo's
 *   - `fonts` beside the program (inside the macOS .saver bundle, next to
 *     the .scr) and, on Linux, <prefix>/share/matrixrain/fonts for a
 *     program in <prefix>/libexec/xscreensaver.
 * Returns how many. */
#define RAIN_MAX_FONT_DIRS 6
int rain_font_dirs(const char *exe_dir, char out[RAIN_MAX_FONT_DIRS][1024]);

/* Log to the user log dir; a no-op unless debug_log was on at load. */
void rain_log(const char *fmt, ...);

#endif
