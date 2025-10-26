/**
 * main.c
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
#include "dedupdomains.h"
#include "pfb_context.h"
#include "pfb_prune.h"
#include "inputargs.h"
#include "logdiagnostics.h"
#include "tld_hash_context.h"
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include "pfb_prune.h"
#include "rw_pfb_csv.h"
#include "pfb_differ.h"
#include <time.h>

// when this is used, the line_info_t array on LiteLineData_t is allocated to
// hold the entire set of line_info_t.
static void collect_litelines(PortLineData_t const *const pld,
		pfb_context_t*, void* data)
{
	ASSERT(pld);
	ASSERT(data);
	LiteLineData_t *litelines = data;

	ASSERT(litelines);
	ASSERT(litelines->alloc > 0);
	ASSERT(litelines->li);
	ASSERT(litelines->used != litelines->alloc);

#if 0 // debug
	DEBUG_PRINTF("collected a line...\n");
	DEBUG_PRINTF("pld offset %ld\n", pld->li.offset);
	DEBUG_PRINTF("pld len    %u\n", pld->li.line_len);
	DEBUG_PRINTF("used idx = %u\n", litelines->used);
	DEBUG_PRINTF("\n---\n");
#endif
	litelines->li[litelines->used] = pld->li;
	litelines->used++;
}

/**
 * de-duplicate, sort, and write the final adlist to the specified output
 * context.
 *
 * this will eventually support reading inputs that have been sorted alongside
 * raw unsorted inputs.
 */
static void sort_adbplus_adlists(TLD_implementation_t tld_impl,
		pfb_context_collect_t in_pcc[static 1], bool include_carry_over)
{
	ASSERT(tld_impl.context);

	// in a world where many more file handles are involved than can be opened
	// simultaneously... opening what is necessary and closing afterwards makes
	// sense.
	pfb_open_contexts(&in_pcc->in_contexts);

	// open the files to verify all files can be read. open output file to
	// verify those can be written.
	pfb_read_all(tld_impl, &in_pcc->in_contexts);

	// append mode: if writing header and comments and regexes outside of this
	// area, then it needs to be true(?) otherwise it's always create a new
	// file.
	pfb_open_out_context(&in_pcc->out_context, false);

	// one input context means the carry over can be written to the output
	// context. when multiple inputs are independently de-duplicated, this would
	// have multiple input contexts to deal with paired with a single output
	// context.
	if(include_carry_over)
	{
		pfb_write_carry_over(in_pcc);
	}

	pfb_consolidate(tld_impl, &in_pcc->out_context);

	pfb_close_contexts(&in_pcc->in_contexts);
	pfb_close_out_context(&in_pcc->out_context);
}

static void free_litelines(LiteLineData_t c[static 1])
{
	ASSERT(c);
	free(c->li);
	c->used = 0;
	c->alloc = 0;
#ifndef NDEBUG
	c->re_alloc_counter = 0;
#endif
}

static FILE* open_tmp_file(char a_or_b)
{
	UNUSED(a_or_b);
#ifndef NDEBUG
	if(a_or_b == 'a')
		return fopen("tmp.a.out", "wb+");
	else if(a_or_b == 'b')
		return fopen("tmp.b.out", "wb+");
	else
#endif
		return tmpfile();
}

/**
 *
 * Diff a file and a directory containing one or more adlists:
 *
 * dedup.real <file> <directory
 * dedup.real <file1> <file2>
 * dedup.real <directory1> <directory2>
 * dedup.real <file1> <file2> -o <file.out>
 *
 * Deduplicate and sort a single file and write to file.out
 * dedup.real -D <file.adlist> -o <file.out> 
 *
 * Deduplicate and sort one or more files in a directory and write to file.out.
 * dedup.real -D <directory> -o <file.out>
 *
 * --
 * -m maximum size for in-memory allocation in MB?
 * -G flag to de-duplicate and consolidate a set of adlists
 * -D flag to de-duplicate a set of adlists and store into originals or copies.
 * -d flag to run on a set of directories (or do we infer by reading if is a
 *  file or directory?)
 *
 * -D -d <directory> -o <directory>
 * -D -x <extension> -f <filename>
 *
 * -G <filename> -d <directory>
 * -G <filename> -f <filename>
 *
 * -D <directory|.<extension> <directory>|[<filename1>, <filename2>, ...]
 * -G <filename> <directory>|[<filename1>, <filename2>, ...]
 *
 * in memory allocations can be done one file at a time with consolidation done
 * in sequence and a final sort done after all files loaded.
 *
 * will also have a library module component which can take a list of filenames
 * and perform actions on those files. there will not be a means to specify a
 * directory. only filenames. is there a point to have a directory option for
 * the executable?
 * 
 * for some cases, reading entire input into memory and writing final output to
 * the same input is optimal and efficient. otoh there are many cases where
 * original input is useful to compare against final output i.e. tests. is this
 * another option?
 */
