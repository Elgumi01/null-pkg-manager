/*
 * validate.c - input validation helpers shared across npkg.
*/

#include "common/validate.h"

#include <ctype.h>
#include <string.h>

#define MAX_PACKAGE_NAME_LEN 128

/* Check if the package_name is valid returning
 * 1 if yes, 0 otherwise
*/

int is_valid_package_name(const char *name)
{
    if (!name || name[0] == '\0') return 0;
    if (strstr(name, "..") != NULL) return 0;
    if (name[0] == '/' || name[0] == '.') return 0;

    size_t len = 0;
    for (const char *p = name; *p; p++)
    {
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '_' ||
              *p == '+' || *p == '-'))
        {
            return 0;
        }

        if (++len > MAX_PACKAGE_NAME_LEN) return 0;
    }

    return 1;
}
