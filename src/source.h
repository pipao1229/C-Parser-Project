#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>

typedef struct {
    char *content;
    size_t length;
} Source;

int source_load(const char *filename, Source *source);
void source_free(Source *source);

#endif