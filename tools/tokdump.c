/* Prints the token stream returned by Get_Token()
 * Not part of the final program */
#include <stdio.h>
#include <stdlib.h>

#include "scanner.h"

const char *token_name(int token);

int main(int argc, char **argv)
{
    Token token;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <archivo>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!scanner_init(argv[1])) {
        fprintf(stderr, "No se pudo abrir '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    for (token = Get_Token(); token.type != TOKEN_EOF; token = Get_Token()) {
        printf("%4d:%-3d %-24s %s\n", token.line, token.column,
                token_name(token.type), token.lexeme != NULL ? token.lexeme : "");
        token_free(&token);
    }

    scanner_close();
    return EXIT_SUCCESS;
}