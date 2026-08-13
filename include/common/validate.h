#ifndef NPKG_COMMON_VALIDATE_H
#define NPKG_COMMON_VALIDATE_H

/*
 * Returns 1 if 'name' is a safe package name (no path traversal, no
 * leading '/' or '.', made up only of alphanumerics plus '.', '_',
 * '+', '-', and within the maximum length), 0 otherwise.
*/
int is_valid_package_name(const char *name);

#endif /* NPKG_COMMON_VALIDATE_H */
