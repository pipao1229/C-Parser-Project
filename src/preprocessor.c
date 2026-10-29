#define _XOPEN_SOURCE 700   /* realpath() */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "builtin_headers.h"
#include "preprocessor.h"

#define TEMP_TEMPLATE "/tmp/scanner_preprocessed_XXXXXX"
#define INITIAL_MACROS 32
#define INITIAL_ACTIVE 16
#define PP_PATH_MAX 4096

/* What to do with an unresolvable #include (missing or circular):
 * 0 = skip silently, 1 = report on stderr */
#define PP_REPORT_INCLUDE_ERRORS 0

/* Stores definition for both object-like and function-like macros */
typedef struct {
    char *name;
    char *value;
    int is_function_like;
    char **params;
    size_t param_count;
} Macro;

/* Dynamic table storing all active preprocessor definitions */
typedef struct {
    Macro *items;
    size_t count;
    size_t capacity;
} MacroTable;

/* Tracks macro expansion recursion to prevent infinite replacement loops */
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ActiveMacros;

/* Call stack of included files (by real path) to detect circular includes */
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} IncludeStack;

/* Output line under construction, plus the original column of each byte */
typedef struct {
    char *text;
    int *cols;
    size_t length;
    size_t capacity;
} LineBuf;

static char *copy_string(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length + 1);
    return copy;
}

static char *trim_copy(const char *text, size_t length)
{
    size_t start = 0, end = length;
    while (start < end && isspace((unsigned char)text[start])) start++;
    while (end > start && isspace((unsigned char)text[end - 1])) end--;

    size_t result_length = end - start;
    char *result = malloc(result_length + 1);
    if (result == NULL) return NULL;

    memcpy(result, text + start, result_length);
    result[result_length] = '\0';
    return result;
}

static int is_identifier_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static int is_identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static void lb_init(LineBuf *lb)
{
    lb->text = NULL;
    lb->cols = NULL;
    lb->length = 0;
    lb->capacity = 0;
}

static void lb_free(LineBuf *lb)
{
    free(lb->text);
    free(lb->cols);
    lb_init(lb);
}

static int lb_put(LineBuf *lb, char c, int col)
{
    if (lb->length == lb->capacity) {
        size_t new_capacity = (lb->capacity == 0) ? 128 : lb->capacity * 2;
        char *new_text = realloc(lb->text, new_capacity);
        if (new_text == NULL) return 0;
        lb->text = new_text;
        int *new_cols = realloc(lb->cols, new_capacity * sizeof(int));
        if (new_cols == NULL) return 0;
        lb->cols = new_cols;
        lb->capacity = new_capacity;
    }
    lb->text[lb->length] = c;
    lb->cols[lb->length] = col;
    lb->length++;
    return 1;
}

/* Original column of text[index]: user-written text maps one to one, 
 * macro-expanded text maps entirely to the column of the invoked macro name */
static int col_of(int base_col, int fixed_col, size_t index)
{
    return (fixed_col > 0) ? fixed_col : base_col + (int)index;
}

static int lb_put_text(LineBuf *lb, const char *text, size_t count,
                        int base_col, int fixed_col, size_t first_index)
{
    for (size_t k = 0; k < count; k++) {
        if (!lb_put(lb, text[k], col_of(base_col, fixed_col, first_index + k))) return 0;
    }
    return 1;
}

static int pp_add_file(PreprocessedFile *pp, const char *name)
{
    for (size_t i = 0; i < pp->file_count; i++) {
        if (strcmp(pp->files[i], name) == 0) return (int)i;
    }

    if (pp->file_count == pp->files_capacity) {
        size_t new_capacity = (pp->files_capacity == 0) ? 8 : pp->files_capacity * 2;
        char **new_files = realloc(pp->files, new_capacity * sizeof(char *));
        if (new_files == NULL) return -1;
        pp->files = new_files;
        pp->files_capacity = new_capacity;
    }

    char *copy = copy_string(name);
    if (copy == NULL) return -1;
    pp->files[pp->file_count] = copy;
    return (int)pp->file_count++;
}

/* Registers an output line, the map takes ownership of the column array */
static int pp_push_line(PreprocessedFile *pp, int file, int line, LineBuf *lb)
{
    if (pp->line_count == pp->lines_capacity) {
        size_t new_capacity = (pp->lines_capacity == 0) ? 256 : pp->lines_capacity * 2;
        OutLine *new_lines = realloc(pp->lines, new_capacity * sizeof(OutLine));
        if (new_lines == NULL) return 0;
        pp->lines = new_lines;
        pp->lines_capacity = new_capacity;
    }

    OutLine *entry = &pp->lines[pp->line_count++];
    entry->file = file;
    entry->line = line;
    entry->cols = lb->cols;
    entry->length = lb->length;
    lb->cols = NULL;   /* now owned by the map */
    return 1;
}

/* Writes the line to the temp file and records it in the map */
static int emit_line(FILE *output, PreprocessedFile *pp, int file_index, int line_no, LineBuf *lb)
{
    int ok = 1;

    if (lb->length > 0 && fwrite(lb->text, 1, lb->length, output) != lb->length) ok = 0;
    if (ok && fputc('\n', output) == EOF) ok = 0;
    if (ok && !pp_push_line(pp, file_index, line_no, lb)) ok = 0;

    lb_free(lb);
    return ok;
}

