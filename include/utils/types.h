#ifndef UTILS_TYPES_H
#define UTILS_TYPES_H
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "utils/constant.h"


typedef enum {
    ACTION_TYPE_HELP, 
    ACTION_TYPE_VERSION,
    ACTION_TYPE_FUSE, 
    ACTION_TYPE_FISS,
    ACTION_TYPE_STATUS, 
    ACTION_TYPE_LIST,
    ACTION_TYPE_CALL, 
    ACTION_TYPE_HAS,
    ACTION_TYPE_UNMNT
} ActionType;

typedef struct {
    ActionType type;
    char *args[2];
} ActionTypeArgs;

typedef struct {
    char tool[MAX_NAME_LEN];
    char entry[MAX_PATH_LEN];
} Registration;

typedef struct {
    Registration items[MAX_REGISTRATIONS];
    size_t len;
} Config;

typedef struct {
    uint32_t entry_count;
} __attribute__((packed)) ProjectHeader;

typedef struct {
    uint32_t tool_len;
    uint32_t entry_len;
    uint32_t record_count;
} __attribute__((packed)) ProjectMeta;

typedef struct {
    uint8_t kind;
    uint32_t mode;
    uint32_t v_len;
    uint32_t p_len;
} __attribute__((packed)) ProjectRecord;

typedef struct Node {
    char name[MAX_NAME_LEN];
    int is_dir;
    int is_symlink;
    mode_t mode;
    char p_path[MAX_PATH_LEN];
    struct Node *child;
    struct Node *next;
} Node;

typedef struct {
    size_t files;
    uint64_t stored_bytes;
    uint64_t duplicate_bytes;
} ScanStats;

typedef struct {
    uint32_t tool_len;
    uint32_t entry_len;
    uint32_t record_count;
} __attribute__((packed)) MapMeta;

typedef struct {
    uint8_t kind;
    uint32_t mode;
    uint32_t v_len;
    uint32_t p_len;
} __attribute__((packed)) MapRecord;


#endif