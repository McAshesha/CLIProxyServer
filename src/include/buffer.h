#ifndef BUFF_H
#define BUFF_H

/*
 * Структура буфера:
 * - data        : указатель на область памяти для хранения данных
 * - write_index : индекс, куда будет записываться следующий байт
 * - read_index  : индекс, откуда будет читаться следующий байт
 * - cap         : текущая ёмкость выделенного массива data
 */
typedef struct buffer
{
    char   *data;        // Указатель на старт данных буфера
    size_t  write_index; // Сколько байт записано (конец данных)
    size_t  read_index;  // Сколько байт прочитано (начало данных)
    size_t  cap;         // Общий размер буфера
} buffer_t;

/*
 * Создаёт буфер с заданным размером. Вернёт NULL, если память не выделится.
 */
buffer_t *buffer_create(size_t capacity);

/*
 * Чистит всю память буфера вместе со структурой buffer\_t.
 */
void buffer_release(buffer_t *buffer);

/*
 * Считывает из fd в буфер, расширяя при необходимости.
 * Возвращает число прочитанных байт (>0), 0 — EOF, <0 — ошибка.
 */
int buffer_readfd(buffer_t *buffer, int fd);

/*
 * Пишет из буфера в fd.
 * Возвращает число записанных байт (>0), 0 — если не смог записать, <0 — ошибка.
 */
int buffer_writefd(buffer_t *buffer, int fd);

/*
 * Копирует size байт из внутреннего буфера в dst
 */
void *buffer_read(buffer_t *buffer, void *dst, size_t size);

/*
 * Пропускает size байт (увеличивает read_index). Проверка через assert
 */
void buffer_skip(buffer_t *buffer, size_t size);

/*
 * Записывает size байт из src в буфер, сдвигая/расширяя его при необходимости
 * Возвращает 0 при успехе, -1 при ошибке расширения
 */
int buffer_write(buffer_t *buffer, void *src, size_t size);

/*
 * Конкатенирует содержимое rear (от read_index до write_index) в конец front
 */
int buffer_concat(buffer_t *front, buffer_t *rear);

/*
 * Возвращает число байт, доступных для чтения: write_index - read_index
 */
size_t buffer_readable(buffer_t *buffer);

/*
 * Сброс буфера: обнуляет индексы, старые данные остаются в памяти, но перезаписываются
 */
void buffer_clear(buffer_t *buffer);

#endif // BUFF_H
