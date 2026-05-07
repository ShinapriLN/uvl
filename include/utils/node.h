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

int uvl_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);

int uvl_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);

int uvl_open(const char *path, struct fuse_file_info *fi);

#endif