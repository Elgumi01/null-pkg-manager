/*
 * remove.c - package remove pipeline for npkg
 *
 * Reads a package manifest from INSTALLED_DIR/<name>.json, unlinks every
 * file listed in 'files' field and deletes the manifest itself.
*/

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>
#include <string.h>

#include "config.h"

#define NPKG_PATH_MAX    512
#define COMMAND_MAX      512

#define ERR(...) fprintf(stderr, "npkg: " __VA_ARGS__)
#define STATUS(...) printf("=> " __VA_ARGS__)

/*
 * Verify if 'package_name' is a valid name, returning 1 if
 * valid and 0 otherwise.
*/

static int is_valid_package_name(char *package_name)
{
    if (!package_name || package_name[0] == '\0')
        return 0;

    if (strcmp(package_name, ".") == 0 || strcmp(package_name, "..") == 0)
        return 0;

    if (strchr(package_name, '/') != NULL)
        return 0;

    return 1;
}

/*
 * Remove every file on the 'files' field on the <package> .json.
 * Keeps going even if a unlink fails, so a parcially broken install
 * doesn't get stuck. Return the number of failures for further
 * verification.
*/

static int remove_files(cJSON *files)
{
    int failures = 0;
    cJSON *file = NULL;

    cJSON_ArrayForEach(file, files)
    {
        char *path = cJSON_GetStringValue(file);

        if (!path)
        {
            ERR("skipping non-string entry in manifest\n");
            failures++;
            continue;
        }

        if (unlink(path) != 0)
        {
            if (errno = ENOENT)
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

int package_remove(char *package_name)
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

    char manifest_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(manifest_path, sizeof(manifest_path), "%s%s.json", INSTALLED_DIR, package_name);

    struct stat st;
    if (stat(manifest_path, &st) != 0)
    {
        ERR("package '%s' is not installed\n", package_name);
        return 1;
    }

    FILE *package_file = fopen(manifest_path, "r");
    if (!package_file)
    {
        ERR("failed to open %s%s.json", PACKAGES_JSON, package_name);
        return 1;
    }

    fseek(package_file, 0, SEEK_END);
    long size = ftell(package_file);
    fseek(package_file, 0, SEEK_SET);

    char *buffer = malloc(size + 1);

    if (!buffer)
    {
        ERR("failed to allocate memory for buffer!\n");
        fclose(package_file);
        return 1;
    }

    fread(buffer, 1, size, package_file);
    buffer[size] = '\0';

    cJSON *package_json = cJSON_Parse(buffer);

    free(buffer);

    if (!package_json)
    {
        ERR("failed to parse package.json!\n");
        return 1;
    }

    STATUS("removing %s...\n", package_name);

    int failures = remove_files(cJSON_GetObjectItem(package_json, "files"));
    cJSON_Delete(package_json);

    if (failures > 0)
    {
        ERR("%d file(s) couldn't be removed for '%s', manifest kept for a removel retry\n", failures, package_name);
        return 1;
    }

    if (remove(manifest_path) != 0)
    {
        ERR("failed to remove package manifest %s\n", manifest_path);
        return 1;
    }

    STATUS("removed %s successfully!\n", package_name);

    return 0;
}
