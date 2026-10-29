#ifndef PREPROCESSOR_H
#define PREPROCESSOR_H

#include <stddef.h>

/* One preprocessed line and where it came from in the original source */
typedef struct {
    int file;        
    int line;        
    int *cols;      
    size_t length;   
} OutLine;

typedef struct {
    char *path;              /* temp file holding the preprocessed text */

    char **files;           
    size_t file_count;
    size_t files_capacity;

    OutLine *lines;          /* lines[n-1] describes line n of the preprocessed file */
    size_t line_count;
    size_t lines_capacity;
} PreprocessedFile;

int preprocessor_run(const char *input_path, PreprocessedFile *output);

/* Deletes the temp file and frees everything. Safe after a failed run */
void preprocessor_cleanup(PreprocessedFile *output);

/* Maps a (line, column) of the preprocessed text to the original source.
 * Returns the original file name, or NULL if unknown */
const char *preprocessor_origin(const PreprocessedFile *pp, int line, int column,
                                int *orig_line, int *orig_col);

#endif