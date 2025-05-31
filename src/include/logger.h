#ifndef LOGGER_H
#define LOGGER_H



/*
 * Логи для прокачки детализации вывода
 */
typedef enum log_level
{
    INFO,    // Инфа-сообщения
    WARNING, // Варнинги про возможные косяки
    ERROR    // Ошибки, на которые надо забить внимание
} log_level_t;

/*
 * Инициализация логгера: открытие файла (append) или stdout, установка минимального уровня
 */
void log_init(const char *filename, log_level_t level);

/*
 * Запуск логгера: открываем файл (append) или stdout, задаём минимальный уровень
 */
void log_message(log_level_t level, const char *fmt, ...);

/*
 * Доп. логирование: кроме файла, дублирует WARNING/ERROR в stdout
 */
void extra_log_message(log_level_t level, const char *fmt, ...);

/*
 * Макросы для вызова с уровнем без прямого вызова log\_message
 */
#define LOG_INFO(...)         log_message(INFO,    __VA_ARGS__)

#define LOG_WARN(...)         log_message(WARNING, __VA_ARGS__)

#define LOG_ERROR(...)        log_message(ERROR,   __VA_ARGS__)

#define EXTRA_LOG_WARN(...)   extra_log_message(WARNING, __VA_ARGS__)

#define EXTRA_LOG_ERROR(...)  extra_log_message(ERROR,   __VA_ARGS__)

#endif // LOGGER_H
