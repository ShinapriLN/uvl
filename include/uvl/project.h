

#ifndef UVL_PROJECT_H
#define UVL_PROJECT_H
#include "utils/types.h"

int print_project_status(void);

const char *home_dir(void);

void uvl_home(char *out, size_t out_len);

void store_path(char *out, size_t out_len, const char *tool);

#endif


