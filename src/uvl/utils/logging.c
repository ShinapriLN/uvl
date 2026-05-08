
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "utils/logging.h"
#include "utils/general.h"

void log_message(const char* level, const char* color, const char* message) {

    printf("%s[UVL]%s %s[ %s ]%s %s\n",
           CLR_UVL, CLR_RESET,
           color, level, CLR_RESET,
           message);

}

void print_help(void) {
    puts("uvl: virtual dependency directories backed by a shared FUSE store");
    puts("");
    puts("Usage:");
    puts("  uvl --fuse <tool> --mnt <dir>   Register/fustion a tool mount directory");
    puts("  uvl --fiss <tool>               Remove/fission a tool out of uvl");
    puts("  uvl <tool> [args...]            Run the tool through uvl");
    puts("  uvl --unmnt <dir>               Unmount a virtualized dependency directory");
    puts("  uvl --stat                      Show current project mount status");
    puts("  uvl --list, -l                  List fused tools");
    puts("  uvl --has <tool>                Check whether a tool is fused with uvl");
    puts("  uvl --version, -v               Print version");
}