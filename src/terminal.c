#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "terminal.h"
#include "logger.h"

/*
 * Флаг режима «freeze» для приостановки пересылки трафика.
 */
static volatile sig_atomic_t freeze_flag = 0;

/*
 * Функция-обработчик фонового потока терминала.
 * Читает команды из stdin и реагирует следующим образом:
 *   • "freeze" — переключает состояние freeze_flag и выводит предупреждение
 *   • "stop"   — выводит предупреждение и генерирует SIGINT для graceful shutdown
 *   • остальное — выводит предупреждение об неизвестной команде
*/
static void *terminal_thread(void *arg)
{
    char line[64];
    (void)arg;  // чтобы не ругался компилятор на неиспользуемый параметр

    // Постоянно читаем строки из консоли
    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        // Удаляем символы новой строки и возврата каретки
        line[strcspn(line, "\r\n")] = '\0';

        if (strcmp(line, "freeze") == 0)
        {
            // Меняем режим заморозки: если был включён — выключаем, и наоборот
            freeze_flag = !freeze_flag;
            EXTRA_LOG_WARN("Terminal → freeze %s",
                           freeze_flag ? "ON" : "OFF");
        }
        else if (strcmp(line, "stop") == 0)
        {
            // Запрашиваем корректное завершение через SIGINT
            EXTRA_LOG_WARN("Terminal → stop");
            raise(SIGINT);
            break;  // выходим из цикла и завершаем поток
        }
        else
        {
            // Логируем, что команда неизвестна
            EXTRA_LOG_WARN("Unknown command: '%s'", line);
        }
    }

    return NULL;
}

/*
 * Создает и отсоединяет поток для терминала.
 * Если pthread_create возвращает ошибку, логируем её и не запускаем поток.
 * После успешного pthread_detach ресурсы потока освободятся автоматически.
 */
void terminal_start(void)
{
    pthread_t tid;
    if (pthread_create(&tid, NULL, terminal_thread, NULL) != 0)
    {
        LOG_ERROR("Failed to launch terminal thread");
        return;
    }
    pthread_detach(tid);
}

/*
 * Возвращает текущее состояние freeze_flag.
 * Основной код туннеля вызывает эту функцию, чтобы решить,
 * форвардить пакеты или нет.
 */
bool terminal_is_frozen(void)
{
    return (bool)freeze_flag;
}
