#ifndef ERRORS_H
#define ERRORS_H

/* Turns bison's "syntax error, unexpected X, expecting Y" into a gcc-like
 * message in Spanish and reports it at the best position. */
void report_syntax_error(const char *bison_message);

#endif