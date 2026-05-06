#define FUSE_USE_VERSION 31

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PATH_LEN 1024
#define UVL_MAGIC "UVL3"
#define UVL_KIND_DIR 1
#define UVL_KIND_FILE 2
#define UVL_KIND_SYMLINK 3

typedef struct {
    uint32_t tool_len;
    uint32_t entry_len;
    uint32_t record_count;
} __attribute__((packed)) MapMeta;

typedef struct {
    uint8_t kind;
    uint32_t mode;
    uint32_t v_len;
    uint32_t p_len;
} __attribute__((packed)) MapRecord;

typedef struct Node {
    char name[256];
    int is_dir;
    int is_symlink;
    mode_t mode;
    char p_path[MAX_PATH_LEN];
    struct Node *child;
    struct Node *next;
} Node;

static Node *root = NULL;

Node *create_node(const char *name, int is_dir) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    strncpy(n->name, name, sizeof(n->name) - 1);
    n->is_dir = is_dir;
    n->is_symlink = 0;
    n->mode = is_dir ? 0755 : 0444;
    return n;
}

Node *find_child(Node *parent, const char *name) {
    Node *curr = parent->child;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

void add_child(Node *parent, Node *child) {
    child->next = parent->child;
    parent->child = child;
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

Node *find_node(const char *path) {
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

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void skip_record(FILE *f, MapRecord *record) {
    fseek(f, (long)(record->v_len + record->p_len), SEEK_CUR);
}

static void load_mapping(const char *map_file, const char *mountpoint) {
    FILE *f = fopen(map_file, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open mapping file\n");
        exit(1);
    }

    char magic[4];
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) || memcmp(magic, UVL_MAGIC, sizeof(magic)) != 0) {
        fprintf(stderr, "Invalid uvl mapping file\n");
        fclose(f);
        exit(1);
    }

    uint32_t entry_count = 0;
    if (fread(&entry_count, sizeof(entry_count), 1, f) != 1) {
        fprintf(stderr, "Invalid uvl mapping header\n");
        fclose(f);
        exit(1);
    }

    const char *wanted_entry = basename_of(mountpoint);
    int loaded = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        MapMeta meta;
        if (fread(&meta, sizeof(meta), 1, f) != 1 ||
            meta.tool_len >= MAX_PATH_LEN ||
            meta.entry_len >= MAX_PATH_LEN) {
            fprintf(stderr, "Invalid uvl mapping metadata\n");
            fclose(f);
            exit(1);
        }

        char tool[MAX_PATH_LEN];
        char entry[MAX_PATH_LEN];
        if (fread(tool, 1, meta.tool_len, f) != meta.tool_len ||
            fread(entry, 1, meta.entry_len, f) != meta.entry_len) {
            fprintf(stderr, "Truncated uvl mapping metadata\n");
            fclose(f);
            exit(1);
        }
        tool[meta.tool_len] = '\0';
        entry[meta.entry_len] = '\0';

        int should_load = strcmp(entry, wanted_entry) == 0;
        for (uint32_t j = 0; j < meta.record_count; j++) {
            MapRecord record;
            if (fread(&record, sizeof(record), 1, f) != 1 ||
                record.v_len >= MAX_PATH_LEN ||
                record.p_len >= MAX_PATH_LEN) {
                fprintf(stderr, "Invalid uvl mapping record\n");
                fclose(f);
                exit(1);
            }

            if (!should_load) {
                skip_record(f, &record);
                continue;
            }

            char v_path[MAX_PATH_LEN];
            char p_path[MAX_PATH_LEN];
            if (fread(v_path, 1, record.v_len, f) != record.v_len ||
                fread(p_path, 1, record.p_len, f) != record.p_len) {
                fprintf(stderr, "Truncated uvl mapping file\n");
                fclose(f);
                exit(1);
            }
            v_path[record.v_len] = '\0';
            p_path[record.p_len] = '\0';

            insert_path(
                v_path,
                p_path,
                record.kind == UVL_KIND_DIR,
                record.kind == UVL_KIND_SYMLINK,
                (mode_t)record.mode
            );
            loaded = 1;
        }
    }
    fclose(f);

    if (!loaded) {
        fprintf(stderr, "No uvl mapping entry for %s\n", wanted_entry);
        exit(1);
    }
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
        int res = stat(n->p_path, stbuf);
        if (res == -1) return -errno;
        stbuf->st_mode = S_IFREG | (n->mode ? n->mode : (stbuf->st_mode & 0555));
        stbuf->st_blocks = 0;
    }
    return 0;
}

static int uvl_readdir(
    const char *path,
    void *buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
    (void)offset;
    (void)fi;
    (void)flags;

    Node *n = find_node(path);
    if (!n || !n->is_dir) return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    Node *child = n->child;
    while (child) {
        filler(buf, child->name, NULL, 0, 0);
        child = child->next;
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
    if (res == -1) res = -errno;
    return res;
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

int main(int argc, char *argv[]) {
    if (argc < 3) return 1;

    load_mapping(argv[1], argv[2]);

    int fuse_argc = argc - 1;
    char **fuse_argv = malloc(fuse_argc * sizeof(char *));
    if (!fuse_argv) return 1;

    fuse_argv[0] = argv[0];
    for (int i = 2; i < argc; i++) {
        fuse_argv[i - 1] = argv[i];
    }

    int ret = fuse_main(fuse_argc, fuse_argv, &uvl_oper, NULL);
    free(fuse_argv);
    return ret;
}
