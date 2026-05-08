
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <linux/fs.h>

#include "utils/node.h"
#include "utils/constant.h"
#include "io/file.h"
#include "io/path.h"
#include "io/mnt.h"
#include "uvl/project.h"

Node *root = NULL;

Node *create_node(const char *name, int is_dir) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    snprintf(n->name, sizeof(n->name), "%s", name);
    n->is_dir = is_dir;
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

static Node *detach_child(Node *parent, const char *name) {
    if (!parent || !parent->is_dir) return NULL;
    Node *prev = NULL;
    Node *curr = parent->child;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (prev) prev->next = curr->next;
            else parent->child = curr->next;
            curr->next = NULL;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

static void free_node(Node *node) {
    if (!node) return;
    Node *child = node->child;
    while (child) {
        Node *next = child->next;
        free_node(child);
        child = next;
    }
    free(node);
}

Node *find_parent_node(const char *path, char *name, size_t name_len) {
    if (strcmp(path, "/") == 0) return NULL;
    char path_copy[MAX_PATH_LEN];
    strncpy(path_copy, path, MAX_PATH_LEN - 1);
    path_copy[MAX_PATH_LEN - 1] = '\0';

    char *last = strrchr(path_copy, '/');
    if (!last) return NULL;
    snprintf(name, name_len, "%s", last + 1);
    if (last == path_copy) return root;
    *last = '\0';
    return find_node(path_copy);
}

int remove_child(Node *parent, const char *name, int require_dir) {
    if (!parent || !parent->is_dir) return -ENOENT;
    Node *prev = NULL;
    Node *curr = parent->child;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (require_dir && !curr->is_dir) return -ENOTDIR;
            if (!require_dir && curr->is_dir) return -EISDIR;
            if (curr->is_dir && curr->child) return -ENOTEMPTY;
            if (prev) prev->next = curr->next;
            else parent->child = curr->next;
            free_node(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    return -ENOENT;
}

Node *find_node(const char *path) {
    if (strcmp(path, "/") == 0) return root;
    char path_copy[MAX_PATH_LEN];
    strncpy(path_copy, path, MAX_PATH_LEN - 1);
    path_copy[MAX_PATH_LEN - 1] = '\0';
    char *saveptr = NULL;
    char *token = strtok_r(path_copy, "/", &saveptr);
    Node *curr = root;
    while (token && curr) {
        curr = find_child(curr, token);
        token = strtok_r(NULL, "/", &saveptr);
    }
    return curr;
}

static int persist_tree(void) {
    const char *manifest = mount_manifest_path();
    const char *tool = mount_tool_name();
    const char *entry = mount_entry_name();
    if (!manifest[0] || !tool[0] || !entry[0]) return -EIO;
    if (rewrite_manifest_from_tree(manifest, tool, entry, root) != 0) return -EIO;
    return 0;
}

static int make_temp_file(char *path, size_t path_len) {
    char store[MAX_PATH_LEN];
    char tmp_dir[MAX_PATH_LEN];
    store_path(store, sizeof(store), mount_tool_name());
    path_join(tmp_dir, sizeof(tmp_dir), store, "tmp");
    if (mkdir_p(tmp_dir) != 0) return -1;
    snprintf(path, path_len, "%s/uvl-write-XXXXXX", tmp_dir);
    int fd = mkstemp(path);
    return fd;
}

static int copy_fd_from_path(int out, const char *src) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -errno;
    char buf[1024 * 1024];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w < 0) {
                int err = -errno;
                close(in);
                return err;
            }
            p += w;
            n -= w;
        }
    }
    if (n < 0) {
        int err = -errno;
        close(in);
        return err;
    }
    close(in);
    return 0;
}

static int ensure_writable_node(Node *node, int truncate_file) {
    if (!node || node->is_dir || node->is_symlink) return -ENOENT;
    if (node->dirty) {
        if (truncate_file && truncate(node->p_path, 0) != 0) return -errno;
        return 0;
    }

    char tmp[MAX_PATH_LEN];
    int fd = make_temp_file(tmp, sizeof(tmp));
    if (fd < 0) return -errno;
    if (!truncate_file && node->p_path[0]) {
        int copied = copy_fd_from_path(fd, node->p_path);
        if (copied != 0) {
            close(fd);
            unlink(tmp);
            return copied;
        }
    }
    close(fd);
    snprintf(node->p_path, sizeof(node->p_path), "%s", tmp);
    node->dirty = 1;
    return 0;
}

static int commit_node(Node *node) {
    if (!node || !node->dirty) return 0;

    char hash[65];
    sha256_file(node->p_path, hash);

    char store[MAX_PATH_LEN];
    char obj_path[MAX_PATH_LEN];
    store_path(store, sizeof(store), mount_tool_name());
    mkdir_p(store);
    path_join(obj_path, sizeof(obj_path), store, hash);

    if (access(obj_path, F_OK) == 0) {
        unlink(node->p_path);
    } else if (move_file(node->p_path, obj_path, node->mode) != 0) {
        return -errno;
    }
    snprintf(node->p_path, sizeof(node->p_path), "%s", obj_path);
    node->dirty = 0;
    return persist_tree();
}

