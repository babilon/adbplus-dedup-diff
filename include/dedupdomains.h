/**
 * dedupdomains.h
 *
 * Part of pfb_adbplus_dedup_diff
 *
 * Copyright (c) 2025 robert.babilon@gmail.com
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <assert.h>

#define UNUSED(x) (void)(x)

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#ifdef RELEASE
#define NIL_ASSERT
#elif defined(RELEASE_LOGGING)
#define LOG_AND_CONTINUE
#else
// This will dramatically slow the program down.
//#define HASH_DEBUG
#endif

#if defined(NIL_ASSERT)
#define ASSERT(x) do {} while(0)

#elif defined(EXIT_ON_FATAL)
#define ASSERT(x) do { \
	if(!(x)) { \
		ELOG_STDERR("[%s:%d] %s: 'ASSERT' failed: %s\n", __FILE__, __LINE__, __FUNCTION__, #x); \
		exit(EXIT_FAILURE); \
	} while(0)

#elif defined(LOG_AND_CONTINUE)
#define ASSERT(x) do { \
	if(!(x)) { \
		ELOG_STDERR("[%s:%d] %s: 'ASSERT' failed: %s\n", __FILE__, __LINE__, __FUNCTION__, #x); \
	} \
} while(0)
#else
#define ASSERT(x) assert((x))
#endif

typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;

// offset into file..
typedef long linenumber_t;
typedef ushort line_len_t;
typedef uint size_len_t;
typedef uchar subdomain_len_t;

#if !defined(NDEBUG) && 1
#define DEBUG_PRINTF(fmt, ...) do { \
	fprintf(stderr, fmt, ##__VA_ARGS__); \
} while(0)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

// can change to e.g. & to see the full buffer
extern const char LINE_TERMINAL;

extern void open_globalErrLog();
FILE *get_globalErrLog();
extern void close_globalErrLog();
extern void free_globalErrLog();

#define ELOG_STDERR(fmt, ...) do { \
	open_globalErrLog(); \
	fprintf(get_globalErrLog(), fmt, ##__VA_ARGS__); \
	close_globalErrLog(); \
} while(0)


#define CHECK_MALLOC(var) do { \
	if(!(var)) { \
		ASSERT(false && "fatal malloc: " #var); \
		exit(EXIT_FAILURE); \
	} \
} while(0)

#define CHECK_REALLOC(var, newsize) do { \
	void *__tmp = realloc(var, newsize); \
	if(!__tmp) { \
		free(var); \
		ASSERT(false && "fatal realloc: " #var); \
		exit(EXIT_FAILURE); \
	} \
	var = __tmp; \
} while(0)
