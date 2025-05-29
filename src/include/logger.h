#ifndef LOGGER_H
#define LOGGER_H

typedef enum log_level
{
    INFO,
    WARNING,
    ERROR
} log_level_t;


void       log_init(const char *filename, log_level_t level);

void       log_message(log_level_t level, const char *fmt, ...);

void       extra_log_message(log_level_t level, const char *fmt, ...);


#define    LOG_INFO(...)         log_message(INFO,    __VA_ARGS__)

#define    LOG_WARN(...)         log_message(WARNING, __VA_ARGS__)

#define    LOG_ERROR(...)        log_message(ERROR,   __VA_ARGS__)

#define    EXTRA_LOG_WARN(...)   extra_log_message(WARNING, __VA_ARGS__)

#define    EXTRA_LOG_ERROR(...)  extra_log_message(ERROR,   __VA_ARGS__)

#endif // LOGGER_H
