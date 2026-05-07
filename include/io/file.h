#ifndef IO_FILE_H
#define IO_FILE_H
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>

#include "utils/types.h"

char *read_file(const char *path);

int skip_manifest_records(FILE *f, uint32_t record_count);

int restore_project_manifest(const char *manifest, const char *target);

int remove_tree(const char *path);

int copy_file(const char *src, const char *dst, mode_t mode);

int move_file(const char *src, const char *dst, mode_t mode);

int build_mapping(const char *tool, const char *target, char *manifest, size_t manifest_len, ScanStats *stats);

void sha256_file(const char *path, char hex[65]);

void escape(FILE *f, const char *s);

void write_map(FILE *f, const char *kind, const char *vpath, const char *ppath, mode_t mode);

#endif