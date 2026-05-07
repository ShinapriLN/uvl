#ifndef UTILS_GENERAL_H
#define UTILS_GENERAL_H
#include <stdbool.h>

bool streq(const char *str1, const char *str2);

void slice_string(char *str, size_t i, size_t j);

char *replace_string(char *str, char *sub_str);

#endif