#ifndef SCANNER_H
#define SCANNER_H

#include "token.h"

/* Opens the file and resets the position; returns 1 on success */
int scanner_init(const char *filename);

/* Main API: returns the next token (TOKEN_EOF at the end) */
Token Get_Token(void);

void scanner_close(void);

#endif