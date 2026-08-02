#include <stdio.h>
#include <string.h>

#include "install.h"
#include "remove.h"

void print_help(void)
{
    printf("Usage: npkg [OPTIONS]...\n");
    printf("Local and simple package manager\n\n");
    printf("npkg install [PKG_MANAGER]        Install a package on the system. (Requires sudo.)\n");
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
        else
        {
            fprintf(stderr, "npkg: invalid argument '%s'. Try 'npkg --help'.\n", argv[i]);
            return 1;
        }

    }
}
