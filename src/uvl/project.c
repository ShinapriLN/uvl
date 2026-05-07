

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>


#include "utils/types.h"
#include "uvl/project.h"
#include "io/file.h"
#include "io/mnt.h"


int print_project_status(void) {
    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) return 1;

    char manifest[MAX_PATH_LEN];
    snprintf(manifest, sizeof(manifest), "%s/.uvl", cwd);

    FILE *f = fopen(manifest, "rb");
    if (!f) {
        puts("Project status: not initialized");
        puts("No .uvl manifest exists in this directory.");
        puts("Run a registered tool through uvl to create one.");
        return 0;
    }

    char magic[4];
    ProjectHeader header;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, UVL_PROJECT_MAGIC, sizeof(magic)) != 0 ||
        fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        printf("Project status: invalid\nManifest: %s\n", manifest);
        return 0;
    }

    printf("Project status:\n");
    printf("  manifest: %s\n", manifest);
    for (uint32_t i = 0; i < header.entry_count; i++) {
        ProjectMeta meta;
        if (fread(&meta, sizeof(meta), 1, f) != 1 ||
            meta.tool_len >= MAX_NAME_LEN ||
            meta.entry_len >= MAX_PATH_LEN) {
            fclose(f);
            printf("Project status: invalid\nManifest: %s\n", manifest);
            return 0;
        }

        char tool[MAX_NAME_LEN];
        char entry[MAX_PATH_LEN];
        if (fread(tool, 1, meta.tool_len, f) != meta.tool_len ||
            fread(entry, 1, meta.entry_len, f) != meta.entry_len) {
            fclose(f);
            printf("Project status: invalid\nManifest: %s\n", manifest);
            return 0;
        }
        tool[meta.tool_len] = '\0';
        entry[meta.entry_len] = '\0';

        if (skip_manifest_records(f, meta.record_count) != 0) {
            fclose(f);
            printf("Project status: invalid\nManifest: %s\n", manifest);
            return 0;
        }

        char target[MAX_PATH_LEN];
        snprintf(target, sizeof(target), "%s/%s", cwd, entry);
        printf("  %s:\n", tool);
        printf("    mount: %s\n", entry);
        printf("    store: ~/.uvl/store/%s\n", tool);
        printf("    records: %u\n", meta.record_count);
        printf("    mounted: %s\n", is_mountpoint(target) ? "yes" : "no");
    }
    fclose(f);
    return 0;
}



const char *home_dir(void) {
    const char *home = getenv("HOME");
    return home && home[0] ? home : ".";
}



void uvl_home(char *out, size_t out_len) {
    snprintf(out, out_len, "%s/.uvl", home_dir());
}



void store_path(char *out, size_t out_len, const char *tool) {
    snprintf(out, out_len, "%s/.uvl/store/%s", home_dir(), tool);
}

