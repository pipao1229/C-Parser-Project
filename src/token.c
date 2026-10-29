#include <stdlib.h>

#include "token.h"

void token_free(Token *token)
{
    free(token->lexeme);
    token->lexeme = NULL;
}
