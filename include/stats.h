#pragma once

#include <stddef.h>

/**
 * Get system statistics as a JSON string.
 * @param buf The buffer to write to.
 * @param size The size of the buffer.
 * @return The number of bytes written.
 */
size_t pldmgr_get_system_stats_json(char *buf, size_t size);

/**
 * [CN-FORK] Resolve the icon0.png path of the currently running game.
 * Uses only the server-side probed title id (alphanumeric-validated, no client input).
 * @param out Buffer receiving the absolute file path.
 * @param out_size Size of out (must be >= 256).
 * @return 1 if an icon file was found, 0 otherwise.
 */
int pldmgr_get_game_icon_path(char *out, size_t out_size);

/**
 * [CN-FORK] Diagnostic plain-text dump: fw/model/serial probe results + full kenv
 * space (KENV_DUMP). Used to discover what PS5 firmware actually exposes.
 * @return number of bytes written.
 */
int stats_sysinfo_probe(char *buf, size_t size);
