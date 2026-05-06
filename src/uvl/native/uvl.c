#define FUSE_USE_VERSION 31

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_PATH_LEN 4096
#define MAX_NAME_LEN 256
#define MAX_REGISTRATIONS 128
#define UVL_PROJECT_MAGIC "UVL3"

typedef struct {
    uint32_t tool_len;
    uint32_t entry_len;
    uint32_t record_count;
} __attribute__((packed)) ProjectMeta;

typedef struct {
    uint8_t kind;
    uint32_t mode;
    uint32_t v_len;
    uint32_t p_len;
} __attribute__((packed)) ProjectRecord;

typedef struct {
    char tool[MAX_NAME_LEN];
    char entry[MAX_PATH_LEN];
} Registration;

typedef struct {
    Registration items[MAX_REGISTRATIONS];
    size_t len;
} Config;

typedef struct Node {
    char name[MAX_NAME_LEN];
    int is_dir;
    int is_symlink;
    mode_t mode;
    char p_path[MAX_PATH_LEN];
    struct Node *child;
    struct Node *next;
} Node;

typedef struct {
    size_t files;
    uint64_t stored_bytes;
    uint64_t duplicate_bytes;
} ScanStats;

static Node *root = NULL;

static void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static const char *home_dir(void) {
    const char *home = getenv("HOME");
    return home && home[0] ? home : ".";
}

static void path_join(char *out, size_t out_len, const char *a, const char *b) {
    snprintf(out, out_len, "%s/%s", a, b);
}

static void uvl_home(char *out, size_t out_len) {
    snprintf(out, out_len, "%s/.uvl", home_dir());
}

static void config_path(char *out, size_t out_len) {
    char base[MAX_PATH_LEN];
    uvl_home(base, sizeof(base));
    path_join(out, out_len, base, "config.json");
}

static void store_path(char *out, size_t out_len, const char *tool) {
    snprintf(out, out_len, "%s/.uvl/store/%s", home_dir(), tool);
}

static int mkdir_p(const char *path) {
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

static int ensure_store(const char *tool) {
    char path[MAX_PATH_LEN];
    store_path(path, sizeof(path), tool);
    if (mkdir_p(path) != 0) return -1;
    char child[MAX_PATH_LEN];
    path_join(child, sizeof(child), path, "objects");
    if (mkdir_p(child) != 0) return -1;
    return 0;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = calloc((size_t)size + 1, 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, (size_t)size, f);
    fclose(f);
    return buf;
}

static void load_config(Config *config) {
    memset(config, 0, sizeof(*config));
    char path[MAX_PATH_LEN];
    config_path(path, sizeof(path));
    char *text = read_file(path);
    if (!text) return;

    char *cursor = text;
    while ((cursor = strstr(cursor, "\"entry\"")) && config->len < MAX_REGISTRATIONS) {
        char *scan = cursor;
        char *tool_start = NULL;
        while (scan > text) {
            scan--;
            if (*scan == '"') {
                char *maybe_start = scan - 1;
                while (maybe_start > text && *maybe_start != '"') maybe_start--;
                if (*maybe_start == '"') {
                    tool_start = maybe_start + 1;
                    break;
                }
            }
        }
        char *entry_colon = strchr(cursor, ':');
        char *entry_quote = entry_colon ? strchr(entry_colon, '"') : NULL;
        char *entry_end = entry_quote ? strchr(entry_quote + 1, '"') : NULL;
        if (tool_start && entry_quote && entry_end) {
            char *tool_end = strchr(tool_start, '"');
            size_t tool_len = tool_end ? (size_t)(tool_end - tool_start) : 0;
            size_t entry_len = (size_t)(entry_end - entry_quote - 1);
            if (tool_len > 0 && tool_len < MAX_NAME_LEN && entry_len < MAX_PATH_LEN) {
                Registration *reg = &config->items[config->len++];
                memcpy(reg->tool, tool_start, tool_len);
                reg->tool[tool_len] = '\0';
                memcpy(reg->entry, entry_quote + 1, entry_len);
                reg->entry[entry_len] = '\0';
            }
        }
        cursor = entry_end ? entry_end + 1 : cursor + 7;
    }
    free(text);
}

static Registration *find_registration(Config *config, const char *tool) {
    for (size_t i = 0; i < config->len; i++) {
        if (strcmp(config->items[i].tool, tool) == 0) return &config->items[i];
    }
    return NULL;
}

static void save_config(Config *config) {
    char base[MAX_PATH_LEN];
    uvl_home(base, sizeof(base));
    if (mkdir_p(base) != 0) die("[uvl] could not create %s", base);

    char path[MAX_PATH_LEN];
    config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) die("[uvl] could not write %s", path);

    fputs("{\n  \"registrations\": {\n", f);
    for (size_t i = 0; i < config->len; i++) {
        fprintf(
            f,
            "    \"%s\": {\"entry\": \"%s\"}%s\n",
            config->items[i].tool,
            config->items[i].entry,
            i + 1 == config->len ? "" : ","
        );
    }
    fputs("  }\n}\n", f);
    fclose(f);
}

static const char *default_entry(const char *tool) {
    if (strcmp(tool, "bun") == 0) return "node_modules";
    if (strcmp(tool, "npm") == 0) return "node_modules";
    if (strcmp(tool, "pnpm") == 0) return "node_modules";
    if (strcmp(tool, "yarn") == 0) return "node_modules";
    if (strcmp(tool, "uv") == 0) return ".venv";
    if (strcmp(tool, "pip") == 0) return ".venv";
    if (strcmp(tool, "poetry") == 0) return ".venv";
    return NULL;
}

static int command_exists(const char *tool) {
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

static int resolve_command(const char *tool, char *out, size_t out_len) {
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

static void register_tool(const char *tool, const char *entry) {
    char exe_path[MAX_PATH_LEN];
    if (!resolve_command(tool, exe_path, sizeof(exe_path))) {
        fprintf(stderr, "[uvl] ❌ Cannot mount '%s': binary not found in PATH\n", tool);
        fprintf(stderr, "Install '%s' first, or pass the real executable name to `uvl --fuse`.\n", tool);
        exit(1);
    }

    Config config;
    load_config(&config);
    Registration *reg = find_registration(&config, tool);
    if (!reg) {
        if (config.len >= MAX_REGISTRATIONS) die("[uvl] too many registrations");
        reg = &config.items[config.len++];
    }
    snprintf(reg->tool, sizeof(reg->tool), "%s", tool);
    snprintf(reg->entry, sizeof(reg->entry), "%s", entry);
    save_config(&config);
    ensure_store(tool);

    printf("[uvl] Resolved '%s' binary: %s\n", tool, exe_path);
    printf("✅ %s has been mounted with uvl.\n", tool);
    printf("👍 now you can use `uvl %s ...` with any %s arguments.\n", tool, tool);
    printf("✨ %s will physically store at ~/.uvl/store/%s\n\n", entry, tool);
    printf("💥 Caution: While a directory is mounted by uvl, avoid deleting or modifying it directly.\n");
    printf("Unmount it first with `uvl --unmnt %s`.\n", entry);
}

static void sha256_file(const char *path, char hex[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) die("[uvl] could not open %s", path);

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    unsigned char buf[1024 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        SHA256_Update(&ctx, buf, n);
    }
    fclose(f);

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + (i * 2), "%02x", digest[i]);
    }
    hex[64] = '\0';
}

static void escape(FILE *f, const char *s) {
    for (; *s; s++) {
        if (*s == '\\') fputs("\\\\", f);
        else if (*s == '\t') fputs("\\t", f);
        else if (*s == '\n') fputs("\\n", f);
        else fputc(*s, f);
    }
}

static void write_map(FILE *f, const char *kind, const char *vpath, const char *ppath, mode_t mode) {
    fprintf(f, "%s\t", kind);
    escape(f, vpath);
    fputc('\t', f);
    if (ppath) escape(f, ppath);
    fprintf(f, "\t%o\n", mode & 07777);
}

static int copy_file(const char *src, const char *dst, mode_t mode) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[1024 * 1024];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w < 0) {
                close(in);
                close(out);
                return -1;
            }
            p += w;
            n -= w;
        }
    }
    close(in);
    close(out);
    return 0;
}

static int move_file(const char *src, const char *dst, mode_t mode) {
    if (rename(src, dst) == 0) {
        chmod(dst, mode & 0777);
        return 0;
    }
    if (errno != EXDEV) return -1;
    if (copy_file(src, dst, mode) != 0) return -1;
    unlink(src);
    return 0;
}

static void rel_join(char *out, size_t out_len, const char *rel, const char *name) {
    if (rel[0] == '\0') snprintf(out, out_len, "/%s", name);
    else snprintf(out, out_len, "%s/%s", rel, name);
}

static void fs_join(char *out, size_t out_len, const char *base, const char *name) {
    snprintf(out, out_len, "%s/%s", base, name);
}

static void scan_dir(const char *tool, const char *root_dir, const char *dir, const char *rel, FILE *map, ScanStats *stats) {
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

static void manifest_path(char *out, size_t out_len, const char *tool, const char *target) {
    char parent[MAX_PATH_LEN];
    snprintf(parent, sizeof(parent), "%s", target);
    char *slash = strrchr(parent, '/');
    if (slash) *slash = '\0';
    else snprintf(parent, sizeof(parent), ".");
    snprintf(out, out_len, "%s/.uvl", parent);
    (void)tool;
}

static int build_mapping(const char *tool, const char *target, char *manifest, size_t manifest_len, ScanStats *stats) {
    memset(stats, 0, sizeof(*stats));
    ensure_store(tool);
    manifest_path(manifest, manifest_len, tool, target);

    FILE *map = fopen(manifest, "w");
    if (!map) return -1;

    struct stat st;
    if (stat(target, &st) != 0) {
        fclose(map);
        return -1;
    }
    write_map(map, "DIR", "/", "", st.st_mode);
    scan_dir(tool, target, target, "", map, stats);
    fclose(map);
    return 0;
}

static int remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR *dp = opendir(path);
        if (!dp) return -1;
        struct dirent *de;
        while ((de = readdir(dp))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            char child[MAX_PATH_LEN];
            fs_join(child, sizeof(child), path, de->d_name);
            remove_tree(child);
        }
        closedir(dp);
        return rmdir(path);
    }
    return unlink(path);
}

static int is_mountpoint(const char *path) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("mountpoint", "mountpoint", "-q", path, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int unmount_target(const char *target, int quiet) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("fusermount3", "fusermount3", "-u", target, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        pid = fork();
        if (pid == 0) {
            execlp("fusermount3", "fusermount3", "-uz", target, NULL);
            _exit(127);
        }
        waitpid(pid, &status, 0);
        ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (!quiet) {
        if (ok) printf("✅ Unmounted %s\n⚡ You can now delete or modify anything inside\n", target);
        else fprintf(stderr, "[uvl] Could not unmount %s\n", target);
    }
    return ok;
}

static void parent_dir(char *out, size_t out_len, const char *path) {
    snprintf(out, out_len, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0';
    else snprintf(out, out_len, ".");
}

static int restore_project_manifest(const char *manifest, const char *target) {
    FILE *f = fopen(manifest, "rb");
    if (!f) return -1;

    char magic[4];
    ProjectMeta meta;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, UVL_PROJECT_MAGIC, sizeof(magic)) != 0 ||
        fread(&meta, sizeof(meta), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    if (fseek(f, (long)(meta.tool_len + meta.entry_len), SEEK_CUR) != 0) {
        fclose(f);
        return -1;
    }

    remove_tree(target);
    mkdir_p(target);

    ProjectRecord record;
    while (fread(&record, sizeof(record), 1, f) == 1) {
        if (record.v_len >= MAX_PATH_LEN || record.p_len >= MAX_PATH_LEN) {
            fclose(f);
            return -1;
        }
        char v_path[MAX_PATH_LEN];
        char p_path[MAX_PATH_LEN];
        if (fread(v_path, 1, record.v_len, f) != record.v_len ||
            fread(p_path, 1, record.p_len, f) != record.p_len) {
            fclose(f);
            return -1;
        }
        v_path[record.v_len] = '\0';
        p_path[record.p_len] = '\0';

        char path[MAX_PATH_LEN];
        if (strcmp(v_path, "/") == 0) snprintf(path, sizeof(path), "%s", target);
        else snprintf(path, sizeof(path), "%s%s", target, v_path);

        if (record.kind == 1) {
            mkdir_p(path);
            chmod(path, record.mode & 0777);
        } else if (record.kind == 2) {
            char parent[MAX_PATH_LEN];
            parent_dir(parent, sizeof(parent), path);
            mkdir_p(parent);
            if (link(p_path, path) != 0 && copy_file(p_path, path, (mode_t)record.mode) != 0) {
                fclose(f);
                return -1;
            }
            chmod(path, record.mode & 0777);
        } else if (record.kind == 3) {
            char parent[MAX_PATH_LEN];
            parent_dir(parent, sizeof(parent), path);
            mkdir_p(parent);
            symlink(p_path, path);
        }
    }
    fclose(f);
    return 0;
}

static int wait_for_mount(const char *target) {
    for (int i = 0; i < 40; i++) {
        if (is_mountpoint(target)) return 1;
        usleep(50000);
    }
    return is_mountpoint(target);
}

static int spawn_mount(const char *self, const char *manifest, const char *target) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execl(self, self, "__mount", manifest, target, NULL);
        _exit(127);
    }
    return wait_for_mount(target);
}

static int create_virtual_fs(const char *self, const char *tool, const char *target, int remount) {
    struct stat st;
    if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    if (is_mountpoint(target)) return 0;

    if (remount) {
        printf("\n[uvl] Re-virtualizing '%s' after command completed\n", target);
    } else {
        printf("\n[uvl] Taking control of '%s'\n", target);
    }

    char manifest[MAX_PATH_LEN];
    ScanStats stats;
    if (build_mapping(tool, target, manifest, sizeof(manifest), &stats) != 0) {
        fprintf(stderr, "[uvl] failed to build manifest for %s\n", target);
        return 0;
    }

    remove_tree(target);
    mkdir_p(target);

    printf("[uvl] 🚀 Mounting virtual filesystem with %zu files...\n", stats.files);
    if (!spawn_mount(self, manifest, target)) {
        fprintf(stderr, "[uvl] ❌ Failed to mount virtual '%s'.\n", target);
        return 0;
    }

    double saved_mb = (double)stats.duplicate_bytes / (1024.0 * 1024.0);
    double virtualized_mb = (double)(stats.stored_bytes + stats.duplicate_bytes) / (1024.0 * 1024.0);
    printf("[uvl] ✅ Successfully %s virtual '%s'.\n", remount ? "remounted" : "mounted", target);
    printf("[uvl] 💾 Deduplication saved %.2f MB of physical disk space.\n", saved_mb);
    printf("[uvl] 📦 Virtualized %.2f MB into ~/.uvl/store/%s.\n", virtualized_mb, tool);
    return 1;
}

static int run_tool(const char *tool, int argc, char **argv) {
    Config config;
    load_config(&config);
    Registration *reg = find_registration(&config, tool);
    if (!reg) {
        fprintf(stderr, "Error: '%s' is not mounted with uvl.\n", tool);
        fprintf(stderr, "Register it first with `uvl --fuse %s --mnt <dependency-dir>`.\n", tool);
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

static Node *create_node(const char *name, int is_dir) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    strncpy(n->name, name, sizeof(n->name) - 1);
    n->is_dir = is_dir;
    n->mode = is_dir ? 0755 : 0444;
    return n;
}

static Node *find_child(Node *parent, const char *name) {
    Node *curr = parent->child;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

static void add_child(Node *parent, Node *child) {
    child->next = parent->child;
    parent->child = child;
}

static void unescape_str(char *s) {
    char *src = s;
    char *dst = s;
    while (*src) {
        if (*src == '\\' && src[1] == 't') {
            *dst++ = '\t';
            src += 2;
        } else if (*src == '\\' && src[1] == 'n') {
            *dst++ = '\n';
            src += 2;
        } else if (*src == '\\' && src[1] == '\\') {
            *dst++ = '\\';
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void split_fields(char *line, char **fields, int max_fields) {
    char *cursor = line;
    for (int i = 0; i < max_fields; i++) {
        fields[i] = cursor;
        char *tab = strchr(cursor, '\t');
        if (!tab) {
            for (int j = i + 1; j < max_fields; j++) fields[j] = NULL;
            return;
        }
        *tab = '\0';
        cursor = tab + 1;
    }
}

static void insert_path(const char *v_path, const char *p_path, int is_dir, int is_symlink, mode_t mode) {
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

static Node *find_node(const char *path) {
    if (strcmp(path, "/") == 0) return root;
    char path_copy[MAX_PATH_LEN];
    strncpy(path_copy, path, MAX_PATH_LEN - 1);
    path_copy[MAX_PATH_LEN - 1] = '\0';
    char *token = strtok(path_copy, "/");
    Node *curr = root;
    while (token && curr) {
        curr = find_child(curr, token);
        token = strtok(NULL, "/");
    }
    return curr;
}

static void load_mapping(const char *map_file) {
    FILE *f = fopen(map_file, "r");
    if (!f) die("Failed to open mapping file");

    char line[MAX_PATH_LEN * 2];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        char *fields[4] = {0};
        split_fields(line, fields, 4);
        char *kind = fields[0];
        char *v_path = fields[1];
        char *p_path = fields[2] ? fields[2] : "";
        char *mode_s = fields[3];
        if (!kind || !v_path) continue;
        unescape_str(v_path);
        unescape_str(p_path);
        mode_t mode = mode_s ? (mode_t)strtol(mode_s, NULL, 8) : 0;
        insert_path(v_path, p_path, strcmp(kind, "DIR") == 0, strcmp(kind, "SYM") == 0, mode);
    }
    fclose(f);
}

static int uvl_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));
    Node *n = find_node(path);
    if (!n) return -ENOENT;
    if (n->is_dir) {
        stbuf->st_mode = S_IFDIR | (n->mode ? n->mode : 0755);
        stbuf->st_nlink = 2;
    } else if (n->is_symlink) {
        stbuf->st_mode = S_IFLNK | (n->mode ? n->mode : 0777);
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(n->p_path);
    } else {
        if (stat(n->p_path, stbuf) == -1) return -errno;
        stbuf->st_mode = S_IFREG | (n->mode ? n->mode : (stbuf->st_mode & 0555));
        stbuf->st_blocks = 0;
    }
    return 0;
}

static int uvl_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;
    Node *n = find_node(path);
    if (!n || !n->is_dir) return -ENOENT;
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    for (Node *child = n->child; child; child = child->next) {
        filler(buf, child->name, NULL, 0, 0);
    }
    return 0;
}

static int uvl_open(const char *path, struct fuse_file_info *fi) {
    Node *n = find_node(path);
    if (!n || n->is_dir || n->is_symlink) return -ENOENT;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
    int fd = open(n->p_path, O_RDONLY);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int uvl_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    int res = pread(fi->fh, buf, size, offset);
    return res == -1 ? -errno : res;
}

static int uvl_readlink(const char *path, char *buf, size_t size) {
    Node *n = find_node(path);
    if (!n || !n->is_symlink) return -EINVAL;
    strncpy(buf, n->p_path, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

static int uvl_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    close(fi->fh);
    return 0;
}

static const struct fuse_operations uvl_oper = {
    .getattr = uvl_getattr,
    .readdir = uvl_readdir,
    .open = uvl_open,
    .read = uvl_read,
    .readlink = uvl_readlink,
    .release = uvl_release,
};

static int mount_mode(int argc, char **argv) {
    if (argc < 4) return 1;
    load_mapping(argv[2]);

    char *fuse_argv[] = {argv[0], argv[3], NULL};
    return fuse_main(2, fuse_argv, &uvl_oper, NULL);
}

static int print_project_status(void) {
    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) return 1;

    char manifest[MAX_PATH_LEN];
    snprintf(manifest, sizeof(manifest), "%s/.uvl", cwd);

    FILE *f = fopen(manifest, "rb");
    if (!f) {
        puts("Project status: not initialized");
        puts("No .uvl manifest exists in this directory.");
        puts("Run a registered package manager through uvl to create one.");
        return 0;
    }

    char magic[4];
    ProjectMeta meta;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, UVL_PROJECT_MAGIC, sizeof(magic)) != 0 ||
        fread(&meta, sizeof(meta), 1, f) != 1 ||
        meta.tool_len >= MAX_NAME_LEN ||
        meta.entry_len >= MAX_PATH_LEN) {
        fclose(f);
        printf("Project status: invalid\nManifest: %s\n", manifest);
        return 0;
    }

    char tool[MAX_NAME_LEN];
    char entry[MAX_PATH_LEN];
    if (fread(tool, 1, meta.tool_len, f) != meta.tool_len ||
        fread(entry, 1, meta.entry_len, f) != meta.entry_len) {
        fclose(f);
        printf("Project status: invalid\nManifest: %s\n", manifest);
        return 0;
    }
    tool[meta.tool_len] = '\0';
    entry[meta.entry_len] = '\0';

    size_t records = 0;
    ProjectRecord record;
    while (fread(&record, sizeof(record), 1, f) == 1) {
        if (fseek(f, (long)(record.v_len + record.p_len), SEEK_CUR) != 0) break;
        records++;
    }
    fclose(f);

    char target[MAX_PATH_LEN];
    snprintf(target, sizeof(target), "%s/%s", cwd, entry);

    printf("Project status:\n");
    printf("  tool: %s\n", tool);
    printf("  entry: %s\n", entry);
    printf("  manifest: %s\n", manifest);
    printf("  store: ~/.uvl/store/%s\n", tool);
    printf("  records: %zu\n", records);
    printf("  mounted: %s\n", is_mountpoint(target) ? "yes" : "no");
    return 0;
}

static void print_help(void) {
    puts("uvl: virtual dependency directories backed by a shared FUSE store");
    puts("");
    puts("Usage:");
    puts("  uvl --fuse <tool> --mnt <dir>   Register a package manager mount directory");
    puts("  uvl <tool> [args...]              Run the package manager through uvl");
    puts("  uvl --unmnt <dir>                 Unmount a virtualized dependency directory");
    puts("  uvl --status                      Show current project mount status");
    puts("  uvl --list                        List registered package manager binaries");
    puts("  uvl --has <tool>                  Check whether a tool is mounted and installed");
    puts("  uvl --version, -v                 Print version");
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }
    if (strcmp(argv[1], "__mount") == 0) return mount_mode(argc, argv);
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        puts("uvl native 0.0.5");
        return 0;
    }
    if (strcmp(argv[1], "--fuse") == 0) {
        if (argc < 3) die("Usage: uvl --fuse <tool> --mnt <dependency-dir>");
        const char *entry = NULL;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--mnt") == 0 && i + 1 < argc) entry = argv[i + 1];
        }
        if (!entry) entry = default_entry(argv[2]);
        if (!entry) die("Usage: uvl --fuse <tool> --mnt <dependency-dir>");
        register_tool(argv[2], entry);
        return 0;
    }
    if (strcmp(argv[1], "--unmnt") == 0) {
        if (argc < 3) die("Usage: uvl --unmnt <dir>");
        return unmount_target(argv[2], 0) ? 0 : 1;
    }
    if (strcmp(argv[1], "--has") == 0) {
        if (argc < 3) return 1;
        Config config;
        load_config(&config);
        int ok = find_registration(&config, argv[2]) && command_exists(argv[2]);
        puts(ok ? "true" : "false");
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "--status") == 0) {
        return print_project_status();
    }
    if (strcmp(argv[1], "--list") == 0) {
        Config config;
        load_config(&config);
        for (size_t i = 0; i < config.len; i++) {
            puts(config.items[i].tool);
        }
        return 0;
    }

    return run_tool(argv[1], argc - 2, argv + 2);
}
