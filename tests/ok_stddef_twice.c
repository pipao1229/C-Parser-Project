#include <stdlib.h>
#include <stddef.h>
#include <stddef.h>
typedef struct { size_t len; ptrdiff_t off; } Buf;
static size_t twice(size_t n) { return n * 2; }
int main(void) { Buf b; b.len = twice(3); return (int)b.len; }