/*
 * manifest.c - final install root (DESTDIR) tracking plus reading and
 * writing per-package manifests under INSTALLED_DIR.
*/

#define _GNU_SOURCE

#include "common/manifest.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/log.h"
#include "common/paths.h"
#include "common/proc.h"
#include "config.h"

static char g_final_root[NPKG_PATH_MAX] = "/";

int manifest_set_final_root(const char *destdir)
{
    if (!destdir || destdir[0] == '\0')
    {
        snprintf(g_final_root, sizeof(g_final_root), "/");
        return 0;
    }

    if (destdir[0] != '/')
    {
        ERR("DESTDIR must be an absolute path\n");
        return 1;
    }

    size_t destdir_len = strlen(destdir);
    if (destdir_len >= sizeof(g_final_root) - NPKG_PATH_SLACK)
    {
        ERR("DESTDIR too long\n");
        return 1;
    }

    snprintf(g_final_root, sizeof(g_final_root), "%s", destdir);
    size_t len = strlen(g_final_root);
    if (len > 0 && g_final_root[len - 1] != '/')
    {
        g_final_root[len] = '/';
        g_final_root[len + 1] = '\0';
    }

    return 0;
}

const char *manifest_get_final_root(void)
{
    return g_final_root;
}

/*
 * Mount the path INSTALLED_DIR inside the final root(g_final_root),
 * so the builds with differents DESTDIR have they own manifests
 * instead using the host one
*/

void manifest_installed_dir_path(char *out, size_t out_size)
{
    if (strcmp(g_final_root, "/") == 0)
    {
        snprintf(out, out_size, "%s", INSTALLED_DIR);
    }
    else
    {
        snprintf(out, out_size, "%s%s", g_final_root, INSTALLED_DIR);
    }
}

/* Return 1 if 'package_name' has a manifest under MANIFEST_DIR, 0 otherwise  */

int is_installed(const char *package_name)
{
    char installed_dir[NPKG_PATH_MAX] = {0};
    manifest_installed_dir_path(installed_dir, sizeof(installed_dir));

    char installed_package_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(installed_package_path,
             sizeof(installed_package_path),
             "%s%s.json",
             installed_dir, package_name);

    struct stat st;
    return stat(installed_package_path, &st) == 0;
}

/* Write INSTALLED_DIR/<package_name>.json containing the package_name,
 * version and installed packages. Writes it to a .tmp file first so a
 * crash mid-write cant corrupt an existing manifest.
*/

int write_manifest(const char *package_name, char *version, cJSON *files)
{
    cJSON *manifest = cJSON_CreateObject();

    if (!manifest)
    {
        ERR("out of memory building manifest for '%s'\n", package_name);
        cJSON_Delete(files);
        return 1;
    }

    cJSON_AddStringToObject(manifest, "name", package_name);
    cJSON_AddStringToObject(manifest, "version", version ? version : "unknown");
    cJSON_AddItemToObject(manifest, "files", files);

    char installed_dir[NPKG_PATH_MAX] = {0};
    manifest_installed_dir_path(installed_dir, sizeof(installed_dir));

    if (mkdir_p(installed_dir) != 0)
    {
        ERR("failed to create manifest directory %s\n", installed_dir);
        cJSON_Delete(manifest);
        return 1;
    }

    char manifest_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(manifest_path, sizeof(manifest_path), "%s%s.json", installed_dir, package_name);

    char tmp_path[sizeof(manifest_path) + 16] = {0};
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", manifest_path);

    FILE *manifest_file = fopen(tmp_path, "w");
    if (!manifest_file)
    {
        ERR("failed to write manifest for '%s': %s\n", package_name, strerror(errno));
        cJSON_Delete(manifest);
        return 1;
    }

    if (fchmod(fileno(manifest_file), 0644) != 0)
    {
        ERR("failed to set permissions on manifest for '%s': %s\n", package_name, strerror(errno));
        fclose(manifest_file);
        cJSON_Delete(manifest);
        unlink(tmp_path);
        return 1;
    }

    char *json_str = cJSON_Print(manifest);
    if (!json_str)
    {
        ERR("failed to serialize manifest for '%s'\n", package_name);
        fclose(manifest_file);
        cJSON_Delete(manifest);
        return 1;
    }

    int write_failed = fputs(json_str, manifest_file) == EOF;
    if (fclose(manifest_file) != 0) write_failed = 1;

    free(json_str);
    cJSON_Delete(manifest);

    if (write_failed)
    {
        ERR("failed to write manifest contents for '%s'\n", package_name);
        unlink(tmp_path);
        return 1;
    }

    if (rename(tmp_path, manifest_path) != 0)
    {
        ERR("failed to finalize manifest for '%s': %s\n", package_name, strerror(errno));
        unlink(tmp_path);
        return 1;
    }

    return 0;
}
