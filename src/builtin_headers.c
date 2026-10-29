#include <string.h>

#include "builtin_headers.h"

/* Minimal versions of the standard headers: just the typedefs and macros a
 * parser needs. Each piece is guarded so repeated or overlapping includes
 * never declare a typedef twice. */
static const struct { const char *name; const char *text; } BUILTIN_HEADERS[] = {
    {"sys/stat.h",
    "#ifndef _PP_MODE_T\n"
    "#define _PP_MODE_T\n"
    "typedef unsigned int mode_t;\n"
    "#endif\n"},
    {"fcntl.h",
    "#ifndef _PP_MODE_T\n"
    "#define _PP_MODE_T\n"
    "typedef unsigned int mode_t;\n"
    "#endif\n"},
    {"stddef.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_NULL\n"
    "#define _PP_NULL\n"
    "#define NULL ((void *)0)\n"
    "#endif\n"
    "#ifndef _PP_WCHAR_T\n"
    "#define _PP_WCHAR_T\n"
    "typedef int wchar_t;\n"
    "#endif\n"
    "#ifndef _PP_PTRDIFF_T\n"
    "#define _PP_PTRDIFF_T\n"
    "typedef long ptrdiff_t;\n"
    "#endif\n"
    "#ifndef _PP_OFFSETOF\n"
    "#define _PP_OFFSETOF\n"
    "#define offsetof(type, member) ((size_t)&((type *)0)->member)\n"
    "#endif\n"},
    {"stdio.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_NULL\n"
    "#define _PP_NULL\n"
    "#define NULL ((void *)0)\n"
    "#endif\n"
    "#ifndef _PP_FILE\n"
    "#define _PP_FILE\n"
    "typedef struct _IO_FILE FILE;\n"
    "typedef long fpos_t;\n"
    "#endif\n"},
    {"stdlib.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_NULL\n"
    "#define _PP_NULL\n"
    "#define NULL ((void *)0)\n"
    "#endif\n"
    "#ifndef _PP_WCHAR_T\n"
    "#define _PP_WCHAR_T\n"
    "typedef int wchar_t;\n"
    "#endif\n"
    "#ifndef _PP_DIV_T\n"
    "#define _PP_DIV_T\n"
    "typedef struct { int quot, rem; } div_t;\n"
    "typedef struct { long quot, rem; } ldiv_t;\n"
    "#endif\n"},
    {"string.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_NULL\n"
    "#define _PP_NULL\n"
    "#define NULL ((void *)0)\n"
    "#endif\n"},
    {"stdint.h",
    "#ifndef _PP_INTN\n"
    "#define _PP_INTN\n"
    "typedef signed char int8_t;\n"
    "typedef short int16_t;\n"
    "typedef int int32_t;\n"
    "typedef long int64_t;\n"
    "typedef unsigned char uint8_t;\n"
    "typedef unsigned short uint16_t;\n"
    "typedef unsigned int uint32_t;\n"
    "typedef unsigned long uint64_t;\n"
    "typedef long intptr_t;\n"
    "typedef unsigned long uintptr_t;\n"
    "typedef long intmax_t;\n"
    "typedef unsigned long uintmax_t;\n"
    "#endif\n"},
    {"inttypes.h",
    "#ifndef _PP_INTN\n"
    "#define _PP_INTN\n"
    "typedef signed char int8_t;\n"
    "typedef short int16_t;\n"
    "typedef int int32_t;\n"
    "typedef long int64_t;\n"
    "typedef unsigned char uint8_t;\n"
    "typedef unsigned short uint16_t;\n"
    "typedef unsigned int uint32_t;\n"
    "typedef unsigned long uint64_t;\n"
    "typedef long intptr_t;\n"
    "typedef unsigned long uintptr_t;\n"
    "typedef long intmax_t;\n"
    "typedef unsigned long uintmax_t;\n"
    "#endif\n"
    "#ifndef _PP_WCHAR_T\n"
    "#define _PP_WCHAR_T\n"
    "typedef int wchar_t;\n"
    "#endif\n"},
    {"stdbool.h",
    "#ifndef _PP_BOOL\n"
    "#define _PP_BOOL\n"
    "#define bool _Bool\n"
    "#define true 1\n"
    "#define false 0\n"
    "#endif\n"},
    {"stdarg.h",
    "#ifndef _PP_VA_LIST\n"
    "#define _PP_VA_LIST\n"
    "typedef char *va_list;\n"
    "#define va_arg(ap, type) (*(type *)(ap))\n"
    "#endif\n"},
    {"time.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_NULL\n"
    "#define _PP_NULL\n"
    "#define NULL ((void *)0)\n"
    "#endif\n"
    "#ifndef _PP_TIME_T\n"
    "#define _PP_TIME_T\n"
    "typedef long time_t;\n"
    "#endif\n"
    "#ifndef _PP_CLOCK_T\n"
    "#define _PP_CLOCK_T\n"
    "typedef long clock_t;\n"
    "#endif\n"},
    {"unistd.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_SSIZE_T\n"
    "#define _PP_SSIZE_T\n"
    "typedef long ssize_t;\n"
    "#endif\n"
    "#ifndef _PP_OFF_T\n"
    "#define _PP_OFF_T\n"
    "typedef long off_t;\n"
    "#endif\n"
    "#ifndef _PP_PID_T\n"
    "#define _PP_PID_T\n"
    "typedef int pid_t;\n"
    "#endif\n"
    "#ifndef _PP_UID_T\n"
    "#define _PP_UID_T\n"
    "typedef unsigned int uid_t;\n"
    "typedef unsigned int gid_t;\n"
    "#endif\n"},
    {"sys/types.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_SSIZE_T\n"
    "#define _PP_SSIZE_T\n"
    "typedef long ssize_t;\n"
    "#endif\n"
    "#ifndef _PP_OFF_T\n"
    "#define _PP_OFF_T\n"
    "typedef long off_t;\n"
    "#endif\n"
    "#ifndef _PP_PID_T\n"
    "#define _PP_PID_T\n"
    "typedef int pid_t;\n"
    "#endif\n"
    "#ifndef _PP_UID_T\n"
    "#define _PP_UID_T\n"
    "typedef unsigned int uid_t;\n"
    "typedef unsigned int gid_t;\n"
    "#endif\n"
    "#ifndef _PP_TIME_T\n"
    "#define _PP_TIME_T\n"
    "typedef long time_t;\n"
    "#endif\n"},
    {"setjmp.h",
    "#ifndef _PP_JMP_BUF\n"
    "#define _PP_JMP_BUF\n"
    "typedef long jmp_buf[8];\n"
    "#endif\n"},
    {"signal.h",
    "#ifndef _PP_SIG_ATOMIC\n"
    "#define _PP_SIG_ATOMIC\n"
    "typedef int sig_atomic_t;\n"
    "#endif\n"},
    {"wchar.h",
    "#ifndef _PP_SIZE_T\n"
    "#define _PP_SIZE_T\n"
    "typedef unsigned long size_t;\n"
    "#endif\n"
    "#ifndef _PP_WCHAR_T\n"
    "#define _PP_WCHAR_T\n"
    "typedef int wchar_t;\n"
    "#endif\n"
    "#ifndef _PP_WINT_T\n"
    "#define _PP_WINT_T\n"
    "typedef unsigned int wint_t;\n"
    "#endif\n"},
    {"wctype.h",
    "#ifndef _PP_WINT_T\n"
    "#define _PP_WINT_T\n"
    "typedef unsigned int wint_t;\n"
    "#endif\n"},
};

const char *builtin_header_text(const char *name)
{
    for (size_t i = 0; i < sizeof(BUILTIN_HEADERS) / sizeof(BUILTIN_HEADERS[0]); i++) {
        if (strcmp(BUILTIN_HEADERS[i].name, name) == 0) return BUILTIN_HEADERS[i].text;
    }
    return NULL;
}