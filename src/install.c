/*
 * install.c - package installation pipeline for npkg
 *
 * Reads a package definition from PACKAGES_JSON/<name>.json, resolves and
 * installs its dependencies, then runs the package's download/build/install
 * steps into a staging directory (DESTDIR) before copying the result onto
 * the live filesystem and recording a manifest under INSTALLED_DIR.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "config.h"

#define COMMAND_MAX     1024
#define NPKG_PATH_MAX   1024   /* avoid clashing with <limits.h> */
#define MAX_CHAIN_DEPTH 32

#define ERR(...) fprintf(stderr, "npkg: " __VA_ARGS__)
#define STATUS(...) printf("=> " __VA_ARGS__)

/*
 * Run argv[0] with the given commands (without shell).
 * Returns the child's error code, or -1 on failures/child
 * was killed by a signal.
*/

static int exec_argv(char *const *argv)
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

static int remove_path(const char *path)
{
    char *argv[] = { "rm", "-rf", (char *)path, NULL };
    return exec_argv(argv) == 0 ? 0 : 1;
}

/* Recursively copy 'src' into 'dst' (equivalent to 'cp -a src dst', shell-free).  */

static int copy_tree(const char *src, const char *dst)
{
    char *argv[] = { "cp", "-a", (char *)src, (char *)dst, NULL };
    return exec_argv(argv) == 0 ? 0 : 1;
}

/*
 * List every singular file into 'stage_dir' (equivalent to
 * 'find stage_dir -type -f'), returned as a cJSON array.
*/

static cJSON *collect_installed_files(char *stage_dir)
{
    cJSON *files = cJSON_CreateArray();

    int pipefd[2] = {0};
    if (pipe(pipefd) != 0)
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

        char *argv[] = { "find", stage_dir, "-type", "f", NULL };
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

    char line[512] = {0};
    while (fgets(line, sizeof(line), commands))
    {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) >= strlen(stage_dir))
            cJSON_AddItemToArray(files, cJSON_CreateString(line + strlen(stage_dir)));
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

        int status = system(exec);

        if (status != 0) {
            ERR("command failed (exit %d): %s\n", status, exec);
            return 1;
        }
    }

    return 0;
}

/* Return 1 if 'package_name' has a manifest under MANIFEST_DIR, 0 otherwise  */

static int is_installed(char *package_name)
{
    char installed_package_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(installed_package_path,
             sizeof(installed_package_path),
             "%s%s.json",
             INSTALLED_DIR, package_name);

    struct stat st;
    return stat(installed_package_path, &st) == 0;
}

/* Write INSTALLED_DIR/<package_name>.json containing the package_name,
 * version and installed packages. Writes it to a .tmp file first so a
 * crash mid-write cant corrupt an existing manifest.
*/

int write_manifest(char *package_name, char *version, cJSON *files)
{
    cJSON *manifest = cJSON_CreateObject();
    cJSON_AddStringToObject(manifest, "name", package_name);
    cJSON_AddStringToObject(manifest, "version", version ? version : "unknown");
    cJSON_AddItemToObject(manifest, "files", files);

    char manifest_path[NPKG_PATH_MAX + 16] = {0};
    snprintf(manifest_path, sizeof(manifest_path), "%s%s.json", INSTALLED_DIR, package_name);

    char tmp_path[sizeof(manifest_path) + 16] = {0};
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", manifest_path);

    FILE *manifest_file = fopen(tmp_path, "w");
    if (!manifest_file)
    {
        ERR("failed to write manifest for '%s': %s\n", package_name, strerror(errno));
        cJSON_Delete(manifest);
        return 1;
    }

    char *json_str = cJSON_Print(manifest);
    fputs(json_str, manifest_file);
    fclose(manifest_file);
    free(json_str);
    cJSON_Delete(manifest);
    if (rename(tmp_path, manifest_path) != 0)
        ERR("failed to finalize manifest for '%s': %s\n", package_name, strerror(errno));

    return 0;
}

/*
 * Dependency handling
*/


static int install_recursive(char *package_name, char **chain, int depth);

/* Return 1 if 'package_name' already appears in the curretn dependency chain */

int in_chain(char **chain, int depth, char *package_name)
{
    for (int i = 0; i < depth; i++)
    {
        if (strcmp(chain[i], package_name) == 0) return 1;
    }

    return 0;
}

int install_dependencies(cJSON *dependencies, char **chain, int depth)
{
    cJSON *dependency = {0};

    cJSON_ArrayForEach(dependency, dependencies)
    {
        char *dep_name = cJSON_GetStringValue(dependency);

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

/*
 * Main install pipeline.
 *
 * Installs 'package_name' and its dependencies.
 *
 * 'chain' tracks the current dependency path (cycle preventing) and
 * must have room for at least MAX_CHAIN_DEPTH entries; 'depth' is
 * the current position on it.
*/

static int install_recursive(char *package_name, char **chain, int depth)
{

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
        STATUS("'%s' already installed\n", package_name);
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

    fseek(package_file, 0, SEEK_END);
    long size = ftell(package_file);
    fseek(package_file, 0, SEEK_SET);

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

    fread(raw, 1, size, package_file);
    raw[size] = '\0';
    fclose(package_file);

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

    printf("=> Entering build dir (%s)...\n", BUILD_DIR);

    if (mkdir(BUILD_DIR, 0755) != 0 && errno != EEXIST)
    {
        ERR("failed to create %s\n", BUILD_DIR);
        cJSON_Delete(package_json);
        return 1;
    }

    if (chdir(BUILD_DIR) != 0)
    {
        ERR("failed to enter %s\n", BUILD_DIR);
        cJSON_Delete(package_json);
        return 1;
    }

    /* clear out possibly leftovers from a failed build */

    remove_path(package_name);

    /* download / build / install steps */

    STATUS("downloading %s...\n", package_name);

    if (run_commands(cJSON_GetObjectItem(package_json, "download")) != 0)
    {
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    STATUS("building %s...\n", package_name);


    if (run_commands(cJSON_GetObjectItem(package_json, "build")) != 0)
    {
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    STATUS("creating stagging directory...\n");

    char stage_dir[512] = {0};
    snprintf(stage_dir, sizeof(stage_dir), "%sstage-%s", BUILD_DIR, package_name);
    if (mkdir(stage_dir, 0755) != 0 && errno != EEXIST)
    {
        ERR("failed to create %s\n", stage_dir);
        cJSON_Delete(package_json);
        return 1;
    }
    setenv("DESTDIR", stage_dir, 1);

    STATUS("installing %s...\n", package_name);

    if (run_commands(cJSON_GetObjectItem(package_json, "install")) != 0)
    {
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    /* stage -> live filesystem, then make package .json manifest  */

    STATUS("collecting installed files...\n");

    cJSON *installed_files = collect_installed_files(stage_dir);

    STATUS("installing files to / ...\n");

    char stage_src[1024] = {0};
    snprintf(stage_src, sizeof(stage_src), "%s/.", stage_dir);

    if (copy_tree(stage_src, "/") != 0)
    {
        ERR("failed to copy staged files for %s\n", package_name);
        cJSON_Delete(installed_files);
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    printf("=> Writing manifest...\n");

    char *version = cJSON_GetStringValue(cJSON_GetObjectItem(package_json, "version"));
    write_manifest(package_name, version, installed_files);

    STATUS("cleaning up...\n");

    remove_path(stage_dir);

    cJSON_Delete(package_json);
    chdir(original_cwd);

    STATUS("'%s' installed successfully.\n", package_name);

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

static int set_env(char *make_conf_path)
{
    FILE *f_make_conf = fopen(make_conf_path, "r");
    if (!f_make_conf)
    {
        ERR("failed to read %s\n", make_conf_path);
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

        if (value[0] == '"' && value[strlen(value) - 1] == '"')
        {
            value++;
            value[strlen(value) - 1] = '\0';
        }

        if (setenv(key, value, 1) == -1)
        {
            ERR("npkg: failed to set %s\n", key);
            fclose(f_make_conf);
            return 1;
        }
    }

    fclose(f_make_conf);

    return 0;
}

int package_install(char *package_name)
{
    if (geteuid() != 0)
    {
        ERR("npkg: this command must be run as root\n");
        return 1;
    }
    struct stat st;
    if (stat(MAKE_CONF, &st) == 0)
    {
        if (set_env(MAKE_CONF) != 0) 
            ERR("npkg: failed to set make.conf");
    }
    char *chain[MAX_CHAIN_DEPTH] = {0};

    return install_recursive(package_name, chain, 0);
}
