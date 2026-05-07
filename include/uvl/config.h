

#ifndef UVL_CONFIG_H
#define UVL_CONFIG_H
#include "utils/types.h"


void load_config(Config *config);
void config_path(char *out, size_t out_len);
void save_config(Config *config);



#endif


