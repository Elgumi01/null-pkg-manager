/*
 * remove.c - package remove pipeline for npkg
 *
 * Reads a package manifest from INSTALLED_DIR/<name>.json, unlinks every
 * file listed in 'files' field and deletes the manifest itself.
*/

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "common/env.h"
#include "common/lock.h"
#include "common/log.h"
#include "common/manifest.h"
#include "common/paths.h"
#include "common/validate.h"
#include "config.h"

/*
 * Check the entries coming out of the manifest bug elsewhere that writes
 * a bad path into a manifest shouldn't turn into "delete whatever that path
 * is, as root". Every entry must be a non-empty, root-relative path with no
 * ".." component.
*/

static int manifest_entry_is_safe(const char *path)
{
    if (!path || path[0] != '/') return 0;
    if (strstr(path, "..") != NULL) return 0;
    if (strlen(path) >= NPKG_PATH_MAX) return 0;
    return 1;
}


/*
 * Remove every file on the 'files' field on the <package> .json.
 * Keeps going even if a unlink fails, so a parcially broken install
 * doesn't get stuck. Return the number of failures for further
 * verification.
*/

static int remove_files(cJSON *files, const char *final_root)
{
    int failures = 0;
    cJSON *file = NULL;

    cJSON_ArrayForEach(file, files)
    {
        char *path = cJSON_GetStringValue(file);

        if (!manifest_entry_is_safe(path))
        {
            ERR("skipping invalid entry in manifest: '%s'\n", path ? path : "(non-string)");
            failures++;
            continue;
        }

        char full_path[NPKG_PATH_MAX + NPKG_PATH_SLACK] = {0};
        snprintf(full_path, sizeof(full_path), "%s%s", final_root, path + 1);

        if (unlink(full_path) != 0)
        {
            if (errno == ENOENT)
            {
                STATUS("already removed %s\n", path);

                continue;
            }

            ERR("failed to remove '%s': %s\n", path, strerror(errno));
            failures++;
            continue;
        }

        STATUS("removed %s\n", path);
    }

    return failures;
}

/*
 * Main package remove pipeline
 *
 * Checks if a package is installed. If so, check the package
 * name via is_valid_package_name and if returns 1 proceeds to
 * failures verification via a value returned by remove_files.
*/

int package_remove(const char *package_name)
{
    if (geteuid() != 0)
    {
        ERR("this command must be run as root\n");
        return 1;
    }

    if (!is_valid_package_name(package_name))
    {
        ERR("invalid package name '%s'\n", package_name ? package_name : "(null)");
        return 1;
    }

    if (acquire_lock() != 0) return 1;

    int rc = 0;

    const char *user_destdir = getenv("DESTDIR");

    if (manifest_set_final_root(user_destdir) != 0)
    {
        ERR("failed to set removal root\n");
        rc = 1;
        goto out;
    }

    if (user_destdir && user_destdir[0] != '\0')
        STATUS("using DESTDIR='%s' as removal root\n", manifest_get_final_root());

    char installed_dir[NPKG_PATH_MAX] = {0};
    manifest_installed_dir_path(installed_dir, sizeof(installed_dir));

    char manifest_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(manifest_path, sizeof(manifest_path), "%s%s.json", installed_dir, package_name);

    struct stat st;
    if (stat(manifest_path, &st) != 0)
    {
        ERR("package '%s' is not installed\n", package_name);
        rc = 1;
        goto out;
    }

    FILE *package_file = fopen(manifest_path, "r");
    if (!package_file)
    {
        ERR("failed to open %s: %s\n", manifest_path, strerror(errno));
        rc = 1;
        goto out;
    }

    if (!fd_is_trusted_root_file(fileno(package_file)))
    {
        ERR("refusing to use %s: must be owned by root and not writable by group/other\n", manifest_path);
        fclose(package_file);
        rc = 1;
        goto out;
    }


    if (fseek(package_file, 0, SEEK_END) != 0)
    {
        ERR("failed to seek %s: %s\n", manifest_path, strerror(errno));
        fclose(package_file);
        rc = 1;
        goto out;
    }

    long size = ftell(package_file);

    if (size < 0)
    {
        ERR("failed to determine size of %s: %s\n", manifest_path, strerror(errno));
        fclose(package_file);
        rc = 1;
        goto out;
    }

    if (fseek(package_file, 0, SEEK_SET) != 0)
    {
        ERR("failed to seek %s: %s\n", manifest_path, strerror(errno));
        fclose(package_file);
        rc = 1;
        goto out;
    }


    char *buffer = malloc(size + 1);

    if (!buffer)
    {
        ERR("failed to allocate memory for buffer!\n");
        fclose(package_file);
        rc = 1;
        goto out;
    }

    size_t nread = fread(buffer, 1, (size_t)size, package_file);
    fclose(package_file);

    if (nread != (size_t)size)
    {
        ERR("failed to read %s (short read)\n", manifest_path);
        free(buffer);
        rc = 1;
        goto out;
    }

    buffer[size] = '\0';

    cJSON *package_json = cJSON_Parse(buffer);
    free(buffer);

    if (!package_json)
    {
        ERR("failed to parse package.json!\n");
        rc = 1;
        goto out;
    }

    STATUS("removing %s...\n", package_name);

    int failures = remove_files(cJSON_GetObjectItem(package_json, "files"), manifest_get_final_root());
    cJSON_Delete(package_json);

    if (failures > 0)
    {
        ERR("%d file(s) couldn't be removed for '%s', manifest kept for a removel retry\n", failures, package_name);
        rc = 1;
        goto out;
    }

    if (remove(manifest_path) != 0)
    {
        ERR("failed to remove package manifest %s\n", manifest_path);
        rc = 1;
        goto out;
    }

    STATUS("removed %s successfully!\n", package_name);

out:
    release_lock();
    return rc;
}
