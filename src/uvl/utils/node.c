
#include <stdlib.h>
#include <errno.h>

#include "utils/node.h"
#include "utils/constant.h"

Node *root = NULL;

Node *create_node(const char *name, int is_dir) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    strncpy(n->name, name, sizeof(n->name) - 1);
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
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
    int fd = open(n->p_path, O_RDONLY);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}