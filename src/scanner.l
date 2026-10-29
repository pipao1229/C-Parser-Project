%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.tab.h"   /* token codes only */
#include "scanner.h"
#include "token.h"

/* Flex's function is renamed so yylex stays free for the bison adapter;
 * Get_Token() is the scanner's only public entry point. */
#define YY_DECL static int flex_next(void)

/* Cursor (1-based). Columns count bytes (tabs are not expanded) so they
 * index directly into the source line. */
static int sc_line = 1;
static int sc_col = 1;

/* Location of the last matched lexeme */
static int tok_line = 1;
static int tok_col = 1;
static int tok_len = 0;

static int at_eof = 0;

/* Runs before every action: records where the lexeme starts and advances
 * the cursor, so no rule has to track columns. */
#define YY_USER_ACTION      \
    tok_line = sc_line;     \
    tok_col  = sc_col;      \
    tok_len  = yyleng;      \
    sc_col  += yyleng;

static char *dup_text(const char *text);
%}

%option noyywrap
%option nodefault
%option nounput
%option noinput

DIGIT       [0-9]
HEX_DIGIT   [0-9a-fA-F]
OCT_DIGIT   [0-7]
LETTER      [a-zA-Z_]
IDENT       {LETTER}({LETTER}|{DIGIT})*

INT_SUFFIX  ([uU][lL]?|[uU](ll|LL)?|[lL][uU]?|(ll|LL)[uU]?)
FLOAT_EXP   ([eE][+-]?{DIGIT}+)
FLOAT_SUFF  [fFlL]

%%

[ \t\r\f\v]+    { /* column already advanced by YY_USER_ACTION */ }
\n              { sc_line++; sc_col = 1; }

    /* ---------- Keywords ---------- */

"void"          { return KW_VOID; }
"char"          { return KW_CHAR; }
"short"         { return KW_SHORT; }
"int"           { return KW_INT; }
"long"          { return KW_LONG; }
"float"         { return KW_FLOAT; }
"double"        { return KW_DOUBLE; }
"signed"        { return KW_SIGNED; }
"unsigned"      { return KW_UNSIGNED; }
"_Bool"         { return KW_BOOL; }
"_Complex"      { return KW_COMPLEX; }
"_Imaginary"    { return KW_IMAGINARY; }

"auto"          { return KW_AUTO; }
"register"      { return KW_REGISTER; }
"static"        { return KW_STATIC; }
"extern"        { return KW_EXTERN; }
"typedef"       { return KW_TYPEDEF; }

"const"         { return KW_CONST; }
"volatile"      { return KW_VOLATILE; }
"restrict"      { return KW_RESTRICT; }
"inline"        { return KW_INLINE; }

"struct"        { return KW_STRUCT; }
"union"         { return KW_UNION; }
"enum"          { return KW_ENUM; }

"if"            { return KW_IF; }
"else"          { return KW_ELSE; }
"switch"        { return KW_SWITCH; }
"case"          { return KW_CASE; }
"default"       { return KW_DEFAULT; }
"while"         { return KW_WHILE; }
"do"            { return KW_DO; }
"for"           { return KW_FOR; }
"goto"          { return KW_GOTO; }
"continue"      { return KW_CONTINUE; }
"break"         { return KW_BREAK; }
"return"        { return KW_RETURN; }

"sizeof"        { return KW_SIZEOF; }

    /* ---------- Numeric constants ---------- */
    /* On equal-length matches the earlier rule wins, so order matters. */

0[xX]{HEX_DIGIT}+{INT_SUFFIX}?      { return HEX_LITERAL; }
0{OCT_DIGIT}+{INT_SUFFIX}?          { return OCT_LITERAL; }
0{OCT_DIGIT}*[89]{DIGIT}*           { return LEX_BAD_OCTAL; }
{DIGIT}+{INT_SUFFIX}?               { return DEC_LITERAL; }

{DIGIT}+"."{DIGIT}*{FLOAT_EXP}?{FLOAT_SUFF}?    { return FLOAT_LITERAL; }
"."{DIGIT}+{FLOAT_EXP}?{FLOAT_SUFF}?            { return FLOAT_LITERAL; }
{DIGIT}+{FLOAT_EXP}{FLOAT_SUFF}?                { return FLOAT_LITERAL; }

    /* Digits glued to letters (123abc, 0xZZ): only wins when longer than
     * any valid constant. */
{DIGIT}({LETTER}|{DIGIT})*          { return LEX_BAD_NUMBER; }

    /* ---------- Identifiers ---------- */

{IDENT}         { return IDENTIFIER; }
    /* TYPE_NAME is decided by the bison adapter (yylex_bridge.c) */

    /* ---------- Strings and characters ---------- */

\"([^\\\"\n]|\\.)*\"        { return STRING_LITERAL; }
\"([^\\\"\n]|\\.)*          { return LEX_UNTERMINATED_STRING; }

\'\'                        { return LEX_EMPTY_CHAR; }
\'([^\\\'\n]|\\.)+\'        { return CHAR_LITERAL; }
\'([^\\\'\n]|\\.)*          { return LEX_UNTERMINATED_CHAR; }

    /* ---------- Operators and delimiters ---------- */
    /* Flex takes the longest match: "<<=" beats "<<" and "<". */

"+"     { return PLUS; }
"-"     { return MINUS; }
"*"     { return STAR; }
"/"     { return SLASH; }
"%"     { return PERCENT; }
"++"    { return INCREMENT; }
"--"    { return DECREMENT; }

"="     { return ASSIGN; }
"+="    { return ADD_ASSIGN; }
"-="    { return SUB_ASSIGN; }
"*="    { return MUL_ASSIGN; }
"/="    { return DIV_ASSIGN; }
"%="    { return MOD_ASSIGN; }
"<<="   { return SHL_ASSIGN; }
">>="   { return SHR_ASSIGN; }
"&="    { return AND_ASSIGN; }
"^="    { return XOR_ASSIGN; }
"|="    { return OR_ASSIGN; }

"=="    { return EQ; }
"!="    { return NEQ; }
"<"     { return LT; }
">"     { return GT; }
"<="    { return LE; }
">="    { return GE; }

"&&"    { return LOGICAL_AND; }
"||"    { return LOGICAL_OR; }
"!"     { return BANG; }

"&"     { return AMPERSAND; }
"|"     { return PIPE; }
"^"     { return CARET; }
"~"     { return TILDE; }
"<<"    { return SHL; }
">>"    { return SHR; }

"."     { return DOT; }
"->"    { return ARROW; }
"?"     { return QUESTION; }
":"     { return COLON; }

"("     { return LPAREN; }
")"     { return RPAREN; }
"["     { return LBRACKET; }
"]"     { return RBRACKET; }
"{"     { return LBRACE; }
"}"     { return RBRACE; }
";"     { return SEMICOLON; }
","     { return COMMA; }
"..."   { return ELLIPSIS; }

    /* ---------- Lexical errors ---------- */
    /* Returned as tokens of their own; the bison adapter reports and drops them. */

[\x80-\xFF]+    { return LEX_NON_ASCII; }

.               { return LEX_BAD_CHAR; }

<<EOF>>         {
                    /* End-of-file location, used for "unexpected end of file" errors */
                    tok_line = sc_line;
                    tok_col = sc_col;
                    tok_len = 0;
                    return TOKEN_EOF;
                }

%%

static char *dup_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

int scanner_init(const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        return 0;
    }

    yyrestart(file);
    sc_line = 1;
    sc_col = 1;
    at_eof = 0;
    return 1;
}

/* Returns the next token (bison code, lexeme, position). Free the lexeme
 * with token_free(). Keeps returning TOKEN_EOF once the input ends. */
Token Get_Token(void)
{
    Token token;

    if (at_eof) {
        token.type = TOKEN_EOF;
        token.lexeme = NULL;
        token.line = sc_line;
        token.column = sc_col;
        token.length = 0;
        return token;
    }

    token.type = flex_next();
    token.line = tok_line;
    token.column = tok_col;
    token.length = tok_len;

    if (token.type == TOKEN_EOF) {
        at_eof = 1;
        token.lexeme = NULL;
    } else {
        token.lexeme = dup_text(yytext);
    }

    return token;
}

void scanner_close(void)
{
    if (yyin != NULL) {
        fclose(yyin);
        yyin = NULL;
    }
}
