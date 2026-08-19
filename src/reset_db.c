#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

#include "common/log.h"

int reset_database(void)
{
    if (geteuid() != 0)
    {
        ERR("this command must be run as root\n");
        return 1;
    }

    while (1)
    {
        char input[64] = {0};

        STATUS("You are about to delete EVERY <package>.json on %s. Are you sure? [y/N] ", PACKAGES_JSON);
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

    DIR *dir = opendir(PACKAGES_JSON);

    if (!dir)
    {
        ERR("failed to open %s: %s\n", PACKAGES_JSON, strerror(errno));
        return 1;
    }
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char file[512];

        snprintf(
            file,
            sizeof(file),
            "%s%s",
            PACKAGES_JSON,
            entry->d_name
        );

        if (unlink(file) != 0)
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
