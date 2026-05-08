
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include "utils/command.h"
#include "utils/constant.h"
#include "utils/general.h"
#include "utils/logging.h"

int command_exists(const char *tool) {
    const char *path = getenv("PATH");
    if (!path) return 0;

    char copy[MAX_PATH_LEN * 2];
    snprintf(copy, sizeof(copy), "%s", path);
    for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
        char candidate[MAX_PATH_LEN];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, tool);
        if (access(candidate, X_OK) == 0) return 1;
    }
    return 0;
}

int resolve_command(const char *tool, char *out, size_t out_len) {
    const char *path = getenv("PATH");
    if (!path) return 0;

    char copy[MAX_PATH_LEN * 2];
    snprintf(copy, sizeof(copy), "%s", path);
    for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
        char candidate[MAX_PATH_LEN];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, tool);
        if (access(candidate, X_OK) == 0) {
            snprintf(out, out_len, "%s", candidate);
            return 1;
        }
    }
    return 0;
}

bool match_command(char *text, size_t num_commands, ...){

    va_list args;
    va_start(args, num_commands); 

    for (size_t i = 0; i < num_commands; i++){
        if (streq(text, va_arg(args, char*))) {
            va_end(args); 
            return true;
        }
    }
    va_end(args); 
    return false;

}

ActionTypeArgs resolve_command_action(int argc, char **argv) {
    
    ActionTypeArgs act = {0};

    if (argc < 2 || match_command(argv[1], 2, "--help", "-h")){
        act.type = ACTION_TYPE_HELP;
        return act;
    }

    if (streq(argv[1], "__mount")) {
        act.type = _INNER_ACTION_TYPE_MOUNT_MODE;
        return act;
    }


    if (match_command(argv[1], 2, "--version", "-v")) {
        act.type = ACTION_TYPE_VERSION;
        return act;
    }

    if (streq(argv[1], "--fuse")) {
        if (argc < 3) {LOG_ERROR("Usage: `%s`", FUSE_COMMAND); exit(1);}

        const char *entry = NULL;
        for (int i = 3; i < argc; i++) {
            if (streq(argv[i], "--mnt") && i + 1 < argc) entry = argv[i + 1];
        }
        if (!entry) {LOG_ERROR("Usage: `%s`", FUSE_COMMAND); exit(1);}
        
        act.type = ACTION_TYPE_FUSE;
        act.args[0] = strdup(argv[2]); 
        act.args[1] = strdup(entry); 

        return act;
    }

    if (streq(argv[1], "--fiss")) {
        if (argc < 3) {LOG_ERROR("Usage: `%s`", FISS_COMMAND); exit(1);}

        act.type = ACTION_TYPE_FISS;
        act.args[0] = strdup(argv[2]); 
        
        return act;
    }

    if (streq(argv[1], "--unmnt")) {
        if (argc < 3) {LOG_ERROR("Usage: `%s`", UNMNT_COMMAND); exit(1);}

        act.type = ACTION_TYPE_UNMNT;
        act.args[0] = strdup(argv[2]); 
        return act; 
    }

    if (streq(argv[1], "--has")) {
        if (argc < 3) {LOG_ERROR("Usage: `%s`", HAS_COMMAND); exit(1);}

        act.type = ACTION_TYPE_HAS;
        act.args[0] = strdup(argv[2]); 
        return act;
    }

    if (streq(argv[1], "--stat")) {
        act.type = ACTION_TYPE_STATUS;
        return act;
    }

    if (match_command(argv[1], 2, "--list", "-l")) {
        act.type = ACTION_TYPE_LIST;
        return act;
    }

    if (streq(slice_string(argv[1], 0, 2), "--") || argv[1][0] == '-') {
        LOG_ERROR("Not found the provided command argument.");
        act.type = ACTION_TYPE_HELP;
        return act;
    }

    act.type = ACTION_TYPE_CALL;
    act.args[0] = strdup(argv[1]); 
    return act;
}