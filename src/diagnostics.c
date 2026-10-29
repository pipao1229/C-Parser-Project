#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "diagnostics.h"
#include "source.h"

#define COLOR_BOLD  "\033[1m"
#define COLOR_RED   "\033[1;31m"
#define COLOR_RESET "\033[0m"

static int error_count = 0;
static const PreprocessedFile *source_map = NULL;

/* Cache of the last original file read, to print its lines */
static Source cached_source;
static char cached_name[1024];
static int cache_valid = 0;

/* Position of the last reported error */
static char last_file[1024];
static int last_line = 0, last_col = 0, last_valid = 0;

void diag_set_source_map(const PreprocessedFile *pp)
{
    source_map = pp;
}

/* Returns the text of a line of an original file (not NUL-terminated) */
static const char *line_text(const char *file, int line, size_t *length)
{
    size_t start = 0, end;
    int current = 1;

    if (!cache_valid || strcmp(cached_name, file) != 0) {
        if (cache_valid) source_free(&cached_source);
        cache_valid = 0;
        if (strlen(file) >= sizeof(cached_name) || !source_load(file, &cached_source)) return NULL;
        strcpy(cached_name, file);
        cache_valid = 1;
    }

    while (start < cached_source.length && current < line) {
        if (cached_source.content[start] == '\n') current++;
        start++;
    }
    if (current != line) return NULL;

    end = start;
    while (end < cached_source.length && cached_source.content[end] != '\n') end++;
    if (end > start && cached_source.content[end - 1] == '\r') end--;

    *length = end - start;
    return cached_source.content + start;
}

/* Prints the source line and a caret under the error (tabs are copied so the
 * caret stays aligned). */
static void print_context(const char *file, int line, int col, int span, int color)
{
    size_t length;
    const char *text = line_text(file, line, &length);

    if (text == NULL) return;
    if (col < 1) col = 1;
    if (span < 1) span = 1;
    if ((size_t)(col - 1) < length && (size_t)(col - 1 + span) > length) span = (int)length - (col - 1);
    if (span < 1) span = 1;

    fprintf(stderr, "%5d | ", line);
    fwrite(text, 1, length, stderr);
    fputc('\n', stderr);

    fprintf(stderr, "      | ");
    for (int i = 0; i < col - 1; i++) {
        fputc(((size_t)i < length && text[i] == '\t') ? '\t' : ' ', stderr);
    }
    if (color) fputs(COLOR_RED, stderr);
    fputc('^', stderr);
    for (int i = 1; i < span; i++) fputc('~', stderr);
    if (color) fputs(COLOR_RESET, stderr);
    fputc('\n', stderr);
}

void diag_error(int line, int col, int len, const char *fmt, ...)
{
    va_list args;
    int orig_line = line, orig_col = col, end_line, end_col;
    int color = isatty(STDERR_FILENO);
    const char *file = preprocessor_origin(source_map, line, col, &orig_line, &orig_col);

    /* Do not report two errors at the very same spot (a lexical error often
     * triggers a syntax error on the same token). */
    if (file != NULL) {
        if (last_valid && last_line == orig_line && last_col == orig_col && strcmp(last_file, file) == 0) return;
        last_valid = (strlen(file) < sizeof(last_file));
        if (last_valid) {
            strcpy(last_file, file);
            last_line = orig_line;
            last_col = orig_col;
        }
    }

    if (file != NULL) {
        int span = 1;
        if (len > 1 && preprocessor_origin(source_map, line, col + len - 1, &end_line, &end_col) != NULL &&
            end_line == orig_line && end_col >= orig_col) {
            span = end_col - orig_col + 1;
        }

        if (color) fprintf(stderr, COLOR_BOLD "%s:%d:%d:" COLOR_RESET " " COLOR_RED "error:" COLOR_RESET " ",
                            file, orig_line, orig_col);
        else fprintf(stderr, "%s:%d:%d: error: ", file, orig_line, orig_col);

        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fputc('\n', stderr);

        print_context(file, orig_line, orig_col, span, color);
    } else {
        fprintf(stderr, "%d:%d: error: ", line, col);
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fputc('\n', stderr);
    }

    error_count++;
}

int diag_count(void)
{
    return error_count;
}

void diag_close(void)
{
    if (cache_valid) source_free(&cached_source);
    cache_valid = 0;
}