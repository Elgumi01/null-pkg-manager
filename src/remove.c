#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include <sys/stat.h>

#include "config.h"

#define PATH_MAX 512
#define COMMAND_MAX 512

int package_remove(char *package_name)
{
    if (geteuid() != 0)
    {
        fprintf(stderr, "npkg: this command must be run as root\n");
        return 1;
    }

    char manifest_path[PATH_MAX + 16] = {0};
    snprintf(manifest_path, sizeof(manifest_path), "%s%s.json", INSTALLED_DIR, package_name);

    struct stat st;
    if (stat(manifest_path, &st) != 0)
    {
        fprintf(stderr, "npkg: package '%s' is not installed\n", package_name);
        return 1;
    }

    FILE *package_file = fopen(manifest_path, "r");
    if (!package_file)
    {
        fprintf(stderr, "=> Failed to open %s%s.json", PACKAGES_JSON, package_name);
        return 1;
    }

    fseek(package_file, 0, SEEK_END);
    long size = ftell(package_file);
    fseek(package_file, 0, SEEK_SET);

    char *buffer = malloc(size + 1);

    if (!buffer)
    {
        fprintf(stderr, "=> Failed to allocate memory for buffer!\n");
        fclose(package_file);
        return 1;
    }

    fread(buffer, 1, size, package_file);
    buffer[size] = '\0';

    cJSON *package_json = cJSON_Parse(buffer);

    free(buffer);

    if (!package_json)
    {
        fprintf(stderr, "=> Failed to parse package.json!\n");
        return 1;
    }

    printf("=> Removing %s...\n", package_name);

    cJSON *file = {0};
    cJSON_ArrayForEach(file, cJSON_GetObjectItem(package_json, "files"))
    {
        char *path = cJSON_GetStringValue(file);

        if (unlink(path) != 0)
        {
            fprintf(stderr, "=> Failed to remove %s\n", path);
            return 1;
        }
        else
        {
            printf("  removed %s\n", path);
        }

    }

    cJSON_Delete(package_json);
    if (remove(manifest_path) != 0)
    {
        fprintf(stderr, "=> Failed to remove package manifest %s\n", manifest_path);
        return 1;
    }

    printf("=> Removed %s successfully!\n", package_name);

    return 0;
}
