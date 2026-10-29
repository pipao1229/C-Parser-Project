#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "parser.tab.h"
#include "diagnostics.h"
#include "scanner.h"
#include "symtab.h"
#include "lexer_state.h"

int yylex(void);

/* State describing the last tokens handed to the parser (used for error messages) */
static char last_lexeme[64] = "";
static int last_end_line = 0, last_end_col = 0;
static int previous_end_line = 0, previous_end_col = 0;
static int open_braces = 0, open_parens = 0;

/* Repair of a missing ';': a virtual ';' is handed to the parser, followed by
 * the token that was already read (the lookahead), which is replayed as is. */
typedef struct {
    int type;
    YYSTYPE value;
    YYLTYPE loc;
    int end_line, end_col;
    char lexeme[64];
} SavedToken;

static SavedToken saved;
static int saved_valid = 0;
static int repair_armed = 0, replay_pending = 0, last_was_virtual = 0, just_replayed = 0;

const char *lexer_last_lexeme(void)
{
    return last_lexeme;
}

void lexer_previous_end(int *line, int *col)
{
    *line = previous_end_line;
    *col = previous_end_col;
}

int lexer_open_braces(void)
{
    return open_braces;
}

/* Tokens that can only begin a new statement or declaration */
static int starts_statement(int type)
{
    switch (type) {
        case IDENTIFIER: case TYPE_NAME: case INCREMENT: case DECREMENT: case STAR:
        case KW_IF: case KW_FOR: case KW_WHILE: case KW_DO: case KW_SWITCH:
        case KW_RETURN: case KW_BREAK: case KW_CONTINUE: case KW_GOTO:
        case KW_VOID: case KW_CHAR: case KW_SHORT: case KW_INT: case KW_LONG:
        case KW_FLOAT: case KW_DOUBLE: case KW_SIGNED: case KW_UNSIGNED: case KW_BOOL:
        case KW_STRUCT: case KW_UNION: case KW_ENUM: case KW_CONST: case KW_VOLATILE:
        case KW_STATIC: case KW_EXTERN: case KW_REGISTER: case KW_AUTO: case KW_TYPEDEF:
            return 1;
        default:
            return 0;
    }
}

/* Called after reporting "missing ';'". If the offending token starts a new
 * statement on a later line (and we are not inside parentheses), the parser
 * will be given a virtual ';' before it, so nothing has to be skipped. */
void lexer_request_repair(void)
{
    if (!saved_valid || last_was_virtual || just_replayed || open_parens > 0) return;
    if (!starts_statement(saved.type)) return;
    if (previous_end_line == 0 || previous_end_line >= saved.loc.first_line) return;
    repair_armed = 1;
}

/* Tells whether the type is one of the lexical error tokens */
static int is_lexical_error(int type)
{
    switch (type) {
        case LEX_BAD_CHAR:
        case LEX_NON_ASCII:
        case LEX_BAD_OCTAL:
        case LEX_BAD_NUMBER:
        case LEX_UNTERMINATED_STRING:
        case LEX_EMPTY_CHAR:
        case LEX_UNTERMINATED_CHAR:
            return 1;
        default:
            return 0;
    }
}

static void report_lexical_error(const Token *token)
{
    const char *text = (token->lexeme != NULL) ? token->lexeme : "";

    switch (token->type) {
        case LEX_BAD_CHAR:
            if (isprint((unsigned char)text[0])) {
                diag_error(token->line, token->column, token->length,
                           "caracter inesperado '%c'", text[0]);
            } else {
                diag_error(token->line, token->column, token->length,
                           "caracter de control inesperado (0x%02X)",
                           (unsigned char)text[0]);
            }
            break;
        case LEX_NON_ASCII:
            diag_error(token->line, token->column, token->length,
                       "caracter no ASCII fuera de un string o caracter");
            break;
        case LEX_BAD_OCTAL:
            diag_error(token->line, token->column, token->length,
                       "digito invalido en constante octal '%s'", text);
            break;
        case LEX_BAD_NUMBER:
            diag_error(token->line, token->column, token->length,
                       "constante numerica invalida '%s'", text);
            break;
        case LEX_UNTERMINATED_STRING:
            diag_error(token->line, token->column, token->length,
                       "string sin cerrar");
            break;
        case LEX_EMPTY_CHAR:
            diag_error(token->line, token->column, token->length,
                       "constante de caracter vacia");
            break;
        case LEX_UNTERMINATED_CHAR:
            diag_error(token->line, token->column, token->length,
                       "constante de caracter sin cerrar");
            break;
        default:
            break;
    }
}

/* Called by bison. Pulls tokens from Get_Token(), reports and drops lexical
 * errors (the parser never sees them) and fills yylval/yylloc. */
int yylex(void)
{
    if (repair_armed) {
        /* Virtual ';' placed right after the previous token */
        repair_armed = 0;
        replay_pending = 1;
        last_was_virtual = 1;
        yylval.str = NULL;
        yylloc.first_line = yylloc.last_line = previous_end_line;
        yylloc.first_column = yylloc.last_column = previous_end_col;
        last_end_line = previous_end_line;
        last_end_col = previous_end_col;
        snprintf(last_lexeme, sizeof(last_lexeme), ";");
        return SEMICOLON;
    }

    if (replay_pending) {
        replay_pending = 0;
        last_was_virtual = 0;
        just_replayed = 1;
        yylval = saved.value;
        yylloc = saved.loc;
        previous_end_line = last_end_line;
        previous_end_col = last_end_col;
        last_end_line = saved.end_line;
        last_end_col = saved.end_col;
        snprintf(last_lexeme, sizeof(last_lexeme), "%s", saved.lexeme);
        return saved.type;
    }

    for (;;) {
        Token token = Get_Token();
        int type = token.type;

        if (is_lexical_error(type)) {
            report_lexical_error(&token);

            /* Malformed numbers, strings and characters still stand for a
             * value, so they go on to the parser as a literal of that kind.
             * Stray characters are dropped. */
            switch (type) {
                case LEX_BAD_OCTAL:
                case LEX_BAD_NUMBER:         type = DEC_LITERAL; break;
                case LEX_UNTERMINATED_STRING: type = STRING_LITERAL; break;
                case LEX_EMPTY_CHAR:
                case LEX_UNTERMINATED_CHAR:  type = CHAR_LITERAL; break;
                default:
                    token_free(&token);
                    continue;
            }
        }

        yylloc.first_line   = yylloc.last_line = token.line;
        yylloc.first_column = token.column;
        yylloc.last_column  = token.column + token.length - 1;

        /* Lexer hack: an identifier declared with typedef is a TYPE_NAME.
         * Names are interned, so the parser never has to free them. */
        if (type == IDENTIFIER) {
            yylval.str = symtab_intern(token.lexeme != NULL ? token.lexeme : "");
            if (symtab_is_typedef(yylval.str)) type = TYPE_NAME;
        }

        /* Remember what the parser has seen, for error reporting */
        previous_end_line = last_end_line;
        previous_end_col = last_end_col;
        last_end_line = token.line;
        last_end_col = token.column + token.length;
        snprintf(last_lexeme, sizeof(last_lexeme), "%s", token.lexeme != NULL ? token.lexeme : "");
        if (type == LBRACE) open_braces++;
        if (type == RBRACE && open_braces > 0) open_braces--;

        /* Parentheses cannot span braces, so a brace resets the count (this
         * keeps one unbalanced '(' from disabling repairs for the whole file) */
        if (type == LPAREN) open_parens++;
        if (type == RPAREN && open_parens > 0) open_parens--;
        if (type == LBRACE || type == RBRACE) open_parens = 0;

        just_replayed = 0;
        saved.type = type;
        saved.value = yylval;
        saved.loc = yylloc;
        saved.end_line = last_end_line;
        saved.end_col = last_end_col;
        snprintf(saved.lexeme, sizeof(saved.lexeme), "%s", last_lexeme);
        saved_valid = 1;

        token_free(&token);
        return type;
    }
}