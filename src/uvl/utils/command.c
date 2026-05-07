
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "utils/command.h"
#include "utils/constant.h"

int command_exists(const char *tool) {
    const char *path = getenv("PATH");
    if (!path) return 0;

    char copy[MAX_PATH_LEN * 2];
    snprintf(copy, sizeof(copy), "%s", path);
    for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
        char candidate[MAX_PATH_LEN];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, tool);
        if (access(candidate, X_OK) == 0) return 1;
    }
    return 0;
}

int resolve_command(const char *tool, char *out, size_t out_len) {
    const char *path = getenv("PATH");
    if (!path) return 0;

    char copy[MAX_PATH_LEN * 2];
    snprintf(copy, sizeof(copy), "%s", path);
    for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
        char candidate[MAX_PATH_LEN];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, tool);
        if (access(candidate, X_OK) == 0) {
            snprintf(out, out_len, "%s", candidate);
            return 1;
        }
    }
    return 0;
}