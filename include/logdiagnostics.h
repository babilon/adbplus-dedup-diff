#pragma once

#include <stdio.h>
#include <string.h>

#ifdef COLLECT_DIAGNOSTICS
#define BORROW_SPACES \
	char *spaces_str = nullptr; \
	do { \
		const char *tmp1 = __FILE__; \
		const char *tmp2 = __FUNCTION__; \
		size_t spaces = strlen(tmp1) + strlen(tmp2); \
		spaces_str = calloc(spaces + 1, sizeof(char)); \
		memset(spaces_str, ' ', spaces); \
		spaces_str[spaces] = '\0'; \
	} while(0)
#define RETURN_SPACES do { free(spaces_str); } while(0)
#endif

#define LOG_DIAG(clsname, ptr, fmt, ...) do { \
BORROW_SPACES; \
open_globalErrLog(); \
fprintf(get_globalErrLog(), "[%s:%s] %s %p\n" \
		" %s   " fmt, \
		__FILE__, __FUNCTION__, clsname, ptr, \
		spaces_str, __VA_ARGS__); \
close_globalErrLog(); \
RETURN_SPACES; \
} while(0)
