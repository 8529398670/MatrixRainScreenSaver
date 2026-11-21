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

/* Folders to load extra (vendored) fonts from, most specific first:
 *   - `fonts` beside the user config file (install scripts copy
 *     fonts/vendor/ there: %APPDATA%\MatrixRain\fonts,
 *     ~/Library/.../MatrixRain/fonts, ~/.config/matrixrain/fonts)
 *   - `fonts/vendor` beside the config.yaml in use, after following a
 *     symlink - the repo's own folder when the config points at the repo.
 * Only folders that exist are returned. Returns how many. */
#define RAIN_MAX_FONT_DIRS 2
int rain_font_dirs(const char *exe_dir, char out[RAIN_MAX_FONT_DIRS][1024]);

/* Log to the user log dir; a no-op unless debug_log was on at load. */
void rain_log(const char *fmt, ...);

#endif
