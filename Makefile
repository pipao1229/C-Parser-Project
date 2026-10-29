CC      = gcc
FLEX    = flex
BISON   = bison

CFLAGS  = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Isrc -Ibuild -g

TARGET  = cparser

OBJ = build/main.o build/token.o build/diagnostics.o build/preprocessor.o build/symtab.o build/source.o build/errors.o build/builtin_headers.o \
    build/yylex_bridge.o build/parser.tab.o build/scanner.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

# bison writes parser.tab.c and parser.tab.h (the latter defines the token codes)
build/parser.tab.c build/parser.tab.h: src/parser.y
	mkdir -p build
	$(BISON) -d -Wall -o build/parser.tab.c $<

# scanner.l includes parser.tab.h, so it depends on it
build/scanner.c: src/scanner.l build/parser.tab.h
	$(FLEX) -o $@ src/scanner.l

build/parser.tab.o: build/parser.tab.c
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

build/scanner.o: build/scanner.c
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

build/%.o: src/%.c build/parser.tab.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# ---- Dev tool: token dump ----
# token_names.c is generated from the enum bison writes in parser.tab.h
build/token_names.c: build/parser.tab.h
	echo '#include "parser.tab.h"' > $@
	echo 'const char *token_name(int token) { switch (token) {' >> $@
	sed -n 's/^ *\([A-Z][A-Z_0-9]*\) = [-0-9]*,\{0,1\}.*/    case \1: return "\1";/p' build/parser.tab.h >> $@
	echo '    default: return "?"; } }' >> $@

tokdump: build/token_names.o build/scanner.o build/token.o build/tokdump.o
	$(CC) $(CFLAGS) $^ -o $@

build/tokdump.o: tools/tokdump.c build/parser.tab.h
	$(CC) $(CFLAGS) -c $< -o $@

build/token_names.o: build/token_names.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---- Dev tool: preprocessed text and its origin ----
ppdump: build/ppdump.o build/preprocessor.o build/builtin_headers.o
	$(CC) $(CFLAGS) $^ -o $@

build/ppdump.o: tools/ppdump.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) tokdump ppdump
	rm -f build/*.o build/scanner.c build/parser.tab.c build/parser.tab.h build/token_names.c

.PHONY: all clean