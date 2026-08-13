/*
 * env.c - environment sanitization, make.conf parsing, and the
 * "trusted root file" fd check shared across npkg.
*/

#define _GNU_SOURCE

#include "common/env.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/log.h"
#include "config.h"

/* Deny list for every dangerous env var so are not cleaned */

static const char *DANGEROUS_ENV_VARS[] = {
    "LD_PRELOAD", "LD_LIBRARY_PATH", "LD_AUDIT", "LD_ORIGIN_PATH",
    "LD_PROFILE", "LD_SHOW_AUXV", "LD_USE_LOAD_BIAS", "LD_DEBUG",
    "LD_DEBUG_OUTPUT", "LD_DYNAMIC_WEAK", "LD_HWCAP_MASK",
    "GCONV_PATH", "GETCONF_DIR", "NLSPATH", "LOCPATH",
    "BASH_ENV", "ENV", "IFS", "SHELLOPTS",
    "SHELL", "MAKEFLAGS",
    "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
    "LIBRARY_PATH", "LD_RUN_PATH", "PKG_CONFIG_PATH",
    "GCC_EXEC_PREFIX", "COMPILER_PATH",
    "PERL5LIB", "PERL5OPT", "PYTHONPATH", "PYTHONSTARTUP",
    "RUBYLIB", "RUBYOPT", "NODE_OPTIONS", "NODE_PATH",
    "TMPDIR", /* build scripts sometimes trust */
    NULL
};

static const char *ALLOWED_MAKE_CONF_KEYS[] = {
    "MAKEOPTS", "CFLAGS", "CXXFLAGS", "PREFIX",
    "XORG_PREFIX", "XORG_CONFIG", "CC",
    NULL
};

static int is_allowed_make_conf_key(const char *key)
{
    for (int i = 0; ALLOWED_MAKE_CONF_KEYS[i]; i++)
    {
        if (strcmp(key, ALLOWED_MAKE_CONF_KEYS[i]) == 0) return 1;
    }
    return 0;
}

/*
 * Returns 1 if the already-open file descriptor 'fd' refers to a file
 * owned by root and not writable by group/other. Safe to trust.
 */

int fd_is_trusted_root_file(int fd)
{
    struct stat st;
    if (fstat(fd, &st) != 0) return 0;
    if (st.st_uid != 0) return 0;
    if (st.st_mode & (S_IWGRP | S_IWOTH)) return 0;
    return 1;
}

int sanitize_environment(void)
{
    for (int i = 0; DANGEROUS_ENV_VARS[i]; i++)
        unsetenv(DANGEROUS_ENV_VARS[i]);

    if (setenv("PATH", NPKG_SAFE_PATH, 1) != 0) return 1;
    if (setenv("PKG_CONFIG_PATH", NPKG_SAFE_PKG_CONFIG_PATH, 1) != 0) return 1;

    if (!getenv("HOME")) setenv("HOME", "/root", 1);
    if (!getenv("TERM")) setenv("TERM", "dumb", 1);

    return 0;
}

/*
 * make.conf parsing.
 *
 * Parse KEY=VALUE lines from 'make_conf_path' (blank lines and lines
 * starting with '#' are skipped and apply via setenv(). Because the
 * variables are set on this process). they remain visible for every
 * system() call made later.
*/

int set_env(char *make_conf_path)
{
    FILE *f_make_conf = fopen(make_conf_path, "r");
    if (!f_make_conf)
    {
        ERR("failed to read %s: %s\n", make_conf_path, strerror(errno));
        return 1;
    }

    struct stat st;
    if (fstat(fileno(f_make_conf), &st) != 0)
    {
        ERR("failed to stat %s: %s\n", make_conf_path, strerror(errno));
        fclose(f_make_conf);
        return 1;
    }

    if (st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)))
    {
        ERR("refusing to use %s: must be owned by root and not writable by group/other\n", make_conf_path);
        fclose(f_make_conf);
        return 1;
    }

    char line[1024] = {0};

    while (fgets(line, sizeof(line), f_make_conf) != NULL)
    {
        line[strcspn(line, "\n")] = '\0';

        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');

        if (!eq) continue;

        *eq = '\0';

        char *key = line;
        char *value = eq + 1;

        if (key[0] == '\0')
        {
            ERR("invalid assigment (empty key) in %s\n", make_conf_path);

            continue;
        }

        size_t value_len = strlen(value);
        if (value_len >= 2 && value[0] == '"' && value[strlen(value) - 1] == '"')
        {
            value[strlen(value) - 1] = '\0';
            value++;
        }

        if (!is_allowed_make_conf_key(key))
        {
            ERR("ignoring unrecognized key '%s' in %s\n", key, make_conf_path);
            continue;
        }

        if (setenv(key, value, 1) == -1)
        {
            ERR("failed to set %s\n", key);
            fclose(f_make_conf);
            return 1;
        }
    }

    fclose(f_make_conf);
    return 0;
}
