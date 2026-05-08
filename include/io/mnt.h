#ifndef IO_MNT_H
#define IO_MNT_H




int is_mountpoint(const char *path);

int unmount_target(const char *target, int quiet);



int wait_for_mount(const char *target);

int spawn_mount(const char *self, const char *manifest, const char *target);

int create_virtual_fs(const char *self, const char *tool, const char *target, int remount);

int mount_mode(int argc, char **argv);

const char *mount_manifest_path(void);

const char *mount_tool_name(void);

const char *mount_entry_name(void);

#endif