int uvl_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
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

int uvl_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
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

int uvl_open(const char *path, struct fuse_file_info *fi) {
    Node *n = find_node(path);
    if (!n || n->is_dir || n->is_symlink) return -ENOENT;
    int accmode = fi->flags & O_ACCMODE;
    if (accmode != O_RDONLY) {
        int writable = ensure_writable_node(n, (fi->flags & O_TRUNC) != 0);
        if (writable != 0) return writable;
    }
    int fd = open(n->p_path, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

int uvl_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) res = -errno;
    return res;
}

int uvl_readlink(const char *path, char *buf, size_t size) {
    Node *n = find_node(path);
    if (!n || !n->is_symlink) return -EINVAL;

    strncpy(buf, n->p_path, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

int uvl_release(const char *path, struct fuse_file_info *fi) {
    Node *n = find_node(path);
    close(fi->fh);
    if (n && n->dirty) return commit_node(n);
    return 0;
}

int uvl_mkdir(const char *path, mode_t mode) {
    char name[MAX_NAME_LEN];
    Node *parent = find_parent_node(path, name, sizeof(name));
    if (!parent || !parent->is_dir) return -ENOENT;
    if (find_child(parent, name)) return -EEXIST;
    Node *child = create_node(name, 1);
    child->mode = mode & 0777;
    add_child(parent, child);
    return persist_tree();
}

int uvl_rmdir(const char *path) {
    char name[MAX_NAME_LEN];
    Node *parent = find_parent_node(path, name, sizeof(name));
    int removed = remove_child(parent, name, 1);
    if (removed != 0) return removed;
    return persist_tree();
}

int uvl_unlink(const char *path) {
    char name[MAX_NAME_LEN];
    Node *parent = find_parent_node(path, name, sizeof(name));
    int removed = remove_child(parent, name, 0);
    if (removed != 0) return removed;
    return persist_tree();
}

int uvl_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char name[MAX_NAME_LEN];
    Node *parent = find_parent_node(path, name, sizeof(name));
    if (!parent || !parent->is_dir) return -ENOENT;
    if (find_child(parent, name)) return -EEXIST;

    char tmp[MAX_PATH_LEN];
    int fd = make_temp_file(tmp, sizeof(tmp));
    if (fd < 0) return -errno;
    if (fchmod(fd, mode & 0777) != 0) {
        int err = -errno;
        close(fd);
        unlink(tmp);
        return err;
    }

    Node *child = create_node(name, 0);
    child->mode = mode & 0777;
    child->dirty = 1;
    snprintf(child->p_path, sizeof(child->p_path), "%s", tmp);
    add_child(parent, child);
    fi->fh = fd;
    return 0;
}

int uvl_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    ssize_t written = pwrite(fi->fh, buf, size, offset);
    if (written < 0) return -errno;
    return (int)written;
}

int uvl_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void)fi;
    Node *n = find_node(path);
    if (!n || n->is_dir || n->is_symlink) return -ENOENT;
    int writable = ensure_writable_node(n, 0);
    if (writable != 0) return writable;
    if (truncate(n->p_path, size) != 0) return -errno;
    return commit_node(n);
}

int uvl_rename(const char *from, const char *to, unsigned int flags) {
    if (flags & ~RENAME_NOREPLACE) return -EINVAL;

    char from_name[MAX_NAME_LEN];
    char to_name[MAX_NAME_LEN];
    Node *from_parent = find_parent_node(from, from_name, sizeof(from_name));
    Node *to_parent = find_parent_node(to, to_name, sizeof(to_name));
    if (!from_parent || !to_parent || !to_parent->is_dir) return -ENOENT;

    Node *node = detach_child(from_parent, from_name);
    if (!node) return -ENOENT;

    Node *existing = find_child(to_parent, to_name);
    if (existing && (flags & RENAME_NOREPLACE)) {
        add_child(from_parent, node);
        return -EEXIST;
    }
    if (existing) {
        existing = detach_child(to_parent, to_name);
        free_node(existing);
    }

    snprintf(node->name, sizeof(node->name), "%s", to_name);
    add_child(to_parent, node);
    return persist_tree();
}

int uvl_symlink(const char *target, const char *linkpath) {
    char name[MAX_NAME_LEN];
    Node *parent = find_parent_node(linkpath, name, sizeof(name));
    if (!parent || !parent->is_dir) return -ENOENT;
    if (find_child(parent, name)) return -EEXIST;

    Node *child = create_node(name, 0);
    child->is_symlink = 1;
    child->mode = 0777;
    snprintf(child->p_path, sizeof(child->p_path), "%s", target);
    add_child(parent, child);
    return persist_tree();
}

int uvl_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void)fi;
    Node *n = find_node(path);
    if (!n) return -ENOENT;
    n->mode = mode & 07777;
    return persist_tree();
}

int uvl_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void)tv;
    (void)fi;
    Node *n = find_node(path);
    if (!n) return -ENOENT;
    return 0;
}
