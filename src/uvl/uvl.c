
// #include <dirent.h>
// #include <errno.h>
// #include <fcntl.h>
// #include <fuse.h>
// #include <openssl/sha.h>
// #include <stdarg.h>
// #include <stddef.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/stat.h>
// #include <sys/types.h>
// #include <sys/wait.h>
// #include <time.h>
// #include <unistd.h>
// #include "utils/log.h"






// static const char *default_entry(const char *tool) {
//     if (strcmp(tool, "bun") == 0) return "node_modules";
//     if (strcmp(tool, "npm") == 0) return "node_modules";
//     if (strcmp(tool, "pnpm") == 0) return "node_modules";
//     if (strcmp(tool, "yarn") == 0) return "node_modules";
//     if (strcmp(tool, "uv") == 0) return ".venv";
//     if (strcmp(tool, "pip") == 0) return ".venv";
//     if (strcmp(tool, "poetry") == 0) return ".venv";
//     return NULL;
// }















static int uvl_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    int res = pread(fi->fh, buf, size, offset);
    return res == -1 ? -errno : res;
}

static int uvl_readlink(const char *path, char *buf, size_t size) {
    Node *n = find_node(path);
    if (!n || !n->is_symlink) return -EINVAL;
    strncpy(buf, n->p_path, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

static int uvl_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    close(fi->fh);
    return 0;
}

static const struct fuse_operations uvl_oper = {
    .getattr = uvl_getattr,
    .readdir = uvl_readdir,
    .open = uvl_open,
    .read = uvl_read,
    .readlink = uvl_readlink,
    .release = uvl_release,
};

static int mount_mode(int argc, char **argv) {
    if (argc < 4) return 1;
    FILE *f = fopen(argv[2], "rb");
    if (!f) die("Failed to open manifest file");

    char magic[4];
    ProjectHeader header;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, UVL_PROJECT_MAGIC, sizeof(magic)) != 0 ||
        fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        die("Failed to read project manifest");
    }

    const char *wanted_entry = path_basename_const(argv[3]);
    uint32_t record_count = 0;
    int found = 0;

    for (uint32_t i = 0; i < header.entry_count; i++) {
        ProjectMeta meta;
        if (fread(&meta, sizeof(meta), 1, f) != 1 ||
            meta.tool_len >= MAX_NAME_LEN ||
            meta.entry_len >= MAX_PATH_LEN) {
            fclose(f);
            die("Failed to read manifest entry");
        }

        char tool[MAX_NAME_LEN];
        char entry[MAX_PATH_LEN];
        if (fread(tool, 1, meta.tool_len, f) != meta.tool_len ||
            fread(entry, 1, meta.entry_len, f) != meta.entry_len) {
            fclose(f);
            die("Failed to read manifest entry data");
        }
        tool[meta.tool_len] = '\0';
        entry[meta.entry_len] = '\0';

        if (strcmp(entry, wanted_entry) == 0) {
            record_count = meta.record_count;
            found = 1;
            break;
        }

        if (skip_manifest_records(f, meta.record_count) != 0) {
            fclose(f);
            die("Failed to skip manifest records");
        }
    }

    if (!found) {
        fclose(f);
        die("Manifest has no matching entry for mount target");
    }

    for (uint32_t i = 0; i < record_count; i++) {
        ProjectRecord record;
        if (fread(&record, sizeof(record), 1, f) != 1 ||
            record.v_len >= MAX_PATH_LEN ||
            record.p_len >= MAX_PATH_LEN) {
            fclose(f);
            die("Failed to read manifest record");
        }

        char v_path[MAX_PATH_LEN];
        char p_path[MAX_PATH_LEN];
        if (fread(v_path, 1, record.v_len, f) != record.v_len ||
            fread(p_path, 1, record.p_len, f) != record.p_len) {
            fclose(f);
            die("Failed to read manifest path data");
        }
        v_path[record.v_len] = '\0';
        p_path[record.p_len] = '\0';
        insert_path(v_path, p_path, record.kind == 1, record.kind == 3, (mode_t)record.mode);
    }
    fclose(f);

    char *fuse_argv[] = {argv[0], argv[3], NULL};
    return fuse_main(2, fuse_argv, &uvl_oper, NULL);
}





// int main(int argc, char **argv) {
//     if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
//         print_help();
//         return 0;
//     }
//     if (strcmp(argv[1], "__mount") == 0) return mount_mode(argc, argv);
//     if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
//         puts("uvl native 0.0.5");
//         return 0;
//     }
//     if (strcmp(argv[1], "--fuse") == 0) {
//         if (argc < 3) die("Usage: uvl --fuse <tool> --mnt <dependency-dir>");
//         const char *entry = NULL;
//         for (int i = 3; i < argc; i++) {
//             if (strcmp(argv[i], "--mnt") == 0 && i + 1 < argc) entry = argv[i + 1];
//         }
//         if (!entry) entry = default_entry(argv[2]);
//         if (!entry) die("Usage: uvl --fuse <tool> --mnt <dependency-dir>");
//         register_tool(argv[2], entry);
//         return 0;
//     }
//     if (strcmp(argv[1], "--fiss") == 0) {
//         if (argc < 3) die("Usage: uvl --fiss <tool>");
//         if (!unregister_tool_config(argv[2])) {
//             fprintf(stderr, "[uvl] No registration found for '%s'\n", argv[2]);
//             return 1;
//         }
//         printf("[uvl] ✅ Removed '%s' from ~/.uvl/config.json\n", argv[2]);
//         return 0;
//     }
//     if (strcmp(argv[1], "--unmnt") == 0) {
//         if (argc < 3) die("Usage: uvl --unmnt <dir>");
//         return unmount_target(argv[2], 0) ? 0 : 1;
//     }
//     if (strcmp(argv[1], "--has") == 0) {
//         if (argc < 3) return 1;
//         Config config;
//         load_config(&config);
//         int ok = find_registration(&config, argv[2]) && command_exists(argv[2]);
//         puts(ok ? "true" : "false");
//         return ok ? 0 : 1;
//     }
//     if (strcmp(argv[1], "--status") == 0) {
//         return print_project_status();
//     }
//     if (strcmp(argv[1], "--list") == 0) {
//         Config config;
//         load_config(&config);
//         for (size_t i = 0; i < config.len; i++) {
//             puts(config.items[i].tool);
//         }
//         return 0;
//     }

//     return run_tool(argv[1], argc - 2, argv + 2);

// }
