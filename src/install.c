#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <cjson/cJSON.h>

#include "config.h"

#define COMMAND_MAX 1024
#define PATH_MAX 1024

int run_commands(cJSON *array)
{
    cJSON *command = {0};

    cJSON_ArrayForEach(command, array)
    {
        int exec = system(cJSON_GetStringValue(command));
        if (exec != 0) {
            fprintf(stderr, "=> Command failed (exit %d): %s\n", exec, cJSON_GetStringValue(command));
            return 1;
        }
    }

    return 0;
}

cJSON *collect_installed_files(char *stage_dir)
{
    cJSON *files = cJSON_CreateArray();

    char find_command[COMMAND_MAX + 16] = {0};

    snprintf(find_command,
             sizeof(find_command),
             "find \"%s\" -type f",
             stage_dir);
    FILE *commands = popen(find_command, "r");
    if (!commands)
    {
        fprintf(stderr, "=> Failed executing find!\n");
        return files;
    }

    char line[512] = {0};
    size_t stage_len = strlen(stage_dir);
    while (fgets(line, sizeof(line), commands))
    {
        line[strcspn(line, "\n")] = '\0';
        cJSON_AddItemToArray(files, cJSON_CreateString(line + stage_len));
    }

    pclose(commands);

    return files;
}

int write_manifest(char *package_name, char *version, cJSON *files)
{
    cJSON *manifest = cJSON_CreateObject();
    cJSON_AddStringToObject(manifest, "name", package_name);
    cJSON_AddStringToObject(manifest, "version", version ? version : "unknown");
    cJSON_AddItemToObject(manifest, "files", files);

    char manifest_path[PATH_MAX + 16] = {0};
    snprintf(manifest_path, sizeof(manifest_path), "%s%s.json", INSTALLED_DIR, package_name);

    char tmp_path[sizeof(manifest_path) + 16] = {0};
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", manifest_path);

    FILE *manifest_file = fopen(tmp_path, "w");
    if (!manifest_file)
    {
        fprintf(stderr, "=> Failed to write manifest for %s\n", package_name);
        cJSON_Delete(manifest);
        return 1;
    }

    char *json_str = cJSON_Print(manifest);
    fputs(json_str, manifest_file);
    fclose(manifest_file);
    free(json_str);
    cJSON_Delete(manifest);

    rename(tmp_path, manifest_path);

    return 0;
}

int package_install(char *package_name)
{
    if (geteuid() != 0)
    {
        fprintf(stderr, "npkg: this command must be run as root\n");
        return 1;
    }

    char installed_package_path[PATH_MAX + 16] = {0};
    snprintf(installed_package_path,
             sizeof(installed_package_path),
             "%s%s.json",
             INSTALLED_DIR, package_name);

    struct stat st;
    if (stat(installed_package_path, &st) == 0)
    {
        printf("npkg: package '%s' already installed\n", package_name);
        return 0;
    }

    char original_cwd[512] = {0};
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        fprintf(stderr, "=> Failed to get current directory\n");
        return 1;
    }


    char package_path[PATH_MAX + 16] = {0};

    snprintf(package_path,
             sizeof(package_path),
             "%s%s.json", PACKAGES_JSON, package_name);

    if (stat(package_path, &st) != 0)
    {
        fprintf(stderr, "npkg: package '%s' not found in %s\n", package_name, PACKAGES_JSON);
        return 1;
    }

    printf("=> Reading .json...\n");

    FILE *package_file = fopen(package_path, "rb");
    if (!package_file)
    {
        fprintf(stderr, "=> Failed to open %s!\n", package_path);
        return 1;
    }

    fseek(package_file, 0, SEEK_END);
    long size = ftell(package_file);
    fseek(package_file, 0, SEEK_SET);

    char *raw = malloc(size + 1);
    fread(raw, 1, size, package_file);
    raw[size] = '\0';
    fclose(package_file);

    cJSON *package_json = cJSON_Parse(raw);
    free(raw);

    if (!package_json)
    {
        fprintf(stderr, "=> Failed to parse %s (%s)\n", package_path, cJSON_GetErrorPtr());
        return 1;
    }

    printf("=> Entering build dir (%s)...\n", BUILD_DIR);

    mkdir(BUILD_DIR, 0755);

    if (chdir(BUILD_DIR) != 0)
    {
        fprintf(stderr, "=> Failed to enter %s\n", BUILD_DIR);
        cJSON_Delete(package_json);
        return 1;
    }

    char clean_command[COMMAND_MAX + 16] = {0};
    snprintf(clean_command, sizeof(clean_command), "rm -rf \"%s\"", package_name);
    system(clean_command);

    /* No fprintf's here because the function already handle errors */

    printf("=> Downloading %s...\n", package_name);

    if (run_commands(cJSON_GetObjectItem(package_json, "download")) != 0)
    {
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    printf("=> Building %s...\n", package_name);


    if (run_commands(cJSON_GetObjectItem(package_json, "build")) != 0)
    {
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    printf("=> Creating stage_dir...\n");

    char stage_dir[512] = {0};
    snprintf(stage_dir, sizeof(stage_dir), "%sstage-%s", BUILD_DIR, package_name);
    mkdir(stage_dir, 0755);
    setenv("DESTDIR", stage_dir, 1);

    printf("=> Installing %s...\n", package_name);

    if (run_commands(cJSON_GetObjectItem(package_json, "install")) != 0)
    {
        cJSON_Delete(package_json);
        chdir(original_cwd);
        return 1;
    }

    printf("=> Collecting installed files...\n");

    cJSON *installed_files = collect_installed_files(stage_dir);

    printf("Copying files to / ...\n");

    char install_command[COMMAND_MAX + 16] = {0};
    snprintf(install_command,
             sizeof(install_command),
             "cp -a \"%s/.\" /", stage_dir);
    system(install_command);

    printf("Writing manifest...\n");

    char *version = cJSON_GetStringValue(cJSON_GetObjectItem(package_json, "version"));
    write_manifest(package_name, version, installed_files);

    printf("=> Cleaning up...\n");

    char cleanup_command[COMMAND_MAX + 16] = {0};
    snprintf(cleanup_command, sizeof(cleanup_command), "rm -rf \"%s\"", stage_dir);
    system(cleanup_command);


    cJSON_Delete(package_json);
    chdir(original_cwd);

    printf("=> '%s' installed successfully.\n", package_name);

    return 0;
}
