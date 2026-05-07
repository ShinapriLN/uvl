

#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <openssl/sha.h>
#include <unistd.h>

#include "io/path.h"
#include "io/file.h"
#include "uvl/project.h"
#include "utils/node.h"
#include "io/file.h"


void path_join(char *out, size_t out_len, const char *a, const char *b) {
    snprintf(out, out_len, "%s/%s", a, b);
}

int mkdir_p(const char *path) {
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return 0;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

void parent_dir(char *out, size_t out_len, const char *path) {
    snprintf(out, out_len, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0';
    else snprintf(out, out_len, ".");
}

const char *path_basename_const(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void insert_path(const char *v_path, const char *p_path, int is_dir, int is_symlink, mode_t mode) {
    if (!root) root = create_node("", 1);
    if (strcmp(v_path, "/") == 0) {
        root->is_dir = 1;
        root->mode = mode ? mode : 0755;
        return;
    }

    char path_copy[MAX_PATH_LEN];
    strncpy(path_copy, v_path, MAX_PATH_LEN - 1);
    path_copy[MAX_PATH_LEN - 1] = '\0';

    char *token = strtok(path_copy, "/");
    Node *curr = root;
    while (token) {
        char *next_token = strtok(NULL, "/");
        Node *child = find_child(curr, token);
        if (!child) {
            child = create_node(token, next_token != NULL || is_dir);
            add_child(curr, child);
        }
        if (!next_token) {
            strncpy(child->p_path, p_path, MAX_PATH_LEN - 1);
            child->p_path[MAX_PATH_LEN - 1] = '\0';
            child->is_dir = is_dir;
            child->is_symlink = is_symlink;
            child->mode = mode ? mode : (is_dir ? 0755 : 0444);
        }
        curr = child;
        token = next_token;
    }
}

void rel_join(char *out, size_t out_len, const char *rel, const char *name) {
    if (rel[0] == '\0') snprintf(out, out_len, "/%s", name);
    else snprintf(out, out_len, "%s/%s", rel, name);
}

void fs_join(char *out, size_t out_len, const char *base, const char *name) {
    snprintf(out, out_len, "%s/%s", base, name);
}

void scan_dir(const char *tool, const char *root_dir, const char *dir, const char *rel, FILE *map, ScanStats *stats) {
    DIR *dp = opendir(dir);
    if (!dp) return;

    struct dirent *de;
    while ((de = readdir(dp))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char path[MAX_PATH_LEN];
        char vpath[MAX_PATH_LEN];
        fs_join(path, sizeof(path), dir, de->d_name);
        rel_join(vpath, sizeof(vpath), rel, de->d_name);

        struct stat st;
        if (lstat(path, &st) != 0) continue;

        if (S_ISLNK(st.st_mode)) {
            char target[MAX_PATH_LEN];
            ssize_t n = readlink(path, target, sizeof(target) - 1);
            if (n >= 0) {
                target[n] = '\0';
                write_map(map, "SYM", vpath, target, st.st_mode);
                stats->files++;
            }
        } else if (S_ISDIR(st.st_mode)) {
            write_map(map, "DIR", vpath, "", st.st_mode);
            scan_dir(tool, root_dir, path, vpath, map, stats);
        } else if (S_ISREG(st.st_mode)) {
            char hash[65];
            sha256_file(path, hash);

            char store[MAX_PATH_LEN];
            char obj_dir[MAX_PATH_LEN];
            char obj_path[MAX_PATH_LEN];
            store_path(store, sizeof(store), tool);
            path_join(obj_dir, sizeof(obj_dir), store, "objects");
            path_join(obj_path, sizeof(obj_path), obj_dir, hash);

            if (access(obj_path, F_OK) == 0) {
                unlink(path);
                stats->duplicate_bytes += (uint64_t)st.st_size;
            } else {
                if (move_file(path, obj_path, st.st_mode) == 0) {
                    stats->stored_bytes += (uint64_t)st.st_size;
                }
            }
            write_map(map, "FILE", vpath, obj_path, st.st_mode);
            stats->files++;
        }
    }

    closedir(dp);
    (void)root_dir;
}

void manifest_path(char *out, size_t out_len, const char *tool, const char *target) {
    char parent[MAX_PATH_LEN];
    snprintf(parent, sizeof(parent), "%s", target);
    char *slash = strrchr(parent, '/');
    if (slash) *slash = '\0';
    else snprintf(parent, sizeof(parent), ".");
    snprintf(out, out_len, "%s/.uvl", parent);
    (void)tool;
}