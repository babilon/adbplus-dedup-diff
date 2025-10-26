/**
 * inputargs.h
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
#include "dedupdomains.h"
#include "paths_list.h"
#include <stdio.h>

typedef struct input_args
{
	/**
	 * 'b' command line option to read input files into memory for in-memory
	 * deduplication and sorting.
	 */
	bool use_shared_buffer;
	/**
	 * In conjunction with 'b', specifies the maximum size in MB for each input
	 * buffer that can be held in memory.
	 */
	uint in_memory_buffer_size;

	/**
	 * 's' set to false to print or log file (if provided) diagnostics and
	 * progress.
	 */
	bool silent_flag;

#ifdef BUILD_TESTS
	/**
	 * 't' set to true to run internal unit tests.
	 */
	bool runtests;
#endif

	/**
	 * 'l' set to true and specify a log file to write diagnostics and progress
	 * to a log file.
	 */
	bool log_flag;
	/**
	 * when log_flag is true, specify a file to append diagnostics and progress
	 * to.
	 */
	char *log_fname;

	/**
	 * Deduplicate, sort, and consolidate a single set of input. If this is
	 * false, the mode is compute the difference between two sets of inputs.
	 */
	bool deduplicate_mode;

	/**
	 * If true, will make every effort to load all of the input sources into
	 * memory, compute the de-dup, sort, and difference in memory, and write to
	 * disk/stdout only the final output.
	 */
	bool in_memory_mode;

	/**
	 * Flag to write the deduplicated sorted output to binary format.
	 * If writing to stdout, the output is always plain text.
	 */
	bool export_binary_fmt;

	/**
	 * write_to_output_file mode is true, the difference or de-duplicated
	 * results will be written to disk instead of stdout.
	 */
	bool write_to_output_file;
	/**
	 * Output filename for group dedup 'n sort
	 *
	 * A value of nullptr indicates to write to stdout.
	 */
	const char* output_filename;

	// will hold all input paths.
	// TODO the input files may be read to confirm they're ascii. no telling
	// what happens between the initial check and the time they're read for
	// processing. the read portion might have to do a check at that time to
	// confirm they're ascii.
	union {
		paths_list_t input_paths_list;
		paths_list_t input_paths_A;
	};
	/**
	 * Used for the diff mode for the second set of inputs
	 */
	paths_list_t input_paths_B;

	/**
	 * FILE for writing diagnostics and progress to. default is stdout.
	 */
	FILE *outFile;
	FILE *errFile;

	bool errLog_flag;
	char const *errLog_fname;

	const char *algorithm;

} input_args_t;

extern void init_input_args(input_args_t flags[static 1]);
extern void free_input_args(input_args_t flags[static 1]);
extern bool silent_mode(const input_args_t flags[static 1]);
extern bool parse_input_args(int argc, char * const* argv, input_args_t flags[static 1]);

extern bool open_logfile(input_args_t flags[static 1]);
extern FILE *get_logFile(input_args_t iargs[static 1]);
extern void close_logfile(input_args_t flags[static 1]);

#define LOG_IFARGS(args, fmt, ...) do { \
	if(!silent_mode(args)) { \
		open_logfile(args); \
		fprintf(get_logFile(args), fmt, ##__VA_ARGS__); \
		close_logfile(args); \
	} \
} while(0)
