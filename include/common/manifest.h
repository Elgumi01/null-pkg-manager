#ifndef NPKG_COMMON_MANIFEST_H
#define NPKG_COMMON_MANIFEST_H

#include <stddef.h>

#include <cjson/cJSON.h>

/*
 * Final install root (DESTDIR) handling.
 *
 * By default the final root is "/". manifest_set_final_root() lets a
 * caller point every subsequent install at a staging DESTDIR instead
 * (each DESTDIR gets its own INSTALLED_DIR manifest tree, so builds
 * with different DESTDIRs don't collide with the host's).
*/

/*
 * Sets the final install root.
 *
 * 'destdir' may be NULL (or empty), in which case the final root is
 * reset to "/". Otherwise it must be an absolute path; a trailing '/'
 * is appended automatically if missing.
 *
 * Returns 0 on success, 1 if 'destdir' is invalid (not absolute, or
 * too long).
*/
int manifest_set_final_root(const char *destdir);

/* Returns the currently configured final root (defaults to "/"). */
const char *manifest_get_final_root(void);

/*
 * Builds the path to INSTALLED_DIR inside the current final root, so
 * builds with different DESTDIRs use their own manifests instead of
 * the host one.
*/
void manifest_installed_dir_path(char *out, size_t out_size);

/* Returns 1 if 'package_name' has a manifest under the final root's
 * INSTALLED_DIR, 0 otherwise. */
int is_installed(const char *package_name);

/*
 * Writes INSTALLED_DIR/<package_name>.json containing the package
 * name, version and installed files. Writes to a .tmp file first so a
 * crash mid-write can't corrupt an existing manifest.
 *
 * Takes ownership of 'files' (it is freed, whether this succeeds or
 * fails). Returns 0 on success, 1 on failure.
*/
int write_manifest(const char *package_name, char *version, cJSON *files);

#endif /* NPKG_COMMON_MANIFEST_H */