const char *preprocessor_origin(const PreprocessedFile *pp, int line, int column,
                                int *orig_line, int *orig_col)
{
    if (pp == NULL || pp->file_count == 0) return NULL;

    if (pp->line_count == 0) {
        /* empty file: point to the start of the main file */
        *orig_line = 1;
        *orig_col = 1;
        return pp->files[0];
    }

    size_t index;
    int past_end = 0;

    if (line < 1) {
        index = 0;
        column = 1;
    } else if ((size_t)line > pp->line_count) {
        index = pp->line_count - 1;   /* end of file: anchor to the last line */
        past_end = 1;
    } else {
        index = (size_t)line - 1;
    }

    const OutLine *entry = &pp->lines[index];
    int last_known = (entry->length > 0) ? entry->cols[entry->length - 1] : 0;

    *orig_line = entry->line;

    if (past_end) {
        *orig_col = last_known + 1;
    } else if (column < 1) {
        *orig_col = 1;
    } else if ((size_t)(column - 1) < entry->length) {
        *orig_col = entry->cols[column - 1];
    } else {
        *orig_col = last_known + (column - (int)entry->length);
    }

    return pp->files[entry->file];
}

void preprocessor_cleanup(PreprocessedFile *output)
{
    if (output->path != NULL) {
        unlink(output->path);
        free(output->path);
    }
    for (size_t i = 0; i < output->file_count; i++) free(output->files[i]);
    free(output->files);
    for (size_t i = 0; i < output->line_count; i++) free(output->lines[i].cols);
    free(output->lines);

    output->path = NULL;
    output->files = NULL;
    output->file_count = 0;
    output->files_capacity = 0;
    output->lines = NULL;
    output->line_count = 0;
    output->lines_capacity = 0;
}

static void macro_table_init(MacroTable *table)
{
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

static void free_params(char **params, size_t count)
{
    if (params == NULL) return;
    for (size_t i = 0; i < count; i++) free(params[i]);
    free(params);
}

static void macro_free_entry(Macro *m)
{
    free(m->name);
    free(m->value);
    free_params(m->params, m->param_count);
    m->name = NULL;
    m->value = NULL;
    m->params = NULL;
    m->param_count = 0;
}

static void macro_table_free(MacroTable *table)
{
    for (size_t i = 0; i < table->count; i++) {
        macro_free_entry(&table->items[i]);
    }
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

static const Macro *macro_find(const MacroTable *table, const char *name)
{
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            return &table->items[i];
        }
    }
    return NULL;
}

/* Defines or redefines a macro. Copies are made before touching the table so
 * an allocation failure never leaves a half-built entry */
static int macro_store(MacroTable *table, const char *name, const char *value,
                       int is_function_like, char **params, size_t param_count)
{
    char *name_copy = copy_string(name);
    char *value_copy = copy_string(value);
    Macro *slot = NULL;

    if (name_copy == NULL || value_copy == NULL) {
        free(name_copy);
        free(value_copy);
        free_params(params, param_count);
        return 0;
    }

    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            slot = &table->items[i];
            macro_free_entry(slot);
            break;
        }
    }

    if (slot == NULL) {
        if (table->count == table->capacity) {
            size_t new_capacity = (table->capacity == 0) ? INITIAL_MACROS : table->capacity * 2;
            Macro *new_items = realloc(table->items, new_capacity * sizeof(Macro));
            if (new_items == NULL) {
                free(name_copy);
                free(value_copy);
                free_params(params, param_count);
                return 0;
            }
            table->items = new_items;
            table->capacity = new_capacity;
        }
        slot = &table->items[table->count++];
    }

    slot->name = name_copy;
    slot->value = value_copy;
    slot->is_function_like = is_function_like;
    slot->params = params;
    slot->param_count = param_count;
    return 1;
}

/* Removes a macro if it exists */
static void macro_undef(MacroTable *table, const char *name)
{
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) {
            macro_free_entry(&table->items[i]);
            table->items[i] = table->items[table->count - 1];
            table->count--;
            return;
        }
    }
}

static void active_init(ActiveMacros *active)
{
    active->items = NULL;
    active->count = 0;
    active->capacity = 0;
}

static void active_free(ActiveMacros *active)
{
    for (size_t i = 0; i < active->count; i++) free(active->items[i]);
    free(active->items);
    active->items = NULL;
    active->count = 0;
    active->capacity = 0;
}

static int active_contains(const ActiveMacros *active, const char *name)
{
    for (size_t i = 0; i < active->count; i++) {
        if (strcmp(active->items[i], name) == 0) return 1;
    }
    return 0;
}

static int active_push(ActiveMacros *active, const char *name)
{
    if (active->count == active->capacity) {
        size_t new_capacity = (active->capacity == 0) ? INITIAL_ACTIVE : active->capacity * 2;
        char **new_items = realloc(active->items, new_capacity * sizeof(char *));
        if (new_items == NULL) return 0;
        active->items = new_items;
        active->capacity = new_capacity;
    }
    char *copy = copy_string(name);
    if (!copy) return 0;
    active->items[active->count++] = copy;
    return 1;
}

static void active_pop(ActiveMacros *active)
{
    if (active->count == 0) return;
    active->count--;
    free(active->items[active->count]);
    active->items[active->count] = NULL;
}

