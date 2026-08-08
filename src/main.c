#include <stdio.h>
#include <string.h>

#include "install.h"
#include "remove.h"
#include "search.h"
#include "list.h"
#include "reset_db.h"

void print_help(void)
{
    printf("Usage: npkg [OPTIONS]...\n");
    printf("Local and simple package manager\n\n");
    printf("npkg install [PKG_NAME]        Install a package on the system. (Requires root previleges.)\n");
    printf("npkg install [PKG_NAME]        Remvove a package from the system. (Requires root previleges.)\n");
    printf("npkg search  [PKG_NAME]        Search a package, displays [*] if installed [ ] if not.\n");
    printf("npkg --reset-db                Reset the entire database of <packages> .json");
    printf("Examples:\n");
    printf("  npkg install gfetch fastfetch\n");
    printf("  npkg remove sysmon\n");
    printf("  npkg search mesa\n");
}

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        fprintf(stderr, "npkg: no options provided, try 'npkg --help'\n");
        return 1;
    }
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_help();
            return 0;
        }

        else if (strcmp(argv[i], "install") == 0)
        {
            if (argv[i + 1] == NULL)
            {
                fprintf(stderr, "npkg: install requires an argument\n");
            }
            for (int j = i + 1; j < argc; j++)
            {
                package_install(argv[j]);
            }
            return 0;
        }
        else if (strcmp(argv[i], "remove") == 0)
        {
            if (argv[i + 1] == NULL)
            {
                fprintf(stderr, "npkg: remove requires an argument\n");
            }
            for (int j = i + 1; j < argc; j++)
            {
                package_remove(argv[j]);
            }
            return 0;
        }
        else if (strcmp(argv[i], "search") == 0)
        {
            if (argv[i + 1] == NULL)
            {
                fprintf(stderr, "npkg: search requires an argument\n");
            }
            for (int j = i + 1; j < argc; j++)
            {
                package_search(argv[j]);
            }
            return 0;
        }
        else if (strcmp(argv[i], "list") == 0)
        {
            packages_list();
            return 0;
        }
        else if (strcmp(argv[i], "--reset-db") == 0)
        {
            reset_database();
            return 0;
        }

        else
        {
            fprintf(stderr, "npkg: invalid argument '%s'. Try 'npkg --help'.\n", argv[i]);
            return 1;
        }

    }
}
