#ifndef SYMTAB_H
#define SYMTAB_H

/* Returns a unique, permanent copy of text (equal names give equal pointers) */
const char *symtab_intern(const char *text);

/* Records a name in the current scope (typedef name or ordinary identifier) */
void symtab_declare(const char *name, int is_typedef);

/* Tells whether the innermost visible declaration of name is a typedef */
int symtab_is_typedef(const char *name);

/* Scope handling: enter a new scope / leave scopes until depth is reached */
int symtab_depth(void);
void symtab_enter_scope(void);
void symtab_restore(int depth);

void symtab_free(void);

#endif