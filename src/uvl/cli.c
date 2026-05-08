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
#include "utils/command.h"

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
                exit(1);
            }

            register_tool(act.args[0], act.args[1]);
            break; 
        
        case ACTION_TYPE_FISS:

            if (!unregister_tool_config(act.args[0])) {
                LOG_ERROR("No fused found for '%s'", act.args[0]);
                exit(1);
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

            return 0;

        case ACTION_TYPE_CALL:
            break;

        case _INNER_ACTION_TYPE_MOUNT_MODE:
            return mount_mode(argc, argv);

        case ACTION_TYPE_HAS:
            load_config(&config);
            int ok = find_registration(&config, act.args[0]) && command_exists(act.args[0]);
            puts(ok ? "true" : "false");
            break;

        case ACTION_TYPE_UNMNT:
            unmount_target(act.args[0], 0);
            break;    

        default:
            print_help();
            break;
    }

    if (act.type != ACTION_TYPE_CALL){
        return 0;
    }

    return run_tool(act.args[0], argc - 2, argv + 2);

}
