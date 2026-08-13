/*
 * lock.c - advisory locking to prevent two instances of npkg from
 * running concurrently.
*/

#define _GNU_SOURCE

#include "common/lock.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include "common/log.h"
#include "config.h"

static int g_lock_fd = -1;

int acquire_lock(void)
{
    g_lock_fd = open(NPKG_LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (g_lock_fd < 0)
    {
        ERR("failed to open lock file %s: %s\n", NPKG_LOCK_PATH, strerror(errno));
        return 1;
    }

    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) != 0)
    {
        if (errno == EWOULDBLOCK)
            ERR("another npkg instance appears to be running\n");
        else
            ERR("failed to lock %s: %s\n", NPKG_LOCK_PATH, strerror(errno));

        close(g_lock_fd);
        g_lock_fd = -1;
        return 1;
    }

    return 0;
}

void release_lock(void)
{
    if (g_lock_fd >= 0)
    {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
    }
}
