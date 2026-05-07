#ifndef UTILS_LOG_H
#define UTILS_LOG_H

#include <string.h>
#include <stdio.h>

#include "utils/general.h"

#define CLR_RESET  "\x1b[0m"
#define CLR_UVL    "\x1b[90m" // Gray
#define CLR_DEBUG  "\x1b[36m" // Cyan
#define CLR_INFO   "\x1b[32m" // Green
#define CLR_WARN   "\x1b[33m" // Yellow
#define CLR_ERROR  "\x1b[31m" // Red
#define LOG_INFO(fmt, ...) \
    { char buf[1240]; sprintf(buf, fmt, ##__VA_ARGS__); log_message("INFO", CLR_INFO, buf); }
#define LOG_DEBUG(fmt, ...) \
    { char buf[1240]; sprintf(buf, fmt, ##__VA_ARGS__); log_message("DEBUG", CLR_DEBUG, buf); }
#define LOG_WARN(fmt, ...) \
    { char buf[1240]; sprintf(buf, fmt, ##__VA_ARGS__); log_message("WARN", CLR_WARN, buf); }
#define LOG_ERROR(fmt, ...) \
    { char buf[1240]; sprintf(buf, fmt, ##__VA_ARGS__); log_message("ERROR", CLR_ERROR, buf); }

#define FUSE_COMMAND    "uvl --fuse <tool> --mnt <dir>"
#define FISS_COMMAND    "uvl --fiss <tool>"
#define UNMNT_COMMAND   "uvl --unmnt <dir>"
#define HAS_COMMAND     "uvl --has <tool>"

void log_message(const char* level, const char* color, const char* message);

void print_help(void);

#endif