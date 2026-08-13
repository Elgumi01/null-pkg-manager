
#ifndef NPKG_COMMON_ENV_H
#define NPKG_COMMON_ENV_H

/*
 * Returns 1 if the already-open file descriptor 'fd' refers to a file
 * owned by root and not writable by group/other. Safe to trust.
*/

int fd_is_trusted_root_file(int fd);

/*
 * Strips every known-dangerous environment variable (LD_PRELOAD and
 * friends) and resets PATH / PKG_CONFIG_PATH to safe, known values.
 * Returns 0 on success, 1 on failure.
*/

int sanitize_environment(void);

/*
 * make.conf parsing.
 *
 * Parses KEY=VALUE lines from 'make_conf_path' (blank lines and lines
 * starting with '#' are skipped) and applies them via setenv() for
 * every allow-listed key. 'make_conf_path' must be owned by root and
 * not writable by group/other. Because the variables are set on this
 * process, they remain visible for every system() call made later.
 *
 * Returns 0 on success, 1 on failure.
*/

int set_env(char *make_conf_path);

#endif /* NPKG_COMMON_ENV_H */

