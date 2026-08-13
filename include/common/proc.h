#ifndef NPKG_COMMON_PROC_H
#define NPKG_COMMON_PROC_H

/*
 * Run argv[0] with the given arguments (without a shell).
 * Returns the child's exit code, or -1 on failure / if the child
 * was killed by a signal.
*/
int exec_argv(char *const *argv);

/* Recursively remove 'path' (equivalent to 'rm -rf path', shell-free). */
int remove_dir(const char *path);

/* Equivalent to 'cp -a --remove-destination src dst', shell-free. */
int copy_tree(const char *src, const char *dst);

/* Equivalent to 'mkdir -p path', refusing to walk through symlinks. */
int mkdir_p(const char *path);

/* Symlink guard: returns 1 if 'path' exists and is itself a symlink,
 * 0 otherwise. */
int component_is_unsafe_symlink(const char *path);

#endif /* NPKG_COMMON_PROC_H */
