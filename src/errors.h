#ifndef ERRORS_H
#define ERRORS_H

/* Turns bison's reports into a gcc-like message at the best position. */
void report_syntax_error(const char *bison_message);

#endif