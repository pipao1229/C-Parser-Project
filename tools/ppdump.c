/* Prints the preprocessed text with each line's origin.
 * "-c LINE COL" maps a preprocessed position to the original source */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "preprocessor.h"

int main(int argc, char **argv)
{
    PreprocessedFile pp;
    FILE *file;
    char buffer[8192];
    size_t i;

    if (argc != 2 && argc != 5) {
        fprintf(stderr, "Uso: %s <archivo> [-c LINEA COL]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!preprocessor_run(argv[1], &pp)) {
        fprintf(stderr, "fallo el preprocesado\n");
        return EXIT_FAILURE;
    }

    if (argc == 5 && strcmp(argv[2], "-c") == 0) {
        int line = atoi(argv[3]), col = atoi(argv[4]), ol, oc;
        const char *name = preprocessor_origin(&pp, line, col, &ol, &oc);
        printf("%d:%d -> %s:%d:%d\n", line, col, name ? name : "?", ol, oc);
        preprocessor_cleanup(&pp);
        return EXIT_SUCCESS;
    }

    file = fopen(pp.path, "rb");
    for (i = 0; file != NULL && fgets(buffer, sizeof(buffer), file) != NULL; i++) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') buffer[len - 1] = '\0';
        if (i < pp.line_count) {
            printf("%3zu  %-22s %3d | %s\n", i + 1, pp.files[pp.lines[i].file],
                    pp.lines[i].line, buffer);
        }
    }
    if (file != NULL) fclose(file);
    printf("(%zu lineas, %zu archivos)\n", pp.line_count, pp.file_count);

    preprocessor_cleanup(&pp);
    return EXIT_SUCCESS;
}