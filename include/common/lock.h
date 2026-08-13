#ifndef NPKG_COMMON_LOCK_H
#define NPKG_COMMON_LOCK_H

/*
 * Simple advisory lock over NPKG_LOCK_PATH, used to prevent two
 * instances of npkg from running concurrently.
*/

/* Acquires the lock. Returns 0 on success, 1 on failure (including
 * when another instance already holds it). */

int acquire_lock(void);

/* Releases the lock previously acquired with acquire_lock(). Safe to
 * call even if the lock was never acquired. */

void release_lock(void);

#endif /* NPKG_COMMON_LOCK_H */

