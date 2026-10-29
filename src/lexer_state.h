#ifndef LEXER_STATE_H
#define LEXER_STATE_H

/* Text of the last token handed to the parser (the lookahead on an error) */
const char *lexer_last_lexeme(void);

/* Position just after the token that came before the last one */
void lexer_previous_end(int *line, int *col);

/* Number of '{' not yet closed */
int lexer_open_braces(void);

/* Asks the lexer to hand the parser a virtual ';' before the lookahead token
 * Ignored unless it is safe. */
void lexer_request_repair(void);

#endif