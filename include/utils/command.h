#ifndef UTILS_COMMAND_H
#define UTILS_COMMAND_H

#include <stddef.h>
#include "utils/types.h"
#include <stdbool.h>

int command_exists(const char *tool);
int resolve_command(const char *tool, char *out, size_t out_len);
bool match_command(char *text, size_t num_commands, ...);
ActionTypeArgs resolve_command_action(int argc, char **argv);

#endif