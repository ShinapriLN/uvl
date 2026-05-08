#ifndef UTILS_NODE_H
#define UTILS_NODE_H

#include "utils/types.h"

#include <fuse.h>

#include "utils/constant.h"


extern Node *root;

Node *find_node(const char *path);

Node *create_node(const char *name, int is_dir);

Node *find_child(Node *parent, const char *name);

void add_child(Node *parent, Node *child);

Node *find_parent_node(const char *path, char *name, size_t name_len);

int remove_child(Node *parent, const char *name, int require_dir);

int uvl_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);

int uvl_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);

int uvl_open(const char *path, struct fuse_file_info *fi);

int uvl_release(const char *path, struct fuse_file_info *fi);

int uvl_readlink(const char *path, char *buf, size_t size);

int uvl_release(const char *path, struct fuse_file_info *fi);

int uvl_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int uvl_mkdir(const char *path, mode_t mode);

int uvl_rmdir(const char *path);

int uvl_unlink(const char *path);

int uvl_create(const char *path, mode_t mode, struct fuse_file_info *fi);

int uvl_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

int uvl_truncate(const char *path, off_t size, struct fuse_file_info *fi);

int uvl_rename(const char *from, const char *to, unsigned int flags);

int uvl_symlink(const char *target, const char *linkpath);

int uvl_chmod(const char *path, mode_t mode, struct fuse_file_info *fi);

int uvl_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi);

#endif
