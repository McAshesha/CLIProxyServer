#ifndef LOGGER_H
#define LOGGER_H


typedef enum
{
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
}
log_level_t;


// Инициализация логгера (можно задать файл, уровень и т.п.)
void log_init(const char *filename, log_level_t level);

// Основная функция логирования
void log_message(log_level_t level, const char *fmt, ...);


// Удобные макросы
#define LOG_INFO(...)    log_message(LOG_LEVEL_INFO,    __VA_ARGS__)

#define LOG_WARN(...)    log_message(LOG_LEVEL_WARNING, __VA_ARGS__)

#define LOG_ERROR(...)   log_message(LOG_LEVEL_ERROR,   __VA_ARGS__)


#endif // LOGGER_H
