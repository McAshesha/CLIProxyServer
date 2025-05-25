#include "include/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // оценим длину и выделим буфер
    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char *buf = malloc(needed + 1);
    va_start(args, format);
    vsnprintf(buf, needed + 1, format, args);
    va_end(args);

    char *line = buf;
    char *nl;
    while ((nl = strchr(line, '\n')))
    {
        *nl = '\0';
        fprintf(stdout, "[INFO] %s\n", line);
        line = nl + 1;
    }
    // остаток после последнего '\n'
    if (*line)
        fprintf(stdout, "[INFO] %s\n", line);

    free(buf);
}


void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}
