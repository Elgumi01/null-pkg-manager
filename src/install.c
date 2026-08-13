/*
 * install.c - package installation pipeline for npkg
 *
 * Reads a package definition from PACKAGES_JSON/<name>.json, resolves and
 * installs its dependencies, then runs the package's download/build/install
 * steps into a staging directory (DESTDIR) before copying the result onto
 * the live filesystem and recording a manifest under INSTALLED_DIR.
*/

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "common/env.h"
#include "common/lock.h"
#include "common/log.h"
#include "common/manifest.h"
#include "common/paths.h"
#include "common/proc.h"
#include "common/validate.h"
#include "config.h"

#define MAX_CHAIN_DEPTH 32

static void package_build_dir_path(char *out, size_t out_size, const char *package_name)
{
    snprintf(out, out_size, "%s%s/", BUILD_DIR, package_name);
}

static int remove_path(const char *package_name)
{
    char path[NPKG_PATH_MAX] = {0};
    snprintf(path, sizeof(path), "%s%s", BUILD_DIR, package_name);
    return remove_dir(path);
}

/*
 * Before copying the stage tree onto the live filesystem, walk every
 * entry under 'stage_dir' and, for any path whose type (regular file,
 * directory, or symlink) differs from what's already at the same path
 * on disk, remove the existing entry — otherwise 'cp -a' refuses to
 * overwrite a directory with a symlink (or vice versa).
*/

/* Builds the absolute path to the package build directory.  */

static int reconcile_types(const char *stage_dir)
{
    int pipefd[2] = {0};
    if (pipe2(pipefd, O_CLOEXEC) != 0)
    {
        ERR("failed to create pipe for 'find': %s\n", strerror(errno));
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        ERR("failed to fork for 'find': %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /* no '-type f' this time: we need dirs and symlinks too */
        char *argv[] = { "find", (char *)stage_dir, "-mindepth", "1", NULL };
        execvp("find", argv);
        _exit(127);
    }

    close(pipefd[1]);

    FILE *entries = fdopen(pipefd[0], "r");
    if (!entries)
    {
        ERR("failed t:o read output of 'find': %s\n", strerror(errno));
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return 1;
    }

    size_t stage_len = strlen(stage_dir);
    char line[NPKG_PATH_MAX] = {0};
    int had_error = 0;

    while (fgets(line, sizeof(line), entries))
    {
        line[strcspn(line, "\n")] = '\0';

        if (strlen(line) < stage_len) continue;

        char dest_path[NPKG_PATH_MAX + NPKG_PATH_SLACK] = {0};
        snprintf(dest_path, sizeof(dest_path), "/%s", line + stage_len);

        struct stat stage_st, dest_st;

        /* lstat, not stat: we care about the entry itself, not what a
         * symlink points to */
        if (lstat(line, &stage_st) != 0) continue;
        if (lstat(dest_path, &dest_st) != 0) continue; /* nothing there yet, no conflict */

        int stage_is_dir = S_ISDIR(stage_st.st_mode);
        int dest_is_dir  = S_ISDIR(dest_st.st_mode);
        int stage_is_lnk = S_ISLNK(stage_st.st_mode);
        int dest_is_lnk  = S_ISLNK(dest_st.st_mode);

        if (stage_is_dir != dest_is_dir || stage_is_lnk != dest_is_lnk)
        {
            int depth = 0;
            for (const char *c = dest_path; *c; c++)
                if (*c == '/') depth++;

            if (depth <= 1 && getenv("NPKG_ALLOW_UNSAFE_RECONCILE") == NULL)
            {
                ERR("refusing to remove top-level path %s due to a type conflict (set NPKG_ALLOW_UNSAFE_RECONCILE=1 to override)\n",
                    dest_path);
                had_error = 1;
                break;
            }

            STATUS("resolving type conflict at %s...\n", dest_path);
            if (remove_dir(dest_path) != 0)
            {
                ERR("failed to remove conflicting path %s\n", dest_path);
                had_error = 1;
                break;
            }
        }
    }

    fclose(entries);
    waitpid(pid, NULL, 0);

    return had_error;
}

/*
 * List every singular file into 'stage_dir' (equivalent to
 * 'find stage_dir -type -f'), returned as a cJSON array.
*/

static cJSON *collect_installed_files(char *stage_dir)
{
    cJSON *files = cJSON_CreateArray();

    int pipefd[2] = {0};
    if (pipe2(pipefd, O_CLOEXEC) != 0)
    {
        ERR("failed to create pipe for 'find': %s\n", strerror(errno));
        return files;
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        ERR("failed to fork failed for 'find': %s\n", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return files;
    }

    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        char *argv[] = { "find", stage_dir, "(", "-type", "f", "-o", "-type", "l", ")", NULL };
        execvp("find", argv);
        _exit(127);
    }

    close(pipefd[1]);

    FILE *commands = fdopen(pipefd[0], "r");
    if (!commands)
    {
        ERR("failed to read output of 'find': %s\n", strerror(errno));
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return files;
    }

    size_t stage_len = strlen(stage_dir);
    char line[NPKG_PATH_MAX] = {0};

    while (fgets(line, sizeof(line), commands))
    {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) >= stage_len)
            cJSON_AddItemToArray(files, cJSON_CreateString(line + stage_len));
    }

    fclose(commands);
    waitpid(pid, NULL, 0);

    return files;
}

/*
 * Run every command listed on a step by step from package's .json.
 *
 * Unlike the helpers above, this intentionally uses system().
 * The commands here are part of the own package's .json, not
 * externally path fragments.
 *
 * Returns 0 if every command exit sucessfully, 1 otherwise.
*/

static int run_commands(cJSON *array)
{
    cJSON *command = {0};

    cJSON_ArrayForEach(command, array)
    {
        const char *exec = cJSON_GetStringValue(command);

        if (!exec)
        {
            ERR("invalid command entry in package definition\n");
            return 1;
        }

        int status = system(exec);

        if (status == -1) {
            ERR("failed to execute command: %s (%s)\n", exec, strerror(errno));
            return 1;
        }

        if (WIFSIGNALED(status))
        {
            ERR("command killed by signal %d: %s\n", WTERMSIG(status), exec);
            return 1;
        }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            ERR("command failed (exit %d): %s\n", rc, exec);
            return 1;
        }
    }

    return 0;
}

static int run_step(cJSON *package_json, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(package_json, key);

    if (!item)
    {
        STATUS("no '%s' steps defined for this package\n", key);
        return 0;
    }

    if (!cJSON_IsArray(item))
    {
        ERR("'%s' field must be a JSON array in the package definition\n", key);
        return 1;
    }

    return run_commands(item);
}

/*
 * Dependency handling
*/


static int install_recursive(const char *package_name, const char **chain, int depth);

/* Return 1 if 'package_name' already appears in the curretn dependency chain */

static int in_chain(const char **chain, int depth, const char *package_name)
{
    for (int i = 0; i < depth; i++)
    {
        if (strcmp(chain[i], package_name) == 0)
            return 1;
    }

    return 0;
}

static int install_dependencies(cJSON *dependencies, const char **chain, int depth)
{
    cJSON *dependency = {0};

    cJSON_ArrayForEach(dependency, dependencies)
    {
        char *dep_name = cJSON_GetStringValue(dependency);

        if (!dep_name || !is_valid_package_name(dep_name))
        {
            ERR("invalid dependency name in package definition\n");
            return 1;
        }

        if (is_installed(dep_name))
        {
            continue;
        }

        STATUS("'%s' requires '%s', installing it first...\n", chain[depth - 1], dep_name);

        /* no fprintf because we handle it on the function */
        if (install_recursive(dep_name, chain, depth) != 0)
        {
            return 1;
        }
    }

    return 0;
}

/* Restore the working directory saved before entering a package's
 * build.
*/

static void restore_cwd(const char *original_cwd)
{
    if (chdir(original_cwd) != 0)
        ERR("failed to restore working directory to %s: %s\n", original_cwd, strerror(errno));
}


/*
 * Main install pipeline.
 *
 * Installs 'package_name' and its dependencies.
 *
 * 'chain' tracks the current dependency path (cycle preventing) and
 * must have room for at least MAX_CHAIN_DEPTH entries; 'depth' is
 * the current position on it.
*/

