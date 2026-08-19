#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "config.h"

#include "common/log.h"
#include "common/paths.h"
#include "common/proc.h"

int clear_cache(void)
{
    if (geteuid() != 0)
    {
        ERR("this command must be run as root\n");
        return 1;
    }

    while (1)
    {
        char input[64] = {0};

        STATUS("You are about to delete EVERY directory on %s. Are you sure? [y/N] ", BUILD_DIR);
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin))
        {
            ERR("failed to read input\n");
            return 1;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "y") == 0 || strcmp(input, "yes") == 0)
            break;
        else if (strcmp(input, "n") == 0 || strcmp(input, "no") == 0 || input[0] == '\0')
            return 1;
        else
        {
            STATUS("invalid answer, please input [y/N]\n");
            continue;
        }
    }

    DIR *dir = opendir(BUILD_DIR);

    if (!dir)
    {
        ERR("failed to open %s: %s\n", BUILD_DIR, strerror(errno));
        return 1;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char file[NPKG_PATH_MAX] = {0};

        int size = snprintf(file,
            sizeof(file),
            "%s%s",
            BUILD_DIR,
            entry->d_name
        );

        if (size < 0 || (size_t)size >= sizeof(file))
        {
            ERR("path too long: %s%s\n", BUILD_DIR, entry->d_name);
            closedir(dir);
            return 1;
        }

        if (remove_dir(file) != 0)
        {
            ERR("failed to remove %s\n", file);
            closedir(dir);
            return 1;
        }

        STATUS("removed %s\n", file);
    }

    closedir(dir);

    return 0;
}
