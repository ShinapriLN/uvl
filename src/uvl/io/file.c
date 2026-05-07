
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <openssl/sha.h>

#include "io/file.h"
#include "io/path.h"
#include "utils/types.h"
#include "utils/constant.h"
#include "utils/logging.h"
#include "uvl/tool.h"

char *read_file(const char *path) {
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

int skip_manifest_records(FILE *f, uint32_t record_count) {
    for (uint32_t i = 0; i < record_count; i++) {
        ProjectRecord record;
        if (fread(&record, sizeof(record), 1, f) != 1) return -1;
        if (fseek(f, (long)(record.v_len + record.p_len), SEEK_CUR) != 0) return -1;
    }
    return 0;
}

int restore_project_manifest(const char *manifest, const char *target) {
    FILE *f = fopen(manifest, "rb");
    if (!f) return -1;

    char magic[4];
    ProjectHeader header;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, UVL_PROJECT_MAGIC, sizeof(magic)) != 0 ||
        fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    const char *wanted_entry = path_basename_const(target);
    uint32_t record_count = 0;
    int found = 0;

    for (uint32_t i = 0; i < header.entry_count; i++) {
        ProjectMeta meta;
        if (fread(&meta, sizeof(meta), 1, f) != 1 ||
            meta.tool_len >= MAX_NAME_LEN ||
            meta.entry_len >= MAX_PATH_LEN) {
            fclose(f);
            return -1;
        }

        char tool[MAX_NAME_LEN];
        char entry[MAX_PATH_LEN];
        if (fread(tool, 1, meta.tool_len, f) != meta.tool_len ||
            fread(entry, 1, meta.entry_len, f) != meta.entry_len) {
            fclose(f);
            return -1;
        }
        tool[meta.tool_len] = '\0';
        entry[meta.entry_len] = '\0';

        if (strcmp(entry, wanted_entry) == 0) {
            record_count = meta.record_count;
            found = 1;
            break;
        }

        if (skip_manifest_records(f, meta.record_count) != 0) {
            fclose(f);
            return -1;
        }
    }

    if (!found) {
        fclose(f);
        return -1;
    }

    remove_tree(target);
    mkdir_p(target);

    for (uint32_t i = 0; i < record_count; i++) {
        ProjectRecord record;
        if (fread(&record, sizeof(record), 1, f) != 1) {
            fclose(f);
            return -1;
        }
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


int remove_tree(const char *path) {
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

int copy_file(const char *src, const char *dst, mode_t mode) {
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

int move_file(const char *src, const char *dst, mode_t mode) {
    if (rename(src, dst) == 0) {
        chmod(dst, mode & 0777);
        return 0;
    }
    if (errno != EXDEV) return -1;
    if (copy_file(src, dst, mode) != 0) return -1;
    unlink(src);
    return 0;
}

int build_mapping(const char *tool, const char *target, char *manifest, size_t manifest_len, ScanStats *stats) {
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

void sha256_file(const char *path, char hex[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) LOG_ERROR("Could not open %s", path);

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

void escape(FILE *f, const char *s) {
    for (; *s; s++) {
        if (*s == '\\') fputs("\\\\", f);
        else if (*s == '\t') fputs("\\t", f);
        else if (*s == '\n') fputs("\\n", f);
        else fputc(*s, f);
    }
}

void write_map(FILE *f, const char *kind, const char *vpath, const char *ppath, mode_t mode) {
    fprintf(f, "%s\t", kind);
    escape(f, vpath);
    fputc('\t', f);
    if (ppath) escape(f, ppath);
    fprintf(f, "\t%o\n", mode & 07777);
}