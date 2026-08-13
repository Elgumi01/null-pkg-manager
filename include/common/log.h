#ifndef NPKG_COMMON_LOG_H
#define NPKG_COMMON_LOG_H

#include <stdio.h>

/*
 * Shared logging macros.
 *
 * ERR()    -> prints to stderr, prefixed with "npkg: "
 * STATUS() -> prints to stdout, prefixed with "=> "
*/

#define ERR(...) fprintf(stderr, "npkg: " __VA_ARGS__)
#define STATUS(...) printf("=> " __VA_ARGS__)

#endif /* NPKG_COMMON_LOG_H */
