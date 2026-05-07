#ifndef UTILS_COMMAND_H
#define UTILS_COMMAND_H

#include <stddef.h>

int command_exists(const char *tool);
int resolve_command(const char *tool, char *out, size_t out_len);

#endif