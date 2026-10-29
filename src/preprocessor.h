#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <stddef.h>

/* One preprocessed line and where it came from in the original source */
typedef struct {
    int file;        /* index into PreprocessedFile.files */
    int line;        /* original line (1-based) in that file */
    int *cols;       /* cols[k] = original column (1-based) of output byte k */
    size_t length;   /* output line length, without the '\n' */
} OutLine;

typedef struct {
    char *path;              /* temp file holding the preprocessed text */

    char **files;            /* source file names (the main file is 0) */
    size_t file_count;
    size_t files_capacity;

    OutLine *lines;          /* lines[n-1] describes line n of the preprocessed file */
    size_t line_count;
    size_t lines_capacity;
} PreprocessedFile;

int preprocessor_run(const char *input_path, PreprocessedFile *output);

/* Deletes the temp file and frees everything; safe after a failed run */
void preprocessor_cleanup(PreprocessedFile *output);

/* Maps a (line, column) of the preprocessed text to the original source.
 * Returns the original file name, or NULL if unknown. Out-of-range positions
 * are clamped to the last known line/column. */
const char *preprocessor_origin(const PreprocessedFile *pp, int line, int column,
                                int *orig_line, int *orig_col);

#endif