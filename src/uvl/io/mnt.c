

#include "utils/constant.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>

#include "io/mnt.h"
#include "io/file.h"
#include "io/path.h"
#include "utils/logging.h"
#include "utils/types.h"
#include "utils/node.h"

static const struct fuse_operations uvl_oper = {
    .getattr = uvl_getattr,
    .readdir = uvl_readdir,
    .open = uvl_open,
    .read = uvl_read,
    .readlink = uvl_readlink,
    .release = uvl_release,
};

int is_mountpoint(const char *path) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("mountpoint", "mountpoint", "-q", path, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int unmount_target(const char *target, int quiet) {
    pid_t pid = fork();
    if (pid == 0) {
        if (quiet) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        execlp("fusermount3", "fusermount3", "-u", target, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        pid = fork();
        if (pid == 0) {
            if (quiet) {
                int devnull = open("/dev/null", O_RDWR);
                if (devnull >= 0) {
                    dup2(devnull, STDOUT_FILENO);
                    dup2(devnull, STDERR_FILENO);
                    close(devnull);
                }
            }
            execlp("fusermount3", "fusermount3", "-uz", target, NULL);
            _exit(127);
        }
        waitpid(pid, &status, 0);
        ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (!quiet) {
        if (ok) {
            LOG_INFO("Unmounted %s. You can now delete or modify anything inside.", target);
        }else {
            LOG_ERROR("Could not unmount %s.", target);
            exit(1);
        }
    }
    return ok;
}


int wait_for_mount(const char *target) {
    for (int i = 0; i < 40; i++) {
        if (is_mountpoint(target)) return 1;
        usleep(50000);
    }
    return is_mountpoint(target);
}

int spawn_mount(const char *self, const char *manifest, const char *target) {
    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        LOG_ERROR("Failed to create mount error pipe: %s.", strerror(errno));
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(err_pipe[0]);
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[1]);
        execl(self, self, "__mount", manifest, target, NULL);
        dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
        _exit(127);
    }
    close(err_pipe[1]);

    int mounted = wait_for_mount(target);
    if (!mounted) {
        char buffer[1024];
        ssize_t n = read(err_pipe[0], buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            buffer[strcspn(buffer, "\n")] = '\0';
            LOG_ERROR("Native mount error: %s.", buffer);
        }
    }
    close(err_pipe[0]);
    return mounted;
}

int create_virtual_fs(const char *self, const char *tool, const char *target, int remount) {
    struct stat st;
    if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    if (is_mountpoint(target)) return 0;

    if (remount) {
        LOG_INFO("Re-virtualizing '%s' after command completed.", target);
    } else {
        LOG_INFO("Taking control of '%s'.", target);
    }

    char manifest[MAX_PATH_LEN];
    ScanStats stats;
    if (build_mapping(tool, target, manifest, sizeof(manifest), &stats) != 0) {
        LOG_ERROR("Failed to build manifest for %s.", target);
        exit(1);
    }

    remove_tree(target);
    mkdir_p(target);

    LOG_INFO("Mounting virtual filesystem with %zu files...", stats.files);
    if (!spawn_mount(self, manifest, target)) {
        LOG_WARN("Mount failed; restoring normal directory from .uvl manifest.");
        if (restore_project_manifest(manifest, target) != 0) {
            LOG_ERROR("Failed to restore '%s' from .uvl manifest.", target);
            exit(1);
        }
        LOG_ERROR("Failed to mount virtual '%s'.", target);
        exit(1);
    }

    double saved_mb = (double)stats.duplicate_bytes / (1024.0 * 1024.0);
    double virtualized_mb = (double)(stats.stored_bytes + stats.duplicate_bytes) / (1024.0 * 1024.0);
    LOG_INFO("Successfully %s virtual '%s'.", remount ? "remounted" : "mounted", target);
    LOG_INFO("Deduplication saved %.2f MB of physical disk space.", saved_mb);
    LOG_INFO("Virtualized %.2f MB into ~/.uvl/store/%s.", virtualized_mb, tool);
    return 0;
}

int mount_mode(int argc, char **argv) {
    if (argc < 4) return 1;
    FILE *f = fopen(argv[2], "rb");
    if (!f) LOG_ERROR("Failed to open manifest file.");

    char magic[4];
    ProjectHeader header;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        memcmp(magic, UVL_PROJECT_MAGIC, sizeof(magic)) != 0 ||
        fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        LOG_ERROR("Failed to read project manifest.");
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
            LOG_ERROR("Failed to read manifest entry");
        }

        char tool[MAX_NAME_LEN];
        char entry[MAX_PATH_LEN];
        if (fread(tool, 1, meta.tool_len, f) != meta.tool_len ||
            fread(entry, 1, meta.entry_len, f) != meta.entry_len) {
            fclose(f);
            LOG_ERROR("Failed to read manifest entry data.");
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
            LOG_ERROR("Failed to skip manifest records.");
        }
    }

    if (!found) {
        fclose(f);
        LOG_ERROR("Manifest has no matching entry for mount target.");
    }

    for (uint32_t i = 0; i < record_count; i++) {
        ProjectRecord record;
        if (fread(&record, sizeof(record), 1, f) != 1 ||
            record.v_len >= MAX_PATH_LEN ||
            record.p_len >= MAX_PATH_LEN) {
            fclose(f);
            LOG_ERROR("Failed to read manifest record.");
        }

        char v_path[MAX_PATH_LEN];
        char p_path[MAX_PATH_LEN];
        if (fread(v_path, 1, record.v_len, f) != record.v_len ||
            fread(p_path, 1, record.p_len, f) != record.p_len) {
            fclose(f);
            LOG_ERROR("Failed to read manifest path data.");
        }
        v_path[record.v_len] = '\0';
        p_path[record.p_len] = '\0';
        insert_path(v_path, p_path, record.kind == 1, record.kind == 3, (mode_t)record.mode);
    }
    fclose(f);

    char *fuse_argv[] = {argv[0], argv[3], NULL};
    return fuse_main(2, fuse_argv, &uvl_oper, NULL);
}
