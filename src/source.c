#include <stdio.h>
#include <stdlib.h>
#include "source.h"

/* Loads entire file content into a continuous heap-allocated memory buffer */
int source_load(const char *filename, Source *source)
{
    source->content = NULL;
    source->length = 0;

    FILE *file = fopen(filename, "rb");
    if (file == NULL) return 0;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return 0;
    }
    fseek(file, 0, SEEK_SET);

    source->content = malloc((size_t)file_size + 1);
    if (source->content == NULL) {
        fclose(file);
        return 0;
    }

    size_t bytes_read = fread(source->content, 1, (size_t)file_size, file);
    source->content[bytes_read] = '\0';
    source->length = bytes_read;
    fclose(file);
    return 1;
}

/* Releases dynamic buffer memory holding preprocessed source text */
void source_free(Source *source)
{
    free(source->content);
    source->content = NULL;
    source->length = 0;
}