#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

#include "config.h"

#define PATH_MAX 512

int package_search(char *package_name)
{
    char package_path[PATH_MAX + 16] = {0};
    snprintf(package_path, sizeof(package_path), "%s%s.json", PACKAGES_JSON, package_name);

    struct stat st;
    if (stat(package_path, &st) != 0)
    {
        fprintf(stderr, "npkg: package '%s' not found in %s\n", package_name, PACKAGES_JSON);
        return 1;
    }

    int is_installed = 0;
    char installed_path[PATH_MAX + 16] = {0};
    snprintf(installed_path,
             sizeof(installed_path),
             "%s%s.json",
             INSTALLED_DIR, package_name);

    if (stat(installed_path, &st) == 0) is_installed = 1;

    FILE *package_file = fopen(package_path, "r");
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

    cJSON *description = cJSON_GetObjectItem(package_json, "description");
    cJSON *version     = cJSON_GetObjectItem(package_json, "version");

    printf("[%c] %s %s\n",
           is_installed ? '*' : ' ',
           package_name,
           cJSON_IsString(version) ? version->valuestring : "(unknown)");
    printf("    Desc : %s\n",
           cJSON_IsString(description) ? description->valuestring : "(no description)");

    cJSON_Delete(package_json);

    return 0;
}
