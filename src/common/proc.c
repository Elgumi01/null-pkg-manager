/*
 * proc.c - shell-free process spawning and filesystem tree helpers
 * shared across npkg.
*/

#define _GNU_SOURCE

#include "common/proc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/log.h"
#include "common/paths.h"

/*
 * Run argv[0] with the given commands (without shell).
 * Returns the child's error code, or -1 on failures/child
 * was killed by a signal.
*/

int exec_argv(char *const *argv)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        ERR("failed to fork: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        execvp(argv[0], argv);
        /* only reached if execvp fails */
        ERR("failed to exec '%s': %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        ERR("failed waitpid: %s\n", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
    {
        ERR("'%s' killed by signal %d\n", argv[0], WTERMSIG(status));
        return -1;
    }

    return -1;
}

/* Recursively remove 'path' (equivalent to 'rm -rf path', shell-free). */

int remove_dir(const char *path)
{
    char *argv[] = { "rm", "-rf", (char *)path, NULL };
    return exec_argv(argv) == 0 ? 0 : 1;
}

int copy_tree(const char *src, const char *dst)
{
    char *argv[] = { "cp", "-a", "--remove-destination", (char *)src, (char *)dst, NULL };
    return exec_argv(argv) == 0 ? 0 : 1;
}

int component_is_unsafe_symlink(const char *path)
{
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) return 1;
    return 0;
}

/* Equivalent to 'mkdir -p PATH', refusing to walk through syslinks */

int mkdir_p(const char *path)
{
    char tmp[NPKG_PATH_MAX] = {0};
    snprintf(tmp, sizeof(tmp), "%s", path);

    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    if (tmp[0] == '\0')
    {
        return 0;
    }

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (component_is_unsafe_symlink(tmp))
            {
                ERR("refusing to traverse symlink at %s\n", tmp);
                return 1;
            }

            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return 1;
            *p = '/';
        }
    }

    if (component_is_unsafe_symlink(tmp))
    {
        ERR("refusing to traverse symlink at %s\n", tmp);
        return 1;
    }

    return (mkdir(tmp, 0755) != 0 && errno != EEXIST) ? 1 : 0;
}
