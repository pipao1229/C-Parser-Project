#ifndef TOKEN_H
#define TOKEN_H

/* A token type is one of the codes bison defines. Stored as int so this header does not depend on generated code */
#define TOKEN_EOF 0

typedef struct {
    int type;       /* fine-grained token code, or TOKEN_EOF */
    char *lexeme;   /* exact text, free with token_free() */
    int line;       /* start line (1-based) */
    int column;     /* start column (1-based, in bytes) */
    int length;     /* lexeme length in bytes */
} Token;

/* Frees the lexeme of a token */
void token_free(Token *token);

#endif