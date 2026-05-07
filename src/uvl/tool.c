
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "uvl/tool.h"
#include "utils/command.h"
#include "utils/logging.h"
#include "utils/constant.h"
#include "utils/types.h"
#include "uvl/config.h"
#include "io/mnt.h"
#include "io/file.h"
#include "io/path.h"
#include "uvl/project.h"

void register_tool(const char *tool, const char *entry) {
    char exe_path[MAX_PATH_LEN];
    if (!resolve_command(tool, exe_path, sizeof(exe_path))) {
        LOG_ERROR("Cannot fuse '%s': binary not found in PATH", tool);
        exit(1);
    }

    Config config;
    load_config(&config);
    Registration *reg = find_registration(&config, tool);
    if (!reg) {
        if (config.len >= MAX_REGISTRATIONS) LOG_ERROR("Too many registrations");
        reg = &config.items[config.len++];
    }
    snprintf(reg->tool, sizeof(reg->tool), "%s", tool);
    snprintf(reg->entry, sizeof(reg->entry), "%s", entry);
    save_config(&config);
    ensure_store(tool);

    LOG_INFO("Fused `%s` with uvl.", tool);
    LOG_INFO("Mount directory: `%s`", entry);
    LOG_INFO("Store: ~/.uvl/store/%s", tool);
    LOG_INFO("Run `uvl %s ...` to use it through uvl.", tool);
}

int run_tool(const char *tool, int argc, char **argv) {
    Config config;
    load_config(&config);
    Registration *reg = find_registration(&config, tool);
    if (!reg) {
        LOG_ERROR("'%s' is not mounted with uvl.", tool);
        LOG_ERROR("Register it first with `uvl --fuse %s --mnt <dependency-dir>`.", tool);
        return 1;
    }
    if (!command_exists(tool)) {
        fprintf(stderr, "Error: '%s' not found in PATH.\n", tool);
        return 1;
    }

    char cwd[MAX_PATH_LEN];
    getcwd(cwd, sizeof(cwd));
    char target[MAX_PATH_LEN];
    snprintf(target, sizeof(target), "%s/%s", cwd, reg->entry);
    char manifest[MAX_PATH_LEN];
    snprintf(manifest, sizeof(manifest), "%s/.uvl", cwd);

    int was_mounted = is_mountpoint(target);
    if (was_mounted) {
        printf("[uvl] Restoring mounted entry '%s' before running %s\n", reg->entry, tool);
        if (!unmount_target(target, 1)) {
            fprintf(stderr, "Error: failed to unmount '%s' before running command.\n", reg->entry);
            return 1;
        }
        if (restore_project_manifest(manifest, target) != 0) {
            fprintf(stderr, "Error: failed to restore '%s' from .uvl manifest.\n", reg->entry);
            return 1;
        }
    }

    char **exec_args = calloc((size_t)argc + 2, sizeof(char *));
    exec_args[0] = (char *)tool;
    for (int i = 0; i < argc; i++) exec_args[i + 1] = argv[i];
    exec_args[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        execvp(tool, exec_args);
        _exit(127);
    }
    free(exec_args);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    char self[MAX_PATH_LEN];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n < 0) snprintf(self, sizeof(self), "uvl");
    else self[n] = '\0';
    create_virtual_fs(self, tool, target, was_mounted);
    return 0;
}

Registration *find_registration(Config *config, const char *tool) {
    for (size_t i = 0; i < config->len; i++) {
        if (strcmp(config->items[i].tool, tool) == 0) return &config->items[i];
    }
    return NULL;
}

int unregister_tool_config(const char *tool) {
    Config config;
    load_config(&config);
    size_t write = 0;
    int removed = 0;
    for (size_t read = 0; read < config.len; read++) {
        if (strcmp(config.items[read].tool, tool) == 0) {
            removed = 1;
            continue;
        }
        if (write != read) config.items[write] = config.items[read];
        write++;
    }
    config.len = write;
    if (removed) save_config(&config);
    return removed;
}

int ensure_store(const char *tool) {
    char path[MAX_PATH_LEN];
    store_path(path, sizeof(path), tool);
    if (mkdir_p(path) != 0) return -1;
    char child[MAX_PATH_LEN];
    path_join(child, sizeof(child), path, "objects");
    if (mkdir_p(child) != 0) return -1;
    return 0;
}