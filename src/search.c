#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <errno.h>

#include "config.h"

#include "common/log.h"
#include "common/validate.h"
#include "common/paths.h"
#include "common/manifest.h"
#include "common/env.h"

int package_search(const char *package_name)
{
    if (!is_valid_package_name(package_name))
    {
        ERR("invalid package name '%s'\n", package_name ? package_name : "(null)");
        return 1;
    }

    char installed_package_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(installed_package_path, sizeof(installed_package_path), "%s%s.json", PACKAGES_JSON, package_name);

    struct stat st;
    if (stat(installed_package_path, &st) != 0)
    {
        ERR("package '%s' not found in %s\n", package_name, PACKAGES_JSON);
        return 1;
    }

    char user_destdir_buf[NPKG_PATH_MAX] = {0};
    int have_user_destdir = 0;
    {
        const char *ud = getenv("DESTDIR");
        if (ud && ud[0] != '\0')
        {
            snprintf(user_destdir_buf, sizeof(user_destdir_buf), "%s", ud);
            have_user_destdir = 1;
        }
    }

    if (manifest_set_final_root(have_user_destdir ? user_destdir_buf : NULL) != 0)
    {
        ERR("failed to set final root\n");
        return 1;
    }

    if (have_user_destdir)
    {
        STATUS("using DESTDIR='%s' as the search root\n", manifest_get_final_root());
    }

    char package_path[NPKG_PATH_MAX] = {0};
    const char *root = manifest_get_final_root();

    snprintf(
        package_path,
        sizeof(package_path),
        "%s%s%s.json",
        root,
        root[strlen(root) - 1] == '/'
            ? PACKAGES_JSON + 1
            : PACKAGES_JSON,
        package_name
    );

    FILE *package_file = fopen(package_path, "r");
    if (!package_file)
    {
        ERR("failed to open %s: %s\n", package_path, strerror(errno));
        return 1;
    }

    if (!fd_is_trusted_root_file(fileno(package_file)))
    {
        ERR("refusing to use %s: must be owned by root and not writable by group/other\n", package_path);
        fclose(package_file);
        return 1;
    }

    if (fseek(package_file, 0, SEEK_END) != 0)
    {
        ERR("failed to seek %s: %s\n", package_path, strerror(errno));
        fclose(package_file);
        return 1;
    }

    long size = ftell(package_file);

    if (size < 0)
    {
        ERR("failed to determine size of %s: %s\n", package_path, strerror(errno));
        fclose(package_file);
        return 1;
    }

    if (fseek(package_file, 0, SEEK_SET) != 0)
    {
        ERR("failed to seek %s: %s\n", package_path, strerror(errno));
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

    size_t nread = fread(buffer, 1, (size_t)size, package_file);
    fclose(package_file);

    if (nread != (size_t)size)
    {
        ERR("failed to read %s (short read)\n", package_path);
        free(buffer);
        return 1;
    }

    buffer[size] = '\0';

    cJSON *package_json = cJSON_Parse(buffer);
    free(buffer);

    if (!package_json)
    {
        ERR("failed to parse package.json!\n");
        return 1;
    }

    int package_installed = 0;

    if (is_installed(package_name))
        package_installed = 1;

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
