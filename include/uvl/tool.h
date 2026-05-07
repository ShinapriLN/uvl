

#ifndef UVL_TOOL_H
#define UVL_TOOL_H
#include "utils/types.h"


void register_tool(const char *tool, const char *entry);

int run_tool(const char *tool, int argc, char **argv);

Registration *find_registration(Config *config, const char *tool);

int unregister_tool_config(const char *tool);

int ensure_store(const char *tool);

#endif
