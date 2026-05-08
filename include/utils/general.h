#ifndef UTILS_GENERAL_H
#define UTILS_GENERAL_H
#include <stdbool.h>

bool streq(const char *str1, const char *str2);

char *slice_string(const char *str, const size_t i, const size_t j);

char *concat_string(const char *str1, const char *str2);

char *replace_string(const char *str, const char *match, const char *replace);

#endif