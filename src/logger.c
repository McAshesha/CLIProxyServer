#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>


static FILE         *log_file      = NULL;       // Целевой файл для записи логов
static log_level_t   current_level = INFO;       // Минимальный выводимый уровень


/*
 * Инициализация: если filename есть — пытаемся открыть файл на дозапись,
 * иначе — stdout. Если файл не открывается — падаем на stdout.
 */
void log_init(const char *filename, log_level_t level)
{
    current_level = level;
    if (filename != NULL && strcmp(filename, "") != 0)
    {
        log_file = fopen(filename, "a");
        if (!log_file)
        {
            // Невозможно открыть файл — переключаемся на стандартный вывод
            log_file = stdout;
        }
    }
    else
    {
        // По умолчанию выводим в консоль
        log_file = stdout;
    }
}

/*
 * Базовое логирование: чекаем уровень, формируем хедер и шлём сообщение
 */
void log_message(log_level_t level, const char *fmt, ...)
{
    if (level < current_level)
    {
        return; // Уровень ниже текущего — пропускаем
    }

    // Получаем текущее локальное время
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    // Формируем строку времени [YYYY-MM-DD HH:MM:SS]
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    // Метки уровней
    static const char *labels[] = {"INFO", "WARNING", "ERROR"};
    char header[64];
    snprintf(header, sizeof(header), "[%s] [%s] ", timestamp, labels[level]);

    // Формируем само сообщение, разбивая по символам '\n'
    char    msgbuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
    va_end(args);

    // Печать построчно: для многострочных сообщений сохраняем структуру
    char *start = msgbuf;
    char *pos;
    while ((pos = strchr(start, '\n')) != NULL)
    {
        *pos = '\0';
        fprintf(log_file, "%s%s\n", header, start);
        start = pos + 1;
    }
    if (*start)
    {
        fprintf(log_file, "%s%s\n", header, start);
    }

    fflush(log_file);
}

/*
 * Доп. лог: кроме файла, дублим WARNING/ERROR в stdout
 */
void extra_log_message(log_level_t level, const char *fmt, ...)
{
    // Только WARNING или выше, и не ниже текущего уровня
    if (level < current_level || level < WARNING)
    {
        return;
    }

    // Пустая строка перед важными сообщениями для наглядности
    fprintf(stdout, "\n");

    // Логика очень похожа на log_message
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    static const char *labels[] = {"INFO", "WARNING", "ERROR"};
    char header[64];
    snprintf(header, sizeof(header), "[%s] [%s] ", timestamp, labels[level]);

    char    msgbuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
    va_end(args);

    char *start = msgbuf;
    char *pos;
    while ((pos = strchr(start, '\n')) != NULL)
    {
        *pos = '\0';
        // Запись и в файл
        fprintf(log_file, "%s%s\n", header, start);
        // Дублируем в stdout, если файл не stdout
        if (log_file != stdout)
        {
            fprintf(stdout, "%s%s\n", header, start);
        }
        start = pos + 1;
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
