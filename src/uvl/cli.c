#include <stdio.h>
#include <stdlib.h>

#include "utils/logging.h"
#include "utils/general.h"
#include "utils/types.h"
#include "utils/constant.h"
#include "io/mnt.h"
#include "uvl/tool.h"
#include "uvl/config.h"
#include "uvl/project.h"

#include "utils/general.h"



ActionTypeArgs resolve_command_action(int argc, char **argv) {
    
    ActionTypeArgs act = {0};

    if (argc < 2 || streq(argv[1], "--help") || streq(argv[1], "-h")) {
        act.type = ACTION_TYPE_HELP;
        return act;
    }

    // if (streq(argv[1], "__mount")) {
    //     *cmd_action = ACTION_TYPE_FUSE;
    //     return cmd_action;
    // }

    if (streq(argv[1], "--version")|| streq(argv[1], "-v")) {
        
        act.type = ACTION_TYPE_VERSION;
        return act;
    }

    if (streq(argv[1], "--fuse")) {
        if (argc < 3) LOG_ERROR("Usage: `%s`", FUSE_COMMAND);

        const char *entry = NULL;
        for (int i = 3; i < argc; i++) {
            if (streq(argv[i], "--mnt") && i + 1 < argc) entry = argv[i + 1];
        }
        if (!entry) LOG_ERROR("Usage: `%s`", FUSE_COMMAND);
        
        act.type = ACTION_TYPE_FUSE;
        act.args[0] = strdup(argv[2]); 
        act.args[1] = strdup(entry); 

        return act;
    }

    if (streq(argv[1], "--fiss")) {
        if (argc < 3) LOG_ERROR("Usage: `%s`", FISS_COMMAND);

        act.type = ACTION_TYPE_FISS;
        act.args[0] = strdup(argv[2]); 
        
        return act;
    }

    if (streq(argv[1], "--unmnt")) {
        if (argc < 3) LOG_ERROR("Usage: `%s`", UNMNT_COMMAND);

        act.type = ACTION_TYPE_UNMNT;
        act.args[0] = strdup(argv[2]); 
        return act; 
    }

    if (streq(argv[1], "--has")) {
        if (argc < 3) LOG_ERROR("Usage: `%s`", HAS_COMMAND);
        // Config config;
        // load_config(&config);
        // int ok = find_registration(&config, argv[2]) && command_exists(argv[2]);
        // puts(ok ? "true" : "false");
        act.type = ACTION_TYPE_HAS;
        act.args[0] = strdup(argv[2]); 
        return act;
    }

    if (streq(argv[1], "--status")) {
        act.type = ACTION_TYPE_STATUS;
        return act;
    }

    if (streq(argv[1], "--list")) {
        act.type = ACTION_TYPE_LIST;
        return act;
    }

    act.type = ACTION_TYPE_HELP;
    return act;
}

int main(int argc, char **argv) {
    ActionTypeArgs act = resolve_command_action(argc, argv);

    switch(act.type){

        case ACTION_TYPE_HELP:
            print_help();
            break; 

        case ACTION_TYPE_VERSION:
            puts(UVL_VERSION);
            break;

        case ACTION_TYPE_FUSE:

            if (!act.args[0] || 
                !act.args[1] || 
                act.args[0][0] == '\0' || 
                act.args[1][0] == '\0') {
                LOG_ERROR("Something went wrong on fused tool or mounted directory.");
            }

            register_tool(act.args[0], act.args[1]);
            break; 
        
        case ACTION_TYPE_FISS:

            if (!unregister_tool_config(act.args[0])) {
                LOG_ERROR("No fused found for '%s'", act.args[0]);
            }

            LOG_INFO("Removed '%s' from uvl", act.args[0]);
            break;

        case ACTION_TYPE_STATUS:
            print_project_status();
            break; 
        case ACTION_TYPE_LIST:

            Config config;
            load_config(&config);

            for (size_t i = 0; i < config.len; i++) {
                puts(config.items[i].tool);
            }

            break;
        case ACTION_TYPE_CALL:
            break; 
        case ACTION_TYPE_HAS:
            break;

        case ACTION_TYPE_UNMNT:
            unmount_target(act.args[0], 0);
            break;    

        default:
            break;
        
    }
    return run_tool(argv[1], argc - 2, argv + 2);

}
