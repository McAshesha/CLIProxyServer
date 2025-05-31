#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>

/*
 * Запускает фоновый поток, который читает команды из stdin.
 * Поддержка команд:
 * freeze — ставит паузу на форвардинг пакетов
 * stop   — корректно выключает программу (через SIGINT)
 */
void terminal_start(void);

/*
 * Проверяет, включён ли сейчас режим «freeze».
 * Если true — туннель читает и логирует данные, но не шлёт дальше.
 */
bool terminal_is_frozen(void);

#endif // TERMINAL_H