static int install_recursive(const char *package_name, const char **chain, int depth)
{
    if (!is_valid_package_name(package_name))
    {
        ERR("invalid package name '%s'\n", package_name ? package_name : "(null)");
        return 1;
    }

    if (depth >= MAX_CHAIN_DEPTH)
    {
        ERR("dependency chain too deep (possible cycle with '%s')\n", package_name);
        return 1;
    }

    if (in_chain(chain, depth, package_name))
    {
        ERR("circular dependency detected: '%s' depends on itself\n", package_name);
        return 1;
    }

    if (is_installed(package_name))
    {
        STATUS("'%s' already installed at final system\n", package_name);
        return 0;
    }

    chain[depth] = package_name;

    char original_cwd[512] = {0};
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        ERR("failed to get current directory\n");
        return 1;
    }

    char package_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(package_path,
             sizeof(package_path),
             "%s%s.json", PACKAGES_JSON, package_name);

    struct stat st;
    if (stat(package_path, &st) != 0)
    {
        ERR("package '%s' not found in %s\n", package_name, PACKAGES_JSON);
        return 1;
    }

    STATUS("reading package.json for '%s'...\n", package_name);

    FILE *package_file = fopen(package_path, "rb");
    if (!package_file)
    {
        ERR("failed to open %s!\n", package_path);
        return 1;
    }

    if (!fd_is_trusted_root_file(fileno(package_file)))
    {
        ERR("refusing to use package definition %s: must be owned by root and not writable by group/other\n", package_path);
        fclose(package_file);
        return 1;
    }

    if (fseek(package_file, 0, SEEK_END) != 0)
    {
        ERR("failed to seek %s: %s\n", package_path, strerror(errno));
        fclose(package_file);
        return 1;
    }
    long size = ftell(package_file);

    if (size < 0)
    {
        ERR("failed to determine size of %s: %s\n", package_path, strerror(errno));
        fclose(package_file);
        return 1;
    }

    if (fseek(package_file, 0, SEEK_SET) != 0)
    {
        ERR("failed to seek %s: %s\n", package_path, strerror(errno));
        fclose(package_file);
        return 1;
    }

    if (size < 0)
    {
        ERR("failed to determine size of %s\n", package_name);
        fclose(package_file);
        return 1;
    }

    char *raw = malloc(size + 1);
    if (!raw)
    {
        ERR("out of memory reading %s\n", package_path);
        fclose(package_file);
        return 1;
    }

    size_t nread = fread(raw, 1, size, package_file);
    fclose(package_file);

    if (nread != (size_t)size)
    {
        ERR("failed to read %s\n", package_path);
        free(raw);
        return 1;
    }

    raw[size] = '\0';

    cJSON *package_json = cJSON_Parse(raw);
    free(raw);

    if (!package_json)
    {
        ERR("failed to parse %s (%s)\n", package_path, cJSON_GetErrorPtr());
        return 1;
    }

    if (install_dependencies(cJSON_GetObjectItem(package_json, "dependencies"), chain, depth + 1) != 0)
    {
        cJSON_Delete(package_json);
        return 1;
    }

    /* Every package gets its own build directory */

    char package_build_dir[NPKG_PATH_MAX] = {0};
    package_build_dir_path(package_build_dir, sizeof(package_build_dir), package_name);

    STATUS("entering build dir (%s)...\n", package_build_dir);

    /* Remove possible leftovers */

    if (remove_path(package_name) != 0)
    {
        ERR("failed to clean up leftovers for %s\n", package_name);
        cJSON_Delete(package_json);
        return 1;
    }

    if (mkdir_p(package_build_dir) != 0)
    {
        ERR("failed to create %s\n", package_build_dir);
        cJSON_Delete(package_json);
        return 1;
    }

    if (chdir(package_build_dir) != 0)
    {
        ERR("failed to enter %s\n", package_build_dir);
        cJSON_Delete(package_json);
        return 1;
    }

    /* download / build / install steps */

    STATUS("downloading %s...\n", package_name);

    if (run_step(package_json, "download") != 0)
    {
        cJSON_Delete(package_json);
        restore_cwd(original_cwd);
        return 1;
    }

    STATUS("building %s...\n", package_name);

    if (run_step(package_json, "build") != 0)
    {
        cJSON_Delete(package_json);
        restore_cwd(original_cwd);
        return 1;
    }

    STATUS("creating stagging directory...\n");

    char stage_dir[NPKG_PATH_MAX + NPKG_PATH_SLACK] = {0};
    snprintf(stage_dir, sizeof(stage_dir), "%sstage-%s", package_build_dir, package_name);
    if (mkdir(stage_dir, 0755) != 0 && errno != EEXIST)
    {
        ERR("failed to create %s\n", stage_dir);
        cJSON_Delete(package_json);
        restore_cwd(original_cwd);
        return 1;
    }
    setenv("DESTDIR", stage_dir, 1);

    STATUS("installing %s...\n", package_name);

    if (run_step(package_json, "install") != 0)
    {
        cJSON_Delete(package_json);
        restore_cwd(original_cwd);
        return 1;
    }

    /* stage -> live filesystem, then make package .json manifest  */

    STATUS("collecting installed files...\n");

    cJSON *installed_files = collect_installed_files(stage_dir);

    STATUS("resolving type conflicts...\n");

    if (reconcile_types(stage_dir) != 0)
    {
        ERR("failed to reconcile file types for %s\n", package_name);
        cJSON_Delete(installed_files);
        cJSON_Delete(package_json);
        restore_cwd(original_cwd);
        return 1;
    }

    const char *final_root = manifest_get_final_root();

    STATUS("installing files to %s ...\n", final_root);

    char stage_src[sizeof(stage_dir) + NPKG_PATH_SLACK] = {0};
    snprintf(stage_src, sizeof(stage_src), "%s/.", stage_dir);

    if (mkdir_p(final_root) != 0)
    {
        ERR("failed to create final root %s\n", final_root);
        cJSON_Delete(installed_files);
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    if (copy_tree(stage_src, final_root) != 0)
    {
        ERR("failed to copy staged files for %s\n", package_name);
        cJSON_Delete(installed_files);
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    STATUS("writing manifest...\n");

    char *version = cJSON_GetStringValue(cJSON_GetObjectItem(package_json, "version"));
    if (write_manifest(package_name, version, installed_files) != 0)
    {
        ERR("'%s' files were installed but the manifest could not be written, the package will NOT be tracked correctly\n", package_name);
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    STATUS("cleaning up...\n");

    if (remove_dir(stage_dir) != 0)
        ERR("failed to remove stagging directory");

    cJSON_Delete(package_json);
    chdir(original_cwd);

    STATUS("'%s' installed successfully.\n", package_name);

    return 0;
}

/* Entry */

int package_install(const char *package_name)
{
    if (geteuid() != 0)
    {
        ERR("this command must be run as root\n");
        return 1;
    }

    if (!is_valid_package_name(package_name))
    {
        ERR("invalid package name '%s'\n", package_name ? package_name : "(null)");
        return 1;
    }

    char user_destdir_buf[NPKG_PATH_MAX] = {0};
    int have_user_destdir = 0;
    {
        const char *ud = getenv("DESTDIR");
        if (ud && ud[0] != '\0')
        {
            snprintf(user_destdir_buf, sizeof(user_destdir_buf), "%s", ud);
            have_user_destdir = 1;
        }
    }

    if (sanitize_environment() != 0)
    {
        ERR("failed to sanitize environment\n");
        return 1;
    }

    if (acquire_lock() != 0) return 1;

    int rc = 0;

    if (manifest_set_final_root(have_user_destdir ? user_destdir_buf : NULL) != 0)
    {
        ERR("failed to set final root\n");
        rc = 1;
        goto out;
    }

    if (have_user_destdir)
    {
        STATUS("using DESTDIR='%s' as the final install root\n", manifest_get_final_root());
    }

    struct stat st;
    if (stat(MAKE_CONF, &st) == 0)
    {
        if (set_env(MAKE_CONF) != 0)
        {
            ERR("failed to set make.conf\n");
            rc = 1;
            goto out;
        }
    }

    const char *chain[MAX_CHAIN_DEPTH] = {0};
    rc = install_recursive(package_name, chain, 0);

out:
    if (have_user_destdir)
        setenv("DESTDIR", user_destdir_buf, 1);
    else
        unsetenv("DESTDIR");

    release_lock();
    return rc;
}
