#include <stdio.h>
#include <string.h>

#include "parser.tab.h"
#include "diagnostics.h"
#include "errors.h"
#include "lexer_state.h"

#define MAX_EXPECTED 8
#define NAME_SIZE 64

/* Bison names for token categories, in Spanish */
static const struct { const char *english, *spanish; } CATEGORY_NAMES[] = {
    {"identifier", "un identificador"},
    {"type name", "un nombre de tipo"},
    {"decimal constant", "una constante decimal"},
    {"octal constant", "una constante octal"},
    {"hexadecimal constant", "una constante hexadecimal"},
    {"floating constant", "una constante flotante"},
    {"string literal", "una cadena de texto"},
    {"character constant", "una constante de caracter"},
    {"end of file", "el fin de archivo"}
};

/* Copies name without the double quotes bison adds to some tokens */
static void clean_name(const char *start, size_t length, char *out)
{
    if (length >= 2 && start[0] == '"' && start[length - 1] == '"') {
        start++;
        length -= 2;
    }
    if (length >= NAME_SIZE) length = NAME_SIZE - 1;
    memcpy(out, start, length);
    out[length] = '\0';
}

/* Spanish description of a token name */
static void describe(const char *name, char *out)
{
    for (size_t i = 0; i < sizeof(CATEGORY_NAMES) / sizeof(CATEGORY_NAMES[0]); i++) {
        if (strcmp(name, CATEGORY_NAMES[i].english) == 0) {
            snprintf(out, NAME_SIZE + 8, "%s", CATEGORY_NAMES[i].spanish);
            return;
        }
    }
    snprintf(out, NAME_SIZE + 8, "'%s'", name);
}

/* Joins "a", "a o b", "a, b o c" */
static void join_list(char items[][NAME_SIZE + 8], int count, char *out, size_t size)
{
    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        const char *separator = (i == 0) ? "" : (i == count - 1) ? " o " : ", ";
        size_t used = strlen(out);
        if (used + 1 < size) snprintf(out + used, size - used, "%s%s", separator, items[i]);
    }
}

void report_syntax_error(const char *bison_message)
{
    const char *unexpected = strstr(bison_message, "unexpected ");
    const char *expecting = strstr(bison_message, ", expecting ");
    char expected_raw[MAX_EXPECTED][NAME_SIZE];
    char expected_text[MAX_EXPECTED][NAME_SIZE + 8];
    char expected_joined[512], message[768], unexpected_name[NAME_SIZE];
    int expected_count = 0, at_eof = 0, wants_previous_end = 0, expects_semicolon = 0;
    int line = yylloc.first_line, col = yylloc.first_column;
    int len = yylloc.last_column - yylloc.first_column + 1;

    /* What bison found */
    unexpected_name[0] = '\0';
    if (unexpected != NULL) {
        const char *start = unexpected + strlen("unexpected ");
        const char *end = (expecting != NULL) ? expecting : start + strlen(start);
        clean_name(start, (size_t)(end - start), unexpected_name);
    }
    at_eof = (strcmp(unexpected_name, "end of file") == 0);

    /* What it was waiting for */
    if (expecting != NULL) {
        const char *cursor = expecting + strlen(", expecting ");
        while (*cursor != '\0' && expected_count < MAX_EXPECTED) {
            const char *separator = strstr(cursor, " or ");
            size_t length = (separator != NULL) ? (size_t)(separator - cursor) : strlen(cursor);

            clean_name(cursor, length, expected_raw[expected_count]);
            describe(expected_raw[expected_count], expected_text[expected_count]);
            if (strcmp(expected_raw[expected_count], ";") == 0 || strcmp(expected_raw[expected_count], ")") == 0 ||
                strcmp(expected_raw[expected_count], "]") == 0 || strcmp(expected_raw[expected_count], "}") == 0 ||
                strcmp(expected_raw[expected_count], ",") == 0) {
                wants_previous_end = 1;
            }
            if (strcmp(expected_raw[expected_count], ";") == 0) expects_semicolon = 1;
            expected_count++;
            cursor += length;
            if (separator != NULL) cursor += strlen(" or ");
        }
    }

    /* The offending token as the user wrote it */
    char found[NAME_SIZE + 8], before[NAME_SIZE + 12];
    if (at_eof) {
        snprintf(found, sizeof(found), "el fin de archivo");
        snprintf(before, sizeof(before), "del fin de archivo");
    } else {
        const char *lexeme = lexer_last_lexeme();
        snprintf(found, sizeof(found), "'%s'", lexeme[0] != '\0' ? lexeme : unexpected_name);
        snprintf(before, sizeof(before), "de %s", found);
    }

    /* Message */
    if (at_eof && lexer_open_braces() > 0) {
        snprintf(message, sizeof(message),
                "se esperaba '}' antes del fin de archivo (hay %d llave(s) sin cerrar)",
                lexer_open_braces());
    } else if (expected_count > 0) {
        join_list(expected_text, expected_count, expected_joined, sizeof(expected_joined));
        snprintf(message, sizeof(message), "se esperaba %s antes %s", expected_joined, before);
    } else if (at_eof) {
        snprintf(message, sizeof(message), "fin de archivo inesperado");
    } else {
        snprintf(message, sizeof(message), "token inesperado %s", found);
    }

    /* Position: a missing ';' ')' etc. is best shown right after the previous token */
    if (wants_previous_end || at_eof) {
        int previous_line, previous_col;
        lexer_previous_end(&previous_line, &previous_col);
        if (previous_line > 0 && previous_line < line) {
            line = previous_line;
            col = previous_col;
            len = 1;
        }
    }

    diag_error(line, col, len, "%s", message);

    /* Repair: a missing ';' is supplied so the recovery does not skip code */
    if (expects_semicolon) lexer_request_repair();
}