#ifndef STEVE_LIB_IO_H
#define STEVE_LIB_IO_H

#include <stdint.h>
#include <stddef.h>

char *read_entire_binary_file(const char *filename, size_t *size);

#endif // STEVE_LIB_IO_H
