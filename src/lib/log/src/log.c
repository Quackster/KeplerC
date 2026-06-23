#include "log.h"

#include <stdio.h>
#include <time.h>

static int log_level = LOG_TRACE;

static const char *level_strings[] = {
        "TRACE",
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
        "FATAL"
};

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {
        "\x1b[94m",
        "\x1b[36m",
        "\x1b[32m",
        "\x1b[33m",
        "\x1b[31m",
        "\x1b[35m"
};
#endif

void log_set_level(int level)
{
    log_level = level;
}

void log_log(int level, const char *file, int line, const char *fmt, ...)
{
    if (level < log_level) {
        return;
    }

    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    char time_buffer[16] = {0};

    if (local_time != NULL) {
        strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", local_time);
    }

#ifdef LOG_USE_COLOR
    fprintf(stderr, "%s %s%-5s\x1b[0m %s:%d: ", time_buffer, level_colors[level], level_strings[level], file, line);
#else
    fprintf(stderr, "%s %-5s %s:%d: ", time_buffer, level_strings[level], file, line);
#endif

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
    fflush(stderr);
}
