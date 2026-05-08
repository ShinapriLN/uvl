
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "utils/general.h"

bool streq(const char *str1, const char *str2){
    if (strcmp(str1, str2) == 0){
        return true;
    }
    return false;
}

char *slice_string(const char *str, const size_t i, const size_t j){

    if (j <= i) {
        return NULL;
    }

    size_t len = j - i;

    char *out = malloc(len + 1);
    if (!out) return NULL;

    memcpy(out, str + i, len);
    out[len] = '\0';

    return out;
}

char *concat_string(const char *str1, const char *str2) {
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);

    char *out = malloc(len1 + len2 + 1);
    if (!out) return NULL;

    memcpy(out, str1, len1);
    memcpy(out + len1, str2, len2 + 1);

    return out;
}

char *replace_string(const char *str, const char *match, const char *replace) {
    if (!str || !match || !replace) return NULL;

    size_t str_len = strlen(str);
    size_t match_len = strlen(match);
    size_t replace_len = strlen(replace);

    if (match_len == 0) {
        char *out = malloc(str_len + 1);
        if (!out) return NULL;
        memcpy(out, str, str_len + 1);
        return out;
    }

    size_t count = 0;
    const char *p = str;

    while ((p = strstr(p, match)) != NULL) {
        count++;
        p += match_len;
    }

    size_t out_len = str_len + count * (replace_len - match_len);
    char *out = malloc(out_len + 1);
    if (!out) return NULL;

    const char *src = str;
    char *dst = out;

    while ((p = strstr(src, match)) != NULL) {
        size_t n = (size_t)(p - src);

        memcpy(dst, src, n);
        dst += n;

        memcpy(dst, replace, replace_len);
        dst += replace_len;

        src = p + match_len;
    }

    strcpy(dst, src);

    return out;
}

