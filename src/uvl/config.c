
#include <stdlib.h>
#include <stdio.h>

#include "uvl/config.h"
#include "uvl/project.h"
#include "utils/types.h"
#include "utils/logging.h"
#include "io/file.h"
#include "io/path.h"

void load_config(Config *config) {
    memset(config, 0, sizeof(*config));
    char path[MAX_PATH_LEN];
    config_path(path, sizeof(path));
    char *text = read_file(path);
    if (!text) return;

    char *cursor = text;
    while ((cursor = strstr(cursor, "\"entry\"")) && config->len < MAX_REGISTRATIONS) {
        char *scan = cursor;
        char *tool_start = NULL;
        while (scan > text) {
            scan--;
            if (*scan == '"') {
                char *maybe_start = scan - 1;
                while (maybe_start > text && *maybe_start != '"') maybe_start--;
                if (*maybe_start == '"') {
                    tool_start = maybe_start + 1;
                    break;
                }
            }
        }
        char *entry_colon = strchr(cursor, ':');
        char *entry_quote = entry_colon ? strchr(entry_colon, '"') : NULL;
        char *entry_end = entry_quote ? strchr(entry_quote + 1, '"') : NULL;
        if (tool_start && entry_quote && entry_end) {
            char *tool_end = strchr(tool_start, '"');
            size_t tool_len = tool_end ? (size_t)(tool_end - tool_start) : 0;
            size_t entry_len = (size_t)(entry_end - entry_quote - 1);
            if (tool_len > 0 && tool_len < MAX_NAME_LEN && entry_len < MAX_PATH_LEN) {
                Registration *reg = &config->items[config->len++];
                memcpy(reg->tool, tool_start, tool_len);
                reg->tool[tool_len] = '\0';
                memcpy(reg->entry, entry_quote + 1, entry_len);
                reg->entry[entry_len] = '\0';
            }
        }
        cursor = entry_end ? entry_end + 1 : cursor + 7;
    }
    free(text);
}

void config_path(char *out, size_t out_len) {
    char base[MAX_PATH_LEN];
    uvl_home(base, sizeof(base));
    path_join(out, out_len, base, "config.json");
}

void save_config(Config *config) {
    char base[MAX_PATH_LEN];
    uvl_home(base, sizeof(base));
    if (mkdir_p(base) != 0) LOG_ERROR("Could not create %s", base);

    char path[MAX_PATH_LEN];
    config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) LOG_ERROR("Could not write %s", path);

    fputs("{\n  \"registrations\": {\n", f);
    for (size_t i = 0; i < config->len; i++) {
        fprintf(
            f,
            "    \"%s\": {\"entry\": \"%s\"}%s\n",
            config->items[i].tool,
            config->items[i].entry,
            i + 1 == config->len ? "" : ","
        );
    }
    fputs("  }\n}\n", f);
    fclose(f);
}