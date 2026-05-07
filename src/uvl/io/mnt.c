
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "io/mnt.h"
#include "io/file.h"
#include "io/path.h"
#include "utils/logging.h"
#include "utils/constant.h"
#include "utils/types.h"


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
        execlp("fusermount3", "fusermount3", "-u", target, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        pid = fork();
        if (pid == 0) {
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
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execl(self, self, "__mount", manifest, target, NULL);
        _exit(127);
    }
    return wait_for_mount(target);
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
        return 0;
    }

    remove_tree(target);
    mkdir_p(target);

    LOG_INFO("Mounting virtual filesystem with %zu files...", stats.files);
    if (!spawn_mount(self, manifest, target)) {
        LOG_ERROR("Failed to mount virtual '%s'.", target);
        return 0;
    }

    double saved_mb = (double)stats.duplicate_bytes / (1024.0 * 1024.0);
    double virtualized_mb = (double)(stats.stored_bytes + stats.duplicate_bytes) / (1024.0 * 1024.0);
    LOG_INFO("Successfully %s virtual '%s'.", remount ? "remounted" : "mounted", target);
    LOG_INFO("Deduplication saved %.2f MB of physical disk space.", saved_mb);
    LOG_INFO("Virtualized %.2f MB into ~/.uvl/store/%s.", virtualized_mb, tool);
    return 1;
}