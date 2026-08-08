/*
 * search - package listing function
 *
 * List every json present on INSTALLED_DIR
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

#include "cjson/cJSON.h"

#include "config.h"

#define NPKG_PATH_MAX    512
#define COMMAND_MAX      512

#define ERR(...) fprintf(stderr, "npkg: " __VA_ARGS__)
#define STATUS(...) printf("=> " __VA_ARGS__)

void packages_list(void)
{
    DIR *dir = opendir(INSTALLED_DIR);

    if(!dir)
    {
        ERR("failed open %s: %s\n", INSTALLED_DIR, strerror(errno));
        return;
    }

    struct dirent *entry;
    int installed = 0;

    printf("Installed packages:\n\n");

    while ((entry = readdir(dir)) != NULL)
    {
        /* d_name validation  */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        size_t len = strlen(entry->d_name);

        if (len < 5 || strcmp(entry->d_name + len - 5, ".json") != 0)
            continue;

        char path[NPKG_PATH_MAX] = {0};

        int written = snprintf(path, sizeof(path),
                              "%s%s", INSTALLED_DIR,
                              entry->d_name);

        if (written < 0 || (size_t)written >= sizeof(path))
        {
            ERR("path too long: %s\n", entry->d_name);
            continue;
        }

        FILE *file = fopen(path, "r");

        if (!file)
        {
            ERR("failed to open %s: %s\n",
                path,
                strerror(errno));
            continue;
        }

        /* Determine file size */

        fseek(file, 0, SEEK_END);

        long size = ftell(file);

        if (size < 0)
        {
            ERR("failed to determine size of %s\n", path);
            fclose(file);
            continue;
        }

        fseek(file, 0, SEEK_SET);

        /* Read the entire JSON file. */

        char *buffer = malloc((size_t)size + 1);

        if (!buffer)
        {
            ERR("memory allocation failed\n");
            fclose(file);
            closedir(dir);
            return;
        }

        size_t read = fread(buffer, 1, (size_t)size, file);
        buffer[read] = '\0';

        fclose(file);

        /* Parse JSON. */

        cJSON *package_json = cJSON_Parse(buffer);

        free(buffer);

        if (!package_json)
        {
            ERR("invalid JSON: %s\n", path);
            continue;
        }

        /* Get package name and version. */

        cJSON *name        = cJSON_GetObjectItem(package_json, "name");
        cJSON *version     = cJSON_GetObjectItem(package_json, "version");

        if (!cJSON_IsString(name) || !cJSON_IsString(version))
        {
            ERR("invalid package manifest: %s\n", path);
            cJSON_Delete(package_json);
            continue;
        }

        printf("%-20s %s\n", cJSON_GetStringValue(name), cJSON_GetStringValue(version));

        installed++;

        cJSON_Delete(package_json);

    }

    closedir(dir);

    printf("\nTotal: %d package%s\n", installed, installed > 1 ? "s" : "");
}
