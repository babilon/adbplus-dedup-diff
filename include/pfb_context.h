/**
 * pfb_context.h
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
#include "carry_over.h"
#include "paths_list.h"

/**
 * Holds file name context information for a single file to be processed. A
 * shared pointer to the single DomainTree_t lives here in an array of size 1.
 * This struct is initialized as an array of size # of files to be processed.
 */
typedef struct pfb_context
{
	FILE *in_file;
	char *in_fname;
	// copied from stat info.
	__off_t file_size;
	/**
	 * Line numbers in 'in_fname' to carry over without modification. These are
	 * not inserted into the DomainTree. They are not omitted by any rules.
	 * Typically these refer to regex lines in the original file. Tracking these
	 * here preserves their original order in the input file. These are carried
	 * until pfb_consolidate().
	 */
	carry_over_t co;
	/**
	 * true indicates will attempt to allocate a buffer equal in length to the
	 * file size to then read the entire file into the in-memory buffer and then
	 * process.
	 *
	 * false to read the file in chunks.
	 */
	bool use_mem_buffer;
	char *mem_buffer;
} pfb_context_t;


// we do not know how many lines until the write has completed.
// can realloc the internal and keep track of the next..
// we know how many lines were read. that could be a starting point.
// realloc to a smaller size is also possible.
typedef struct LiteLineData
{
	// number of line_info_t in li
	size_len_t alloc;
#ifndef NDEBUG
	uint re_alloc_counter;
#endif

	// number of line_info_t in li that are used
	size_len_t used;
	// array of these
	line_info_t *li;
} LiteLineData_t;

typedef struct pfb_out_buffer
{
	// can this use the deferred array and still realloc?
	// but the pointer holding this pfb_out_buffer is changed and breaks all
	// else.
	char *buffer;
	// next index into buffer for insertion
	size_t next_idx;
	// size of buffer allocated
	size_t alloc_len;
	// collection of offsets into buffer
	LiteLineData_t litelines;
} pfb_out_buffer_t;

typedef struct pfb_out_context
{
	union {
	FILE *out_file;
	// holds the final full output when sorting step is an intermediate step to
	// compute a difference between two inputs.
	pfb_out_buffer_t *out_buffer;
	};

	// the filename of the final written-to-disk result.
	char *out_fname;

	// used as a temporary storage location when re-reading from the original
	// input files to write a de-duplicated sorted output.
	char *buffer;

	// how many entries were written. this excludes header lines and comments.
	size_t counter;

	// provides ability to direct the output to an alternate construct such as
	// another in-memory buffer. it might even take the input, parse them into
	// domainviews, drop comments and header sections, and jazz to prepare for a
	// subsequent operation e.g. diff.
	size_t (*writer_cb)(const char *buffer_to_write, size_len_t byte_count,
			struct pfb_out_context*);
} pfb_out_context_t;


typedef struct pfb_contexts
{
	pfb_context_t *begin_context;
	pfb_context_t *end_context;
} pfb_contexts_t;

typedef struct pfb_context_collect
{
	pfb_out_context_t out_context;
	pfb_contexts_t in_contexts;
} pfb_context_collect_t;

extern pfb_context_collect_t pfb_init_contexts_BUFFER(paths_list_t in_paths_list,
		pfb_out_buffer_t out_buffer[static 1]);

extern pfb_context_collect_t pfb_init_contexts(paths_list_t in_paths_list,
		const char out_fname[static 1]);

extern pfb_context_collect_t pfb_init_contexts_FILE(paths_list_t in_paths_list,
		FILE out_file[static 1]);

extern void pfb_free_contexts(struct pfb_contexts cs[static 1]);
extern void pfb_free_out_context(pfb_out_context_t c[static 1]);
extern void pfb_free_context_collect(pfb_context_collect_t c[static 1]);

extern void pfb_open_contexts(pfb_contexts_t in_contexts[static 1]);
extern void pfb_close_contexts(pfb_contexts_t in_contexts[static 1]);
extern void pfb_close_out_context(pfb_out_context_t c[static 1]);
extern void pfb_open_out_context(pfb_out_context_t c[static 1], bool append_output);

extern pfb_out_context_t pfb_init_out_context(const char *out_fname);
extern pfb_context_t pfb_context_from_FILE(FILE *tmp);
extern pfb_context_t pfb_context_from_BUFFER(pfb_out_buffer_t *buffer);
extern void pfb_free_context(struct pfb_context c[static 1]);

extern void pfb_free_out_buffer(pfb_out_buffer_t c[static 1]);