// read one or more files and de-dup and sort to file(s):
// -D <output file extension> <list of files to read>|<directory>
//
// read two or more files and collectively de-dup and sort to an output file: 
// -G <output filename> <list of files to read>|<directory>
//
// for the output file extension: if a directory is specified, have to ignore
// files inside that have the same extension.
//
// specifying directory is far more convienent than listing a bunch of files.
//
// dedup and sort either *ONE* file or *ONE* directory ? and output extension.
// if is a group - two or more files or *ONE* directory and a single output file
// that is not one of the inputs and not inside the directory.
//
// option to sort each file in memory i.e. read entire file into RAM, run the
// stuff, then write to disk. one or two, a small handful of files, should be
// possible to load into RAM at a time.

// store a length for number of characters on the line to be read. an unsigned
// short should be enough as 65k characters on a line in an adblock file is
// already obnoxious. we can read into an unsigned int and check for overflow
// and discard the line if it's longer than the max unsigned short can hold.
//
// read multiple formats: adbplus, raw domain, regex(?) or strengths.

int main(int argc, char *const * argv)
{
#if defined(RELEASE) || defined(RELEASE_LOGGING)
	// smoke alarm for release builds. verify assert('invariant') is non-fatal.
	ASSERT(false && "smoke test for 'ASSERT'");
	assert(false && "smoke test for 'assert'");
#endif

	input_args_t flags;
	init_input_args(&flags);

	if(!parse_input_args(argc, argv, &flags))
	{
		exit(EXIT_FAILURE);
	}

	if(!silent_mode(&flags))
	{
		open_logfile(&flags);
		fprintf(get_logFile(&flags), "Prune duplicate entries from the following files:\n");
		//for(size_t i = 0; i < flags.num_files; i++)
		{
			//fprintf(get_logFile(&flags), "  %s\n", flags.filenames[i]);
		}

		close_logfile(&flags);
	}

#ifdef BUILD_TESTS
	if(flags.runtests)
	{
		extern void run_tests();
		run_tests();
		free_globalErrLog();
		return 0;
	}
#endif

	if(flags.deduplicate_mode)
	{
		TLD_implementation_t tld_impl = create_tld_hash_impl();
		ASSERT(tld_impl.context);

		pfb_context_collect_t pcc = pfb_init_contexts(flags.input_paths_list,
				flags.output_filename);

		free_input_args(&flags);

		// deduplicate, sort, and write to an output. output may be stdout or a
		// temporary file.
		sort_adbplus_adlists(tld_impl, &pcc, true);

		pfb_free_context_collect(&pcc);

		ASSERT(!pcc.in_contexts.begin_context);
		ASSERT(!pcc.in_contexts.end_context);

		free_tld_impl(&tld_impl);
		ASSERT(!tld_impl.context);
	}
	else if(!flags.in_memory_mode) // if use temp files
	{
		// temporary files to hold the deduplicated and sorted output of the two
		// input sets of files.
		FILE *tmpA = open_tmp_file('a');
		FILE *tmpB = open_tmp_file('b');

		// context collection for set A with a FILE based output context
		// this init might use a writer that collects LiteLineData
		pfb_context_collect_t pccA = pfb_init_contexts_FILE(flags.input_paths_A,
				tmpA);
		// context collection for set B with a FILE based output context
		pfb_context_collect_t pccB = pfb_init_contexts_FILE(flags.input_paths_B,
				tmpB);

		// final output context created with the specified output filename. when
		// output filename is nullptr, it falls back to stdout.
		pfb_out_context_t out_AvsB = pfb_init_out_context(flags.output_filename);

		// now safe to free the input arguments
		free_input_args(&flags);

		TLD_implementation_t tld_implA = create_tld_hash_impl();
		ASSERT(tld_implA.context);

		// deduplicate and sort set A; write to temporary file tmpA.
		sort_adbplus_adlists(tld_implA, &pccA, false);
		const size_t pccA_count = pccA.out_context.counter;
		UNUSED(pccA_count);
		pfb_free_context_collect(&pccA);
		DEBUG_PRINTF("pccA count=%lu\n", pccA_count);

		TLD_implementation_t tld_implB = create_tld_hash_impl();
		ASSERT(tld_implB.context);

		// deduplicate and sort set B; write to temporary file tmpB.
		sort_adbplus_adlists(tld_implB, &pccB, false);

		const size_t pccB_count = pccB.out_context.counter;
		UNUSED(pccB_count);
		pfb_free_context_collect(&pccB);
		DEBUG_PRINTF("pccB count=%lu\n", pccB_count);

		free_tld_impl(&tld_implA);
		free_tld_impl(&tld_implB);
		ASSERT(!tld_implA.context);
		ASSERT(!tld_implB.context);

		// output of the above is the number of domains - this gives a chance to
		// allocate ahead of time an array for the collection of line/offsets to
		// allocate for the comparison.
		//
		// create input contexts for the diff step. these contexts are
		// effectively the output of the previous sort calls. a sort must be
		// applied on input sets containing 2 or more files. if the input set
		// contains exactly 1 file and the file is marked as sorted, the sort
		// can be skipped. for initial phase, all inputs are assumed unsorted.
		pfb_context_t in_A = pfb_context_from_FILE(tmpA);
		pfb_context_t in_B = pfb_context_from_FILE(tmpB);

		LiteLineData_t litelines_A = {
			.alloc = pccA_count,
			.li = malloc(sizeof(line_info_t) * pccA_count),
		};
		CHECK_MALLOC(litelines_A.li);
		LiteLineData_t litelines_B = {
			.alloc = pccB_count,
			.li = malloc(sizeof(line_info_t) * pccB_count),
		};
		CHECK_MALLOC(litelines_B.li);

		// the file based diff is slow by nature of reading from files, de-dup,
		// write to a temp file, then re-read from disk again. expediting the
		// next step of diffing is not going to reduce the slowness from
		// previous steps. in this implementation, the first step shall be to
		// read each context again from the FILE or if it fits in memory from a
		// buffer, and create a LiteLineData for each line of interest. then do
		// the same for the B file.
		pfb_read_one_context(&in_A, collect_litelines, &litelines_A);
#ifndef NDEBUG
		DEBUG_PRINTF("A collected lines realloc'ed %u\n", litelines_A.re_alloc_counter);
		DEBUG_PRINTF("li alloc=%u\n", litelines_A.alloc);
		DEBUG_PRINTF("li used=%u\n\n", litelines_A.used);
		DEBUG_PRINTF("over head=%u\n", litelines_A.alloc - litelines_A.used);
		DEBUG_PRINTF("\n");
#endif

		pfb_read_one_context(&in_B, collect_litelines, &litelines_B);
#ifndef NDEBUG
		DEBUG_PRINTF("B collected lines realloc'ed %u\n", litelines_B.re_alloc_counter);
		DEBUG_PRINTF("li alloc=%u\n", litelines_B.alloc);
		DEBUG_PRINTF("li used=%u\n", litelines_B.used);
		DEBUG_PRINTF("over head=%u\n", litelines_B.alloc - litelines_B.used);
		DEBUG_PRINTF("\n");
#endif

		pfb_open_out_context(&out_AvsB, false);

		// diff the final output of the two input sets. the output of this is to
		// the FILE specified by the output context.
		diff_adbplus_adlists_FILE(&in_A, &litelines_A, &in_B, &litelines_B, &out_AvsB);

		free_litelines(&litelines_A);
		free_litelines(&litelines_B);

		pfb_free_context(&in_A);
		pfb_free_context(&in_B);
		pfb_free_out_context(&out_AvsB);
	}
	else // use in-memory buffers for the intermediate output.
	{
		DEBUG_PRINTF("in memory mode\n");

		const uint out_buffer_size = 4096;
		// temporary buffers to hold the deduplicated and sorted output of the
		// two input sets of files. a sort is required when the input set
		// contains 2 or more files.
		//
		// entirely in memory for large inputs is retardedly slow. 7+ seconds.
		pfb_out_buffer_t tmpA = {
			.buffer = malloc(out_buffer_size),
			.alloc_len = out_buffer_size,
			.next_idx = 0,
			.litelines = {},
		};
		CHECK_MALLOC(tmpA.buffer);
		pfb_out_buffer_t tmpB = {
			.buffer = malloc(out_buffer_size),
			.alloc_len = out_buffer_size,
			.next_idx = 0,
			.litelines = {},
		};
		CHECK_MALLOC(tmpB.buffer);

		// context collection for set A with a BUFFER based output context
		pfb_context_collect_t pccA = pfb_init_contexts_BUFFER(flags.input_paths_A,
				&tmpA);
		// context collection for set B with a BUFFER based output context
		pfb_context_collect_t pccB = pfb_init_contexts_BUFFER(flags.input_paths_B,
				&tmpB);

		// final output context created with the specified output filename. when
		// output filename is nullptr, it falls back to stdout.
		pfb_out_context_t out_AvsB = pfb_init_out_context(flags.output_filename);

		// now safe to free the input arguments
		free_input_args(&flags);

		TLD_implementation_t tld_implA = create_tld_hash_impl();
		ASSERT(tld_implA.context);

		// deduplicate and sort set A; write to in-memory buffer tmpA.
		sort_adbplus_adlists(tld_implA, &pccA, false);
		ASSERT(tmpA.litelines.li);
		ASSERT(tmpA.litelines.alloc > 0);
		ASSERT(tmpA.litelines.used > 0);
		ASSERT(tmpA.litelines.used < tmpA.litelines.alloc);
#if 0
		printf("A has %u\n", tmpA.litelines.used);
		for(size_t i = 0; i < tmpA.litelines.used; i++)
		{
			printf("li line_len[i=%lu]=%u\n", i, tmpA.litelines.li[i].line_len);
			printf("li offset[i=%lu]=%lu\n", i, tmpA.litelines.li[i].offset);
		}
		fflush(stdout);
#endif

		pfb_free_context_collect(&pccA);
		ASSERT(pccA.out_context.out_buffer == nullptr);

		// separate implementations after de-dup and sort, they're dangling
		// pointers. TODO fix dangling pointer issue.
		TLD_implementation_t tld_implB = create_tld_hash_impl();
		ASSERT(tld_implB.context);

		// deduplicate and sort set B; write to in-memory buffer tmpB.
		sort_adbplus_adlists(tld_implB, &pccB, false);
		ASSERT(tmpB.litelines.li);
		ASSERT(tmpB.litelines.alloc > 0);
		ASSERT(tmpB.litelines.used > 0);
		ASSERT(tmpB.litelines.used < tmpB.litelines.alloc);
#if 0
		printf("B has %u\n", tmpB.litelines.used);
		for(size_t i = 0; i < tmpB.litelines.used; i++)
		{
			printf("li line_len[i=%lu]=%u\n", i, tmpB.litelines.li[i].line_len);
			printf("li offset[i=%lu]=%lu\n", i, tmpB.litelines.li[i].offset);
		}
		fflush(stdout);
#endif

		pfb_free_context_collect(&pccB);
		ASSERT(pccB.out_context.out_buffer == nullptr);

		free_tld_impl(&tld_implA);
		free_tld_impl(&tld_implB);
		ASSERT(!tld_implA.context);
		ASSERT(!tld_implB.context);

		// create input contexts for the diff step. these contexts are
		// effectively the output of the previous sort calls. a sort must be
		// applied on input sets containing 2 or more files. if the input set
		// contains exactly 1 file and the file is marked as sorted, the sort
		// can be skipped. for initial phase, all inputs are assumed unsorted.
		// this becomes .. redundant? we never have an input buffer that is
		// based on an output buffer. it's already in memory and we have the
		// LiteLineData array. we begin diffing each line.

		pfb_open_out_context(&out_AvsB, false);

		// diff the final output of the two input sets. the output of this is to
		// the FILE specified by the output context.
		// this variation takes the out context which has the litelines..
		//
		// for a GUI which may show the diff, the output may be to yet another
		// buffer... or an array of the markers and sections that differ?
		ASSERT(pccA.out_context.out_buffer == nullptr);
		ASSERT(pccB.out_context.out_buffer == nullptr);
		ASSERT(tmpA.buffer);
		ASSERT(tmpA.alloc_len > 0);
		ASSERT(tmpA.litelines.li);
		ASSERT(tmpA.litelines.used > 0);
		ASSERT(tmpB.buffer);
		ASSERT(tmpB.alloc_len > 0);
		ASSERT(tmpB.litelines.li);
		ASSERT(tmpB.litelines.used > 0);
		diff_adbplus_adlists_BUFFER(&tmpA, &tmpB, &out_AvsB);

		pfb_free_out_context(&out_AvsB);

		pfb_free_out_buffer(&tmpA);
		pfb_free_out_buffer(&tmpB);
	}

	free_globalErrLog();

	return 0;
}