static void include_stack_init(IncludeStack *stack)
{
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static void include_stack_free(IncludeStack *stack)
{
    for (size_t i = 0; i < stack->count; i++) free(stack->items[i]);
    free(stack->items);
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
}

static int include_stack_contains(const IncludeStack *stack, const char *path)
{
    for (size_t i = 0; i < stack->count; i++) {
        if (strcmp(stack->items[i], path) == 0) return 1;
    }
    return 0;
}

static int include_stack_push(IncludeStack *stack, const char *path)
{
    if (stack->count == stack->capacity) {
        size_t new_capacity = (stack->capacity == 0) ? 8 : stack->capacity * 2;
        char **new_items = realloc(stack->items, new_capacity * sizeof(char *));
        if (!new_items) return 0;
        stack->items = new_items;
        stack->capacity = new_capacity;
    }
    char *copy = copy_string(path);
    if (!copy) return 0;
    stack->items[stack->count++] = copy;
    return 1;
}

static void include_stack_pop(IncludeStack *stack)
{
    if (stack->count == 0) return;
    stack->count--;
    free(stack->items[stack->count]);
    stack->items[stack->count] = NULL;
}

/* Blanks out comments with spaces (newlines kept) so the output has the same
 * length as the input and all lines/columns still match the original */
static int remove_comments(const char *input, size_t length, char **output, size_t *output_length)
{
    char *result = malloc(length + 1);
    size_t i = 0, position = 0;
    int in_string = 0, in_char = 0;
    if (result == NULL) return 0;

    while (i < length) {
        char c = input[i];

        if (in_string || in_char) {
            result[position++] = c;
            if (c == '\n') {
                in_string = 0;
                in_char = 0;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < length && input[i + 1] != '\n') {
                result[position++] = input[i + 1];
                i += 2;
                continue;
            }
            if (in_string && c == '"') in_string = 0;
            if (in_char && c == '\'') in_char = 0;
            i++;
            continue;
        }

        if (c == '"') {
            in_string = 1;
            result[position++] = c;
            i++;
            continue;
        }

        if (c == '\'') {
            in_char = 1;
            result[position++] = c;
            i++;
            continue;
        }

        if (c == '/' && i + 1 < length && input[i + 1] == '/') {
            while (i < length && input[i] != '\n') {
                result[position++] = ' ';
                i++;
            }
            continue;
        }

        if (c == '/' && i + 1 < length && input[i + 1] == '*') {
            result[position++] = ' ';
            result[position++] = ' ';
            i += 2;
            while (i < length) {
                if (input[i] == '*' && i + 1 < length && input[i + 1] == '/') {
                    result[position++] = ' ';
                    result[position++] = ' ';
                    i += 2;
                    break;
                }
                result[position++] = (input[i] == '\n') ? '\n' : ' ';
                i++;
            }
            continue;
        }

        result[position++] = c;
        i++;
    }

    result[position] = '\0';
    *output = result;
    *output_length = position;
    return 1;
}

static int parse_directive(const char *line, size_t length, char **directive, char **argument)
{
    size_t position = 0, start;
    *directive = NULL;
    *argument = NULL;

    while (position < length && isspace((unsigned char)line[position])) position++;
    if (position >= length || line[position] != '#') return 0;
    position++;

    while (position < length && isspace((unsigned char)line[position])) position++;
    start = position;

    while (position < length && isalpha((unsigned char)line[position])) position++;
    if (position == start) {
        if (position < length) return 0;   
        *directive = copy_string("");      
        *argument = copy_string("");
        return (*directive != NULL && *argument != NULL) ? 1 : -1;
    }

    *directive = trim_copy(line + start, position - start);
    if (*directive == NULL) return -1;

    *argument = trim_copy(line + position, length - position);
    if (*argument == NULL) {
        free(*directive);
        *directive = NULL;
        return -1;
    }
    return 1;
}

/* Appends a parameter to the list, frees 'p' on failure */
static int add_param(char ***params, size_t *count, char *p)
{
    char **new_params = realloc(*params, (*count + 1) * sizeof(char *));
    if (new_params == NULL) {
        free(p);
        return 0;
    }
    *params = new_params;
    (*params)[(*count)++] = p;
    return 1;
}

/* Handles a #define argument. Returns 0 only on out-of-memory.
 * A malformed #define is ignored */
static int handle_define(MacroTable *macros, const char *argument)
{
    size_t arg_len = strlen(argument), pos = 0;

    while (pos < arg_len && isspace((unsigned char)argument[pos])) pos++;
    if (pos >= arg_len || !is_identifier_start(argument[pos])) return 1;

    size_t name_start = pos;
    while (pos < arg_len && is_identifier_char(argument[pos])) pos++;

    char *name = trim_copy(argument + name_start, pos - name_start);
    if (name == NULL) return 0;

    int ok = 1;

    if (pos < arg_len && argument[pos] == '(') {
        /* Function-like macro */
        char **params = NULL;
        size_t param_count = 0, p_start = ++pos;
        int closed = 0, failed = 0;

        while (pos < arg_len && !closed && !failed) {
            if (argument[pos] == ',' || argument[pos] == ')') {
                char *p = trim_copy(argument + p_start, pos - p_start);
                if (p == NULL) {
                    failed = 1;
                } else if (p[0] == '\0') {
                    free(p);
                } else if (!add_param(&params, &param_count, p)) {
                    failed = 1;
                }
                p_start = pos + 1;
                if (argument[pos] == ')') closed = 1;
            }
            pos++;
        }

        if (failed) {
            ok = 0;
            free_params(params, param_count);
        } else if (!closed) {
            free_params(params, param_count);   /* unclosed parameter list: ignore */
        } else {
            char *value = trim_copy(argument + pos, arg_len - pos);
            if (value == NULL) {
                free_params(params, param_count);
                ok = 0;
            } else {
                ok = macro_store(macros, name, value, 1, params, param_count);
                free(value);
            }
        }
    } else {
        char *value = trim_copy(argument + pos, arg_len - pos);
        if (value == NULL) {
            ok = 0;
        } else {
            ok = macro_store(macros, name, value, 0, NULL, 0);
            free(value);
        }
    }

    free(name);
    return ok;
}

/* Reports an #include problem only if PP_REPORT_INCLUDE_ERRORS is set */
static void include_problem(const char *file, int line, const char *fmt, ...)
{
#if PP_REPORT_INCLUDE_ERRORS
    va_list args;
    fprintf(stderr, "%s:%d: error: ", file, line);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
#else
    (void)file;
    (void)line;
    (void)fmt;
#endif
}

/* Extracts the name from "file" or <file>. Returns 0 if malformed */
static int parse_include_target(const char *argument, char *name, size_t name_size, int *is_system)
{
    char closing;
    const char *end;
    size_t length;

    if (argument[0] == '"') {
        closing = '"';
        *is_system = 0;
    } else if (argument[0] == '<') {
        closing = '>';
        *is_system = 1;
    } else {
        return 0;
    }

    end = strchr(argument + 1, closing);
    if (end == NULL) return 0;

    length = (size_t)(end - (argument + 1));
    if (length == 0 || length >= name_size) return 0;

    memcpy(name, argument + 1, length);
    name[length] = '\0';
    return 1;
}

static int is_regular_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

/* Finds the file to include */
static int resolve_include(const char *including_file, const char *name, int is_system,
                           char *out, size_t out_size)
{
    size_t name_len = strlen(name);

    if (!is_system) {
        const char *slash = strrchr(including_file, '/');
        if (slash != NULL) {
            size_t dir_len = (size_t)(slash - including_file) + 1;
            if (dir_len + name_len + 1 <= out_size) {
                memcpy(out, including_file, dir_len);
                memcpy(out + dir_len, name, name_len + 1);
                if (is_regular_file(out)) return 1;
            }
        }
    }

    if (name_len + 1 <= out_size) {
        memcpy(out, name, name_len + 1);
        if (is_regular_file(out)) return 1;
    }
    return 0;
}

static int expand_text(LineBuf *lb, const char *text, size_t length, int base_col, int fixed_col,
                       MacroTable *macros, ActiveMacros *active);

/* Instantiates a function-like macro body by replacing formal parameters with arguments */
static char *instantiate_macro_body(const Macro *m, char **args)
{
    size_t body_len = strlen(m->value);
    size_t out_cap = body_len * 2 + 64;
    char *out = malloc(out_cap);
    if (!out) return NULL;
    size_t out_len = 0;

    size_t i = 0;
    while (i < body_len) {
        if (is_identifier_start(m->value[i])) {
            size_t start = i;
            while (i < body_len && is_identifier_char(m->value[i])) i++;
            size_t id_len = i - start;

            int param_idx = -1;
            for (size_t p = 0; p < m->param_count; p++) {
                if (strlen(m->params[p]) == id_len &&
                    strncmp(m->value + start, m->params[p], id_len) == 0) {
                    param_idx = (int)p;
                    break;
                }
            }

            const char *sub = (param_idx != -1) ? args[param_idx] : m->value + start;
            size_t sub_len = (param_idx != -1) ? strlen(sub) : id_len;

            while (out_len + sub_len + 1 >= out_cap) {
                out_cap *= 2;
                char *new_out = realloc(out, out_cap);
                if (!new_out) { free(out); return NULL; }
                out = new_out;
            }

            memcpy(out + out_len, sub, sub_len);
            out_len += sub_len;
            continue;
        }

        if (out_len + 2 >= out_cap) {
            out_cap *= 2;
            char *new_out = realloc(out, out_cap);
            if (!new_out) { free(out); return NULL; }
            out = new_out;
        }
        out[out_len++] = m->value[i++];
    }

    out[out_len] = '\0';
    return out;
}

/* Expands identifiers and macro calls recursively into the line buffer */
static int expand_text(LineBuf *lb, const char *text, size_t length, int base_col, int fixed_col,
                       MacroTable *macros, ActiveMacros *active)
{
    size_t i = 0;
    int in_string = 0, in_char = 0;

    while (i < length) {
        if (in_string || in_char) {
            if (!lb_put(lb, text[i], col_of(base_col, fixed_col, i))) return 0;
            if (text[i] == '\\' && i + 1 < length) {
                if (!lb_put(lb, text[i + 1], col_of(base_col, fixed_col, i + 1))) return 0;
                i += 2;
                continue;
            }
            if (in_string && text[i] == '"') in_string = 0;
            if (in_char && text[i] == '\'') in_char = 0;
            i++;
            continue;
        }

        if (text[i] == '"' || text[i] == '\'') {
            if (text[i] == '"') in_string = 1; else in_char = 1;
            if (!lb_put(lb, text[i], col_of(base_col, fixed_col, i))) return 0;
            i++;
            continue;
        }

        /* Numbers are copied whole so suffixes are never mistaken for a macro name */
        if (isdigit((unsigned char)text[i]) ||
            (text[i] == '.' && i + 1 < length && isdigit((unsigned char)text[i + 1]))) {
            while (i < length) {
                char c = text[i];
                int sign_after_exp = (c == '+' || c == '-') && i > 0 &&
                                    (text[i - 1] == 'e' || text[i - 1] == 'E' ||
                                    text[i - 1] == 'p' || text[i - 1] == 'P');
                if (!(isalnum((unsigned char)c) || c == '_' || c == '.' || sign_after_exp)) break;
                if (!lb_put(lb, c, col_of(base_col, fixed_col, i))) return 0;
                i++;
            }
            continue;
        }

        if (is_identifier_start(text[i])) {
            size_t start = i;
            while (i < length && is_identifier_char(text[i])) i++;
            size_t id_len = i - start;

            char *identifier = malloc(id_len + 1);
            if (!identifier) return 0;
            memcpy(identifier, text + start, id_len);
            identifier[id_len] = '\0';

            const Macro *m = macro_find(macros, identifier);

            /* Normal identifier or already active in expansion stack */
            if (m == NULL || active_contains(active, identifier)) {
                int ok = lb_put_text(lb, text + start, id_len, base_col, fixed_col, start);
                free(identifier);
                if (!ok) return 0;
                continue;
            }

            /* Column that everything produced by this macro maps to */
            int expansion_col = (fixed_col > 0) ? fixed_col : base_col + (int)start;

            /* Object-like macro replacement */
            if (!m->is_function_like) {
                if (!active_push(active, identifier)) { free(identifier); return 0; }
                if (!expand_text(lb, m->value, strlen(m->value), 0, expansion_col, macros, active)) {
                    active_pop(active); free(identifier); return 0;
                }
                active_pop(active);
                free(identifier);
                continue;
            }

            /* Function-like macro: verify if followed by '(' */
            size_t peek = i;
            while (peek < length && isspace((unsigned char)text[peek])) peek++;

            if (peek >= length || text[peek] != '(') {
                /* Not invoked as a function, emit identifier as is */
                int ok = lb_put_text(lb, text + start, id_len, base_col, fixed_col, start);
                free(identifier);
                if (!ok) return 0;
                continue;
            }

            /* Parse arguments between '(' and matching ')' */
            peek++; /* Skip '(' */
            int paren_depth = 1;
            char **args = malloc(sizeof(char *) * (m->param_count > 0 ? m->param_count : 1));
            if (args == NULL) { free(identifier); return 0; }
            size_t arg_count = 0;
            size_t cur_arg_start = peek;
            int arg_error = 0, out_of_memory = 0;

            while (peek < length && paren_depth > 0 && !out_of_memory) {
                if (text[peek] == '(') {
                    paren_depth++;
                } else if (text[peek] == ')') {
                    paren_depth--;
                    if (paren_depth == 0) {
                        if (m->param_count > 0 || peek > cur_arg_start) {
                            char *arg_val = trim_copy(text + cur_arg_start, peek - cur_arg_start);
                            if (arg_val == NULL) {
                                out_of_memory = 1;
                            } else if (arg_count < m->param_count) {
                                args[arg_count++] = arg_val;
                            } else {
                                free(arg_val);
                                arg_error = 1;
                            }
                        }
                        peek++;
                        break;
                    }
                } else if (text[peek] == ',' && paren_depth == 1) {
                    char *arg_val = trim_copy(text + cur_arg_start, peek - cur_arg_start);
                    if (arg_val == NULL) {
                        out_of_memory = 1;
                    } else if (arg_count < m->param_count) {
                        args[arg_count++] = arg_val;
                    } else {
                        free(arg_val);
                        arg_error = 1;
                    }
                    cur_arg_start = peek + 1;
                }
                peek++;
            }

            if (out_of_memory) {
                for (size_t a = 0; a < arg_count; a++) free(args[a]);
                free(args);
                free(identifier);
                return 0;
            }

            if (paren_depth != 0 || arg_count != m->param_count || arg_error) {
                /* Mismatch in arguments, output raw text without expansion */
                for (size_t a = 0; a < arg_count; a++) free(args[a]);
                free(args);
                int ok = lb_put_text(lb, text + start, peek - start, base_col, fixed_col, start);
                free(identifier);
                if (!ok) return 0;
                i = peek;
                continue;
            }

            /* Instantiate body with argument substitutions */
            char *instantiated = instantiate_macro_body(m, args);
            for (size_t a = 0; a < arg_count; a++) free(args[a]);
            free(args);

            if (!instantiated) { free(identifier); return 0; }

            if (!active_push(active, identifier)) {
                free(instantiated); free(identifier); return 0;
            }

            /* Recursively expand the instantiated body */
            if (!expand_text(lb, instantiated, strlen(instantiated), 0, expansion_col, macros, active)) {
                active_pop(active); free(instantiated); free(identifier); return 0;
            }

            active_pop(active);
            free(instantiated);
            free(identifier);

            i = peek; /* Advance cursor past the macro invocation */
            continue;
        }

        if (!lb_put(lb, text[i], col_of(base_col, fixed_col, i))) return 0;
        i++;
    }

    return 1;
}

static int process_file(FILE *output, PreprocessedFile *pp, const char *filename,
                        MacroTable *macros, ActiveMacros *active, IncludeStack *inc_stack);

static int process_buffer(FILE *output, PreprocessedFile *pp, int file_index, const char *filename,
                          const char *buffer, size_t length, MacroTable *macros,
                          ActiveMacros *active, IncludeStack *inc_stack);

/* Processes the text of a built-in standard header as if it were a file */
static int process_builtin(FILE *output, PreprocessedFile *pp, const char *header,
                           MacroTable *macros, ActiveMacros *active, IncludeStack *inc_stack)
{
    char display[PP_PATH_MAX];
    const char *text = builtin_header_text(header);
    int index;

    if (text == NULL) return 1;   /* unknown system header: skipped */
    snprintf(display, sizeof(display), "<%s>", header);
    index = pp_add_file(pp, display);
    if (index < 0) return 0;
    return process_buffer(output, pp, index, display, text, strlen(text), macros, active, inc_stack);
}

/* #pragma once */

static char **once_files = NULL;
static size_t once_count = 0, once_capacity = 0;

static int once_contains(const char *real_path)
{
    for (size_t i = 0; i < once_count; i++) {
        if (strcmp(once_files[i], real_path) == 0) return 1;
    }
    return 0;
}

static void once_add(const char *real_path)
{
    if (once_contains(real_path)) return;
    if (once_count == once_capacity) {
        size_t new_capacity = (once_capacity == 0) ? 8 : once_capacity * 2;
        char **new_files = realloc(once_files, new_capacity * sizeof(char *));
        if (new_files == NULL) return;
        once_files = new_files;
        once_capacity = new_capacity;
    }
    char *copy = copy_string(real_path);
    if (copy != NULL) once_files[once_count++] = copy;
}

static void once_free(void)
{
    for (size_t i = 0; i < once_count; i++) free(once_files[i]);
    free(once_files);
    once_files = NULL;
    once_count = once_capacity = 0;
}

/* Constant expressions for #if / #elif */

typedef struct {
    const char *p;
    int failed;
    int depth;
} Eval;

typedef enum {
    OP_LOR, OP_LAND, OP_OR, OP_XOR, OP_AND, OP_EQ, OP_NE, OP_LE, OP_GE,
    OP_SHL, OP_SHR, OP_LT, OP_GT, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD
} BinOp;

/* Two-character operators come before their one-character prefixes */
static const struct { const char *text; int prec; } EVAL_OPS[] = {
    {"||", 1}, {"&&", 2}, {"|", 3}, {"^", 4}, {"&", 5}, {"==", 6}, {"!=", 6},
    {"<=", 7}, {">=", 7}, {"<<", 8}, {">>", 8}, {"<", 7}, {">", 7},
    {"+", 9}, {"-", 9}, {"*", 10}, {"/", 10}, {"%", 10}
};

#define EVAL_MAX_DEPTH 100

static void ev_skip(Eval *e)
{
    while (isspace((unsigned char)*e->p)) e->p++;
}

static long long ev_ternary(Eval *e);

static long long ev_char_constant(Eval *e)
{
    long long value = 0;

    e->p++;   /* opening quote */
    if (*e->p == '\\' && e->p[1] != '\0') {
        e->p++;
        switch (*e->p) {
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            case '0': value = 0; break;
            default:  value = (unsigned char)*e->p; break;
        }
        e->p++;
    } else if (*e->p != '\0' && *e->p != '\'') {
        value = (unsigned char)*e->p++;
    }
    if (*e->p == '\'') e->p++; else e->failed = 1;
    return value;
}

static long long ev_primary(Eval *e)
{
    char c;
    long long value;

    ev_skip(e);
    c = *e->p;

    if (e->depth > EVAL_MAX_DEPTH) {
        e->failed = 1;
        return 0;
    }

    if (c == '(') {
        e->p++;
        e->depth++;
        value = ev_ternary(e);
        e->depth--;
        ev_skip(e);
        if (*e->p == ')') e->p++; else e->failed = 1;
        return value;
    }

    if (c == '+' || c == '-' || c == '!' || c == '~') {
        e->p++;
        e->depth++;
        value = ev_primary(e);
        e->depth--;
        if (c == '-') return (long long)(0ULL - (unsigned long long)value);
        if (c == '!') return !value;
        if (c == '~') return ~value;
        return value;
    }

    if (isdigit((unsigned char)c)) {
        char *end;
        value = (long long)strtoull(e->p, &end, 0);
        e->p = end;
        while (*e->p == 'u' || *e->p == 'U' || *e->p == 'l' || *e->p == 'L') e->p++;
        return value;
    }

    if (c == '\'') return ev_char_constant(e);

    if (is_identifier_start(c)) {
        /* Identifiers left after macro expansion are 0 */
        while (is_identifier_char(*e->p)) e->p++;
        return 0;
    }

    e->failed = 1;
    return 0;
}

static long long ev_apply(BinOp op, long long a, long long b, Eval *e)
{
    unsigned long long ua = (unsigned long long)a, ub = (unsigned long long)b;

    switch (op) {
        case OP_LOR:  return a || b;
        case OP_LAND: return a && b;
        case OP_OR:   return a | b;
        case OP_XOR:  return a ^ b;
        case OP_AND:  return a & b;
        case OP_EQ:   return a == b;
        case OP_NE:   return a != b;
        case OP_LE:   return a <= b;
        case OP_GE:   return a >= b;
        case OP_LT:   return a < b;
        case OP_GT:   return a > b;
        case OP_SHL:  return (b < 0 || b > 62) ? 0 : (long long)(ua << b);
        case OP_SHR:  return (b < 0 || b > 62) ? 0 : (a >> b);
        case OP_ADD:  return (long long)(ua + ub);
        case OP_SUB:  return (long long)(ua - ub);
        case OP_MUL:  return (long long)(ua * ub);
        case OP_DIV:
        case OP_MOD:
            if (b == 0) { e->failed = 1; return 0; }
            if (b == -1) return (op == OP_DIV) ? (long long)(0ULL - ua) : 0;
            return (op == OP_DIV) ? a / b : a % b;
    }
    return 0;
}

static long long ev_binary(Eval *e, int min_prec)
{
    long long lhs = ev_primary(e);

    for (;;) {
        int found = -1;

        ev_skip(e);
        for (size_t i = 0; i < sizeof(EVAL_OPS) / sizeof(EVAL_OPS[0]); i++) {
            size_t len = strlen(EVAL_OPS[i].text);
            if (strncmp(e->p, EVAL_OPS[i].text, len) == 0) {
                found = (int)i;
                break;
            }
        }
        if (found < 0 || EVAL_OPS[found].prec < min_prec) break;

        e->p += strlen(EVAL_OPS[found].text);
        long long rhs = ev_binary(e, EVAL_OPS[found].prec + 1);
        lhs = ev_apply((BinOp)found, lhs, rhs, e);
    }
    return lhs;
}

static long long ev_ternary(Eval *e)
{
    long long cond;

    if (e->depth > EVAL_MAX_DEPTH) {
        e->failed = 1;
        return 0;
    }
    cond = ev_binary(e, 1);

    ev_skip(e);
    if (*e->p == '?') {
        long long yes, no;
        e->p++;
        e->depth++;
        yes = ev_ternary(e);
        e->depth--;
        ev_skip(e);
        if (*e->p == ':') e->p++; else e->failed = 1;
        e->depth++;
        no = ev_ternary(e);
        e->depth--;
        return cond ? yes : no;
    }
    return cond;
}

/* Replaces "defined X" / "defined(X)" by 1 or 0 before macro expansion.
 * Returns a new string, or NULL on allocation failure. */
static char *substitute_defined(const char *expr, const MacroTable *macros)
{
    size_t length = strlen(expr), i = 0, out_len = 0;
    char *out = malloc(length + 1);
    if (out == NULL) return NULL;

    while (i < length) {
        if (isdigit((unsigned char)expr[i])) {
            while (i < length && (isalnum((unsigned char)expr[i]) || expr[i] == '_' || expr[i] == '.')) out[out_len++] = expr[i++];
        } else if (is_identifier_start(expr[i])) {
            size_t start = i;
            while (i < length && is_identifier_char(expr[i])) i++;

            if (i - start == 7 && strncmp(expr + start, "defined", 7) == 0) {
                size_t j = i;
                int paren = 0;
                char name[256];
                size_t name_len = 0;

                while (j < length && isspace((unsigned char)expr[j])) j++;
                if (j < length && expr[j] == '(') { paren = 1; j++; }
                while (j < length && isspace((unsigned char)expr[j])) j++;
                while (j < length && is_identifier_char(expr[j]) && name_len < sizeof(name) - 1) name[name_len++] = expr[j++];
                name[name_len] = '\0';
                while (j < length && isspace((unsigned char)expr[j])) j++;
                if (paren && j < length && expr[j] == ')') j++;

                out[out_len++] = (name_len > 0 && macro_find(macros, name) != NULL) ? '1' : '0';
                i = j;
            } else {
                memcpy(out + out_len, expr + start, i - start);
                out_len += i - start;
            }
        } else if (expr[i] == '\'') {
            out[out_len++] = expr[i++];
            while (i < length && expr[i] != '\'') {
                if (expr[i] == '\\' && i + 1 < length) out[out_len++] = expr[i++];
                out[out_len++] = expr[i++];
            }
            if (i < length) out[out_len++] = expr[i++];
        } else {
            out[out_len++] = expr[i++];
        }
    }

    out[out_len] = '\0';
    return out;
}

/* Evaluates the condition of #if / #elif. A malformed expression counts as false */
static int eval_condition(const char *expr, MacroTable *macros, ActiveMacros *active)
{
    char *substituted = substitute_defined(expr, macros);
    LineBuf lb;
    Eval e;
    long long value;

    if (substituted == NULL) return 0;

    lb_init(&lb);
    if (!expand_text(&lb, substituted, strlen(substituted), 1, 0, macros, active) ||
        !lb_put(&lb, '\0', 0)) {
        free(substituted);
        lb_free(&lb);
        return 0;
    }

    e.p = lb.text;
    e.failed = 0;
    e.depth = 0;
    value = ev_ternary(&e);
    ev_skip(&e);
    if (*e.p != '\0') e.failed = 1;

    free(substituted);
    lb_free(&lb);
    return !e.failed && value != 0;
}

/* Conditional inclusion stack (one per file) */

typedef struct {
    int parent_active;   /* enclosing region is being emitted */
    int taken;           /* some branch of this #if has already been taken */
    int active;          /* the current branch is being emitted */
} CondFrame;

typedef struct {
    CondFrame *items;
    size_t count;
    size_t capacity;
} CondStack;

static int cond_active(const CondStack *stack)
{
    return stack->count == 0 || stack->items[stack->count - 1].active;
}

static int cond_push(CondStack *stack, int parent_active, int condition)
{
    if (stack->count == stack->capacity) {
        size_t new_capacity = (stack->capacity == 0) ? 8 : stack->capacity * 2;
        CondFrame *new_items = realloc(stack->items, new_capacity * sizeof(CondFrame));
        if (new_items == NULL) return 0;
        stack->items = new_items;
        stack->capacity = new_capacity;
    }
    CondFrame *frame = &stack->items[stack->count++];
    frame->parent_active = parent_active;
    frame->taken = condition;
    frame->active = parent_active && condition;
    return 1;
}

/* Macro name tested by #ifdef / #ifndef */
static int name_is_defined(const MacroTable *macros, const char *argument)
{
    char name[256];
    size_t length = 0;

    while (isspace((unsigned char)*argument)) argument++;
    while (is_identifier_char(argument[length]) && length < sizeof(name) - 1) {
        name[length] = argument[length];
        length++;
    }
    name[length] = '\0';
    return length > 0 && macro_find(macros, name) != NULL;
}

static int process_buffer(FILE *output, PreprocessedFile *pp, int file_index, const char *filename,
                          const char *buffer, size_t length, MacroTable *macros,
                          ActiveMacros *active, IncludeStack *inc_stack)
{
    char *without_comments;
    size_t clean_length, position = 0;
    int line_no = 0, result = 0;
    char *joined = NULL;
    CondStack conds = {NULL, 0, 0};

    if (!remove_comments(buffer, length, &without_comments, &clean_length)) return 0;

    while (position < clean_length) {
        size_t line_start = position;
        while (position < clean_length && without_comments[position] != '\n') position++;
        size_t line_end = position;
        if (position < clean_length) position++;   /* skip the '\n' */
        int this_line = ++line_no;

        const char *line_ptr = without_comments + line_start;
        size_t line_length = line_end - line_start;

        /* Line splicing: a trailing backslash joins the next physical line.
         * The logical line keeps the number of its first physical line */
        free(joined);
        joined = NULL;
        if (line_length > 0 && line_ptr[line_length - 1] == '\\' && line_end < clean_length) {
            size_t joined_length = 0, cur_start = line_start, cur_end = line_end;

            joined = malloc(clean_length - line_start + 1);
            if (joined == NULL) goto done;

            for (;;) {
                int continued = (cur_end > cur_start && without_comments[cur_end - 1] == '\\' &&
                                cur_end < clean_length);
                size_t take = continued ? cur_end - 1 - cur_start : cur_end - cur_start;

                memcpy(joined + joined_length, without_comments + cur_start, take);
                joined_length += take;
                if (!continued) break;

                cur_start = cur_end + 1;
                cur_end = cur_start;
                while (cur_end < clean_length && without_comments[cur_end] != '\n') cur_end++;
                line_no++;
                position = (cur_end < clean_length) ? cur_end + 1 : cur_end;
            }
            joined[joined_length] = '\0';
            line_ptr = joined;
            line_length = joined_length;
        }

        char *directive = NULL, *argument = NULL;
        int directive_result = parse_directive(line_ptr, line_length, &directive, &argument);

        if (directive_result == -1) goto done;

        if (directive_result == 1) {
            /* Directives emit no output line, but line_no keeps counting so
             * the rest of the file keeps its original numbering */
            int emitting = cond_active(&conds);
            int ok = 1;

            if (strcmp(directive, "if") == 0) {
                ok = cond_push(&conds, emitting, emitting && eval_condition(argument, macros, active));
            } else if (strcmp(directive, "ifdef") == 0) {
                ok = cond_push(&conds, emitting, name_is_defined(macros, argument));
            } else if (strcmp(directive, "ifndef") == 0) {
                ok = cond_push(&conds, emitting, !name_is_defined(macros, argument));
            } else if (strcmp(directive, "elif") == 0) {
                if (conds.count > 0) {
                    CondFrame *frame = &conds.items[conds.count - 1];
                    if (frame->parent_active && !frame->taken) {
                        frame->active = eval_condition(argument, macros, active);
                        frame->taken = frame->active;
                    } else {
                        frame->active = 0;
                    }
                }
            } else if (strcmp(directive, "else") == 0) {
                if (conds.count > 0) {
                    CondFrame *frame = &conds.items[conds.count - 1];
                    frame->active = frame->parent_active && !frame->taken;
                    frame->taken = 1;
                }
            } else if (strcmp(directive, "endif") == 0) {
                if (conds.count > 0) conds.count--;
            } else if (emitting) {
                if (strcmp(directive, "define") == 0) {
                    ok = handle_define(macros, argument);
                } else if (strcmp(directive, "undef") == 0) {
                    char name[256];
                    size_t n = 0;
                    const char *a = argument;
                    while (is_identifier_char(a[n]) && n < sizeof(name) - 1) { name[n] = a[n]; n++; }
                    name[n] = '\0';
                    if (n > 0) macro_undef(macros, name);
                } else if (strcmp(directive, "pragma") == 0) {
                    if (strcmp(argument, "once") == 0) {
                        char *real = realpath(filename, NULL);
                        if (real != NULL) once_add(real);
                        free(real);
                    }
                } else if (strcmp(directive, "include") == 0) {
                    char name[PP_PATH_MAX], resolved[PP_PATH_MAX];
                    int is_system = 0;

                    if (parse_include_target(argument, name, sizeof(name), &is_system)) {
                        if (!resolve_include(filename, name, is_system, resolved, sizeof(resolved))) {
                            if (is_system) {
                                if (!process_builtin(output, pp, name, macros, active, inc_stack)) ok = 0;
                            } else {
                                include_problem(filename, this_line,
                                                "no se pudo abrir el archivo de cabecera \"%s\"", name);
                            }
                        } else {
                            char *real = realpath(resolved, NULL);
                            int circular = (real != NULL && include_stack_contains(inc_stack, real));
                            int skip_once = (real != NULL && once_contains(real));
                            free(real);

                            if (circular) {
                                include_problem(filename, this_line,
                                                "inclusion circular de \"%s\"", name);
                            } else if (!skip_once &&
                                        !process_file(output, pp, resolved, macros, active, inc_stack)) {
                                ok = 0;
                            }
                        }
                    }
                }
                /* Any other directive (#error, #line, ...) is dropped */
            }

            free(directive);
            free(argument);
            if (!ok) goto done;
            continue;
        }

        if (!cond_active(&conds)) continue;   /* inside a skipped #if branch */

        LineBuf lb;
        lb_init(&lb);

        if (!expand_text(&lb, line_ptr, line_length, 1, 0, macros, active)) {
            lb_free(&lb);
            goto done;
        }

        if (!emit_line(output, pp, file_index, this_line, &lb)) goto done;
    }

    result = 1;

done:
    free(joined);
    free(without_comments);
    free(conds.items);
    return result;
}

static int process_file(FILE *output, PreprocessedFile *pp, const char *filename,
                        MacroTable *macros, ActiveMacros *active, IncludeStack *inc_stack)
{
    char *real = realpath(filename, NULL);
    int pushed = include_stack_push(inc_stack, real != NULL ? real : filename);
    free(real);
    if (!pushed) return 0;

    int file_index = pp_add_file(pp, filename);
    if (file_index < 0) {
        include_stack_pop(inc_stack);
        return 0;
    }

    FILE *input = fopen(filename, "rb");
    if (input == NULL) {
        include_stack_pop(inc_stack);
        return 0;
    }

    fseek(input, 0, SEEK_END);
    long file_size = ftell(input);
    if (file_size < 0) {
        fclose(input);
        include_stack_pop(inc_stack);
        return 0;
    }
    fseek(input, 0, SEEK_SET);

    char *buffer = malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(input);
        include_stack_pop(inc_stack);
        return 0;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)file_size, input);
    buffer[bytes_read] = '\0';
    fclose(input);

    int result = process_buffer(output, pp, file_index, filename, buffer, bytes_read,
                                macros, active, inc_stack);

    free(buffer);
    include_stack_pop(inc_stack);
    return result;
}

int preprocessor_run(const char *input_path, PreprocessedFile *output)
{
    char template[] = TEMP_TEMPLATE;
    MacroTable macros;
    ActiveMacros active;
    IncludeStack inc_stack;

    output->path = NULL;
    output->files = NULL;
    output->file_count = 0;
    output->files_capacity = 0;
    output->lines = NULL;
    output->line_count = 0;
    output->lines_capacity = 0;
    once_free();

    int fd = mkstemp(template);
    if (fd == -1) return 0;

    FILE *temp = fdopen(fd, "wb");
    if (temp == NULL) {
        close(fd);
        unlink(template);
        return 0;
    }

    macro_table_init(&macros);
    active_init(&active);
    include_stack_init(&inc_stack);

    int result = process_file(temp, output, input_path, &macros, &active, &inc_stack);
    if (fclose(temp) != 0) result = 0;

    macro_table_free(&macros);
    active_free(&active);
    include_stack_free(&inc_stack);
    once_free();

    if (result) {
        output->path = copy_string(template);
        if (output->path == NULL) result = 0;
    }

    if (!result) {
        unlink(template);
        preprocessor_cleanup(output);
        return 0;
    }

    return 1;
}