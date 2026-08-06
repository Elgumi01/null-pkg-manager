/*
 * search.c - package search module for npkg
 *
 * Reads a package manifest from MANIFEST_DIR and shows
 * its information.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "config.h"

#define NPKG_PATH_MAX 512

#define ERR(...) fprintf(stderr, "npkg: " __VA_ARGS__)

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

/* Return 1 if 'package_name' has a manifest under MANIFEST_DIR, 0 otherwise  */

static int is_installed(char *package_name)
{
    char installed_package_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(installed_package_path,
             sizeof(installed_package_path),
             "%s%s.json",
             INSTALLED_DIR, package_name);

    struct stat st;
    return stat(installed_package_path, &st) == 0;
}

/*
 * Main package search routine
 *
 * Checks if a package is installed. If it isnot, validate the
 * package name via is_valid_package_name. If returns 1, proceeds to
 * search for 'PACKAGES_JSON/package_name.json'. If the manifest
 * exists, reads the fields 'description' and 'version' fields and
 * display then.
*/

int package_search(char *package_name)
{
    if (!is_valid_package_name(package_name))
    {
        ERR("invalid package name '%s'\n", package_name ? package_name : "(null)");
        return 1;
    }

    char package_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(package_path, sizeof(package_path), "%s%s.json", PACKAGES_JSON, package_name);

    struct stat st;
    if (stat(package_path, &st) != 0)
    {
        ERR("package '%s' not found in %s\n", package_name, PACKAGES_JSON);
        return 1;
    }

    int package_installed = is_installed(package_name);

    FILE *package_file = fopen(package_path, "r");
    if (!package_file)
    {
        ERR("failed to open %s\n", package_path);
        return 1;
    }

    fseek(package_file, 0, SEEK_END);
    long size = ftell(package_file);
    fseek(package_file, 0, SEEK_SET);

    if (size < 0)
    {
        ERR("failed to determine size of %s\n", package_name);
        fclose(package_file);
        return 1;
    }

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

    cJSON *description = cJSON_GetObjectItem(package_json, "description");
    cJSON *version     = cJSON_GetObjectItem(package_json, "version");

    printf("[%c] %s %s\n",
           package_installed ? '*' : ' ',
           package_name,
           cJSON_IsString(version) ? version->valuestring : "(unknown)");
    printf("    Desc : %s\n",
           cJSON_IsString(description) ? description->valuestring : "(no description)");

    cJSON_Delete(package_json);

    return 0;
}
