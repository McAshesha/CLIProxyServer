#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE *log_file = NULL;
static log_level_t current_level = LOG_LEVEL_INFO;

void log_init(const char *filename, log_level_t level)
{
    current_level = level;
    if (filename != NULL && strcmp(filename, "") != 0)
    {
        log_file = fopen(filename, "a");
        if (!log_file)
        {
            log_file = stdout;
        }
    }
    else
    {
        log_file = stdout;
    }
}

void log_message(log_level_t level, const char *fmt, ...)
{
    if (level < current_level)
    {
        return;
    }

    // Формируем временную метку
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    static const char *labels[] = {"INFO", "WARNING", "ERROR"};
    char header[64];
    snprintf(header, sizeof(header), "[%s] [%s] ", timestamp, labels[level]);

    // Форматируем само сообщение во временный буфер
    char msgbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    // Разбиваем по '\n' и выводим каждую строку с префиксом
    char *start = msgbuf;
    char *newline;
    while ((newline = strchr(start, '\n')) != NULL)
    {
        *newline = '\0';
        fprintf(log_file, "%s%s\n", header, start);
        start = newline + 1;
    }
    if (*start)
    {
        fprintf(log_file, "%s%s\n", header, start);
    }

    fflush(log_file);
}

void extra_log_message(log_level_t level, const char *fmt, ...)
{
    if (level < current_level || level < LOG_LEVEL_WARNING)
    {
        return;
    }

    fprintf(stdout, "\n");

    // Формируем временную метку
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    static const char *labels[] = {"INFO", "WARNING", "ERROR"};
    char header[64];
    snprintf(header, sizeof(header), "[%s] [%s] ", timestamp, labels[level]);

    // Форматируем само сообщение во временный буфер
    char msgbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    // Разбиваем по '\n' и выводим каждую строку с префиксом
    char *start = msgbuf;
    char *newline;
    while ((newline = strchr(start, '\n')) != NULL)
    {
        *newline = '\0';
        fprintf(log_file, "%s%s\n", header, start);
        if (log_file != stdout)
        {
            fprintf(stdout, "%s%s\n", header, start);
        }
        start = newline + 1;
    }
    if (*start)
    {
        fprintf(log_file, "%s%s\n", header, start);
        if (log_file != stdout)
        {
            fprintf(stdout, "%s%s\n", header, start);
        }
    }

    fflush(log_file);
}

