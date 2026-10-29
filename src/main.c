#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "diagnostics.h"
#include "preprocessor.h"
#include "scanner.h"
#include "symtab.h"

int yyparse(void);

static void print_usage(FILE *out, const char *prog)
{
    fprintf(out, "Uso: %s [opciones] <archivo-fuente>\n", prog);
    fprintf(out, "Opciones:\n");
    fprintf(out, "  -h    Muestra esta ayuda\n");
}

/* Validates the input file before scanning, with gcc-style messages */
static int check_input_file(const char *prog, const char *path)
{
    struct stat info;

    if (stat(path, &info) == -1) {
        fprintf(stderr, "%s: error: %s: %s\n", prog, path, strerror(errno));
        return 0;
    }
    if (S_ISDIR(info.st_mode)) {
        fprintf(stderr, "%s: error: %s: es un directorio, se esperaba un archivo fuente\n",
                prog, path);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                print_usage(stdout, argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(stderr, argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (argc - optind != 1) {
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (!check_input_file(argv[0], argv[optind])) {
        return EXIT_FAILURE;
    }

    /* Phase 1: preprocess (#include, #define) into a temp file plus an
     * origin map used to report original positions. */
    PreprocessedFile preprocessed;
    if (!preprocessor_run(argv[optind], &preprocessed)) {
        fprintf(stderr, "%s: error: %s: no se pudo preprocesar el archivo\n",
                argv[0], argv[optind]);
        return EXIT_FAILURE;
    }

    /* Phase 2: scanner and parser over the preprocessed text */
    if (!scanner_init(preprocessed.path)) {
        /* fopen leaves the reason in errno */
        fprintf(stderr, "%s: error: %s: %s\n", argv[0], preprocessed.path, strerror(errno));
        preprocessor_cleanup(&preprocessed);
        return EXIT_FAILURE;
    }

    diag_set_source_map(&preprocessed);
    yyparse();
    scanner_close();

    int errors = diag_count();
    preprocessor_cleanup(&preprocessed);
    symtab_free();
    diag_close();

    return errors > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}