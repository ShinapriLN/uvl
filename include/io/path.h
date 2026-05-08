#ifndef IO_PATH_H
#define IO_PATH_H

#include <sys/stat.h>
#include <stddef.h>
#include <stdio.h>

#include "utils/types.h"
#include "utils/constant.h"


void path_join(char *out, size_t out_len, const char *a, const char *b);

int mkdir_p(const char *path);

void parent_dir(char *out, size_t out_len, const char *path);

const char *path_basename_const(const char *path);

void insert_path(const char *v_path, const char *p_path, int is_dir, int is_symlink, mode_t mode);

void rel_join(char *out, size_t out_len, const char *rel, const char *name);

void fs_join(char *out, size_t out_len, const char *base, const char *name);

void scan_dir(const char *tool, const char *root_dir, const char *dir, const char *rel, FILE *map, ScanStats *stats);

void manifest_path(char *out, size_t out_len, const char *tool, const char *target);

const char *basename_of(const char *path);

#endif