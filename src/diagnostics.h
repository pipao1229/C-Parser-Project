#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include "preprocessor.h"

/* Sets the preprocessor map so errors report original source positions.
 * It must stay alive while errors are being reported. */
void diag_set_source_map(const PreprocessedFile *pp);

/* Reports an error at (line, col) of the preprocessed text, gcc style:
 * file:line:col: error: message, the source line and a caret indicator. */
void diag_error(int line, int col, int len, const char *fmt, ...);

/* Number of errors reported so far */
int diag_count(void);

/* Releases the cached source file */
void diag_close(void);

#endif