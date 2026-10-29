#include <stdlib.h>
#include <string.h>

#include "symtab.h"

typedef struct {
    const char *name;   /* interned, compared by pointer */
    int is_typedef;
    int depth;
} Entry;

static char **names = NULL;
static size_t name_count = 0, name_capacity = 0;

static Entry *entries = NULL;
static size_t entry_count = 0, entry_capacity = 0;

static int current_depth = 0;

const char *symtab_intern(const char *text)
{
    for (size_t i = 0; i < name_count; i++) {
        if (strcmp(names[i], text) == 0) return names[i];
    }

    if (name_count == name_capacity) {
        size_t new_capacity = (name_capacity == 0) ? 256 : name_capacity * 2;
        char **new_names = realloc(names, new_capacity * sizeof(char *));
        if (new_names == NULL) return "";
        names = new_names;
        name_capacity = new_capacity;
    }

    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy == NULL) return "";
    memcpy(copy, text, length + 1);
    names[name_count++] = copy;
    return copy;
}

void symtab_declare(const char *name, int is_typedef)
{
    if (entry_count == entry_capacity) {
        size_t new_capacity = (entry_capacity == 0) ? 256 : entry_capacity * 2;
        Entry *new_entries = realloc(entries, new_capacity * sizeof(Entry));
        if (new_entries == NULL) return;
        entries = new_entries;
        entry_capacity = new_capacity;
    }

    entries[entry_count].name = name;
    entries[entry_count].is_typedef = is_typedef;
    entries[entry_count].depth = current_depth;
    entry_count++;
}

int symtab_is_typedef(const char *name)
{
    for (size_t i = entry_count; i > 0; i--) {
        if (entries[i - 1].name == name) return entries[i - 1].is_typedef;
    }
    return 0;
}

int symtab_depth(void)
{
    return current_depth;
}

void symtab_enter_scope(void)
{
    current_depth++;
}

/* Drops every entry declared deeper than depth. Safe to call with a depth the
 * parser reached before an error recovery */
void symtab_restore(int depth)
{
    while (entry_count > 0 && entries[entry_count - 1].depth > depth) entry_count--;
    if (depth < current_depth) current_depth = depth;
}

void symtab_free(void)
{
    for (size_t i = 0; i < name_count; i++) free(names[i]);
    free(names);
    free(entries);
    names = NULL;
    entries = NULL;
    name_count = name_capacity = 0;
    entry_count = entry_capacity = 0;
    current_depth = 0;
}