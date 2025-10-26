/**
 * inputargs.c
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
#include <stdio.h>
#include <stdlib.h>
#include "inputargs.h"
#include "pfb_prune.h"
#include "version.nogit.h"
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#define ELOG_IFARGS(args, fmt, ...) do { \
	open_logfile(args); \
	fprintf(get_elogFile(args), fmt, ##__VA_ARGS__); \
	close_logfile(args); \
} while(0)

#ifdef BUILD_TESTS
#define TESTS_PRINTF
#endif

#define PATH_SEP_CHAR '/'


bool open_logfile(input_args_t iargs[static 1])
{
	assert(iargs && "tests only");
	if(iargs->log_fname)
	{
		iargs->outFile = fopen(iargs->log_fname, "ab");
		if(!iargs->outFile)
		{
			iargs->outFile = stdout;
			fprintf(stderr, "ERROR: Unable to open %s for append writing.\n", iargs->log_fname);
		}
		else
		{
			iargs->errFile = iargs->outFile;
			return true;
		}
	}
	return false;
}

void close_logfile(input_args_t iargs[static 1])
{
	assert(iargs && "tests only");
	if(iargs->log_fname)
	{
		ASSERT(iargs->outFile);
		ASSERT(iargs->errFile);
		ASSERT(iargs->outFile != stdout);
		ASSERT(iargs->errFile != stderr);
		fclose(iargs->outFile);
		iargs->outFile = stdout;
		iargs->errFile = stderr;
	}
}

FILE *get_logFile(input_args_t iargs[static 1])
{
	assert(iargs && "tests only");
#ifdef BUILD_TESTS
	UNUSED(iargs);
	return stdout;
#else
	return iargs->errFile == nullptr ? stderr : iargs->errFile;
#endif
}

static FILE *get_elogFile(input_args_t iargs[static 1])
{
	assert(iargs && "tests only");
#ifdef BUILD_TESTS
	UNUSED(iargs);
	return stderr;
#else
	return iargs->errFile == nullptr ? stderr : iargs->errFile;
#endif
}

typedef struct globalLog
{
	const char *log_fname;
	FILE *file;
} globalLog_t;

static globalLog_t *global_errLog = nullptr;
static globalLog_t *global_stdLog = nullptr;

void open_globalErrLog()
{
	if(global_errLog)
	{
		if(global_errLog->log_fname && global_errLog->file == nullptr)
		{
			global_errLog->file = fopen(global_errLog->log_fname, "ab");
			if(!global_errLog->file)
			{
				fprintf(stderr, "ERROR: Unable to open %s for append writing.\n", global_errLog->log_fname);
			}
		}
	}
}

void close_globalErrLog()
{
	if(global_errLog && global_errLog->log_fname && global_errLog->file &&
			global_errLog->file != stderr)
	{
		fclose(global_errLog->file);
		global_errLog->file = nullptr;
	}
}

FILE *get_globalErrLog()
{
	if(global_errLog && global_errLog->log_fname && global_errLog->file)
	{
		return global_errLog->file;
	}

	return stderr;
}

void free_globalErrLog()
{
	if(global_errLog)
	{
		free(global_errLog);
		global_errLog = nullptr;
	}
}

void append_filename_array(char ***filenames, size_t num_files[static 1],
		char entry[static 1])
{
	ASSERT(filenames);
	ASSERT(*entry);
	ASSERT(entry[0] != '\0');

	// not allowing an empty string
	if(entry[0] == '\0')
	{
		ELOG_STDERR("WARNING: Ignoring empty filename.\n");
		return;
	}

	(*num_files)++;
	CHECK_REALLOC(*filenames, sizeof(char*) * *num_files);
	(*filenames)[*num_files - 1] = entry;
}

void init_input_args(input_args_t iargs[static 1])
{
	memset(iargs, 0, sizeof(input_args_t));
	iargs->outFile = stdout;
	iargs->errFile = stderr;
}

void free_input_args(input_args_t iargs[static 1])
{
	for(uint i = 0; i < iargs->input_paths_A.len; i++)
	{
		free(iargs->input_paths_A.paths[i].path);
	}
	free(iargs->input_paths_A.paths);

	for(uint i = 0; i < iargs->input_paths_B.len; i++)
	{
		free(iargs->input_paths_B.paths[i].path);
	}
	free(iargs->input_paths_B.paths);

	memset(iargs, 0, sizeof(input_args_t));
}

static int extract_stat_info(const char path[static 1], pfb_stat_t pfb_s[static 1])
{
	ASSERT(path);
	ASSERT(stat);
	struct stat s;
	if(stat(path, &s) == 0)
	{
		pfb_s->file_size = s.st_size;
		pfb_s->st_dev = s.st_dev;
		pfb_s->st_ino = s.st_ino;
		return true;
	}
	
	if(errno == ENOENT)
	{
		pfb_s->file_size = 0;
		pfb_s->st_dev = 0;
		pfb_s->st_ino = 0;
		return true;
	}

	ELOG_STDERR("stat exited abnormally with errno: %d", errno);
	exit(EXIT_FAILURE);
	return false;
}

/**
 * Returns true if friendly diagnostic messages should be suppressed, i.e., to
 * NOT print supplemental information.
 */
bool silent_mode(const input_args_t iargs[static 1])
{
	return (iargs->silent_flag && !iargs->log_flag);
}

/**
 * getopt() dictates const-signature of 'argv'.
 */
static bool do_parse_input_args(int argc, char * const* argv,
		input_args_t iargs[static 1])
{
	int errorFlag = 0;
	char opt;

	// getopt(int, char * const *, char const *);
	while(errorFlag == 0 && (opt = getopt(argc, argv, ":vsL:tbaDMxo:E:")) != -1)
	{

		// without -D, the behavior is a differ: diff two input sets and write
		// the difference to the specified file. the file must not be in the
		// input set. the input set consists of two separate arguments; either
		// can be a file or directory. no more than two inputs.
		
		switch(opt)
		{
			case 'v':
				printf("Version: %s\n", VERSIONID);
				break;
			case 's':
				iargs->silent_flag = true;
				break;
			case 'L':
				iargs->log_flag = true;
				iargs->log_fname = optarg;
				break;
			case 't':
#ifndef BUILD_TESTS
				ELOG_IFARGS(iargs, "NOTICE: option -t (run built-in unit tests) will be ignored; binary was built without unit tests.\n");
#else
				iargs->runtests = true;
#endif
				break;
			case 'b':
				iargs->use_shared_buffer = true;
				// argument to set the maximum in-memory buffer size for input
				// files in MB. for example, 100MB.
				iargs->in_memory_buffer_size = atoi(optarg);
				break;
			case 'a':
				iargs->algorithm = optarg;
				break;
			case 'D':
				// de-duplicate and sort one or more files or directories
				// containing files. no validation on files other than the
				// reading of the files.
				iargs->deduplicate_mode = true;
				// the rest of the arguments are inputs to the deduplication
				// set. it might be worth allowing specifying multiple files
				// since that is what a group can be.. or a single directory? or
				// a directory and one or more files.
				//
				// to export a plain text copy of a binary version, run the
				// program with -D and the path to the binary format. it'll read
				// in the header and file and write the file to disk w/o the
				// binary header.
				break;
			case 'M':
				iargs->in_memory_mode = true;
				break;
			case 'x':
				// default will export to the binary format unless -o is omitted
				// and then stdout is used and only plaintext output.
				// or reverse this: default is plaintext. and the fancy programs
				// will specify binary when it is in control to output to the
				// protected location.
				iargs->export_binary_fmt = true;
				break;
			case 'o':
				// path to a file to write the output of the diff or
				// de-duplicated and sorted set to. default behavior writes
				// output to stdout.
				iargs->write_to_output_file = true;
				iargs->output_filename = optarg;
				break;
			case 'E':
				iargs->errLog_flag = true;
				iargs->errLog_fname = optarg;
				break;
			case ':':
				// when an option is specified w/o operands, this is handled.
				// this overrides default behavior showing the usage string.
				ELOG_IFARGS(iargs, "Option -%c requires an operand\n", optopt);
				errorFlag++;
				break;
			case '?':
			default:
				ELOG_IFARGS(iargs, "Usage: %s "
						"[-vstb] "
						"[-L <log file>] "
						"[-E <errlog file>] "
						"[-i <NUMBER>] "
						"[-r <NUMBER>] "
						"[-D <filename>|<directory>] "
						"[<file1>|<directory1>, <file2>|<directory2>] "
						"[-o <filename.out>] "
						"\n", argv[0]);
				errorFlag++;
				break;
		}
	}

	if(errorFlag)
	{
		return false;
	}

	if(iargs->log_flag)
	{
		if(!open_logfile(iargs))
		{
			errorFlag++;
			return false;
		}
		close_logfile(iargs);

		struct stat s;
		if(stat(iargs->log_fname, &s) == 0)
		{
			if(!S_ISREG(s.st_mode))
			{
				ELOG_STDERR("ERROR: '%s' is not a regular file\n", iargs->log_fname);
				errorFlag++;
				return false;
			}
		}
		else
		{
			ELOG_STDERR("ERROR: Unable to stat: %s\n", iargs->log_fname);
			errorFlag++;
			return false;
		}

		global_stdLog = calloc(1, sizeof(globalLog_t));
		CHECK_MALLOC(global_stdLog);
		global_stdLog->log_fname = iargs->log_fname;
	}

	if(iargs->errLog_flag)
	{
		global_errLog = calloc(1, sizeof(globalLog_t));
		CHECK_MALLOC(global_errLog);
		global_errLog->log_fname = iargs->errLog_fname;
	}

#ifdef BUILD_TESTS
	if(iargs->runtests)
	{
		return true;
	}
#endif

	if(iargs->write_to_output_file && !iargs->output_filename)
	{
		ELOG_IFARGS(iargs, "ERROR: output flag specified without a filename.\n");
		errorFlag++;
		return false;
	}

	if(iargs->deduplicate_mode)
	{
		// de-duplicate all the specified files and files in directories
		// the remaining args are files and directories
		for(int i = 0; i < argc; i++)
		{
			DEBUG_PRINTF("do_parse_input_args argv[i=%d]=%s\n", i, argv[i]);
		}
	}


	return true;
}

// TODO unused.
#if 0
static void log_action(const char fname[static 1], input_args_t iargs[static 1])
{
	if(silent_mode(iargs))
	{
		return;
	}

	char *tmp = nullptr;//outputfilename(fname, iargs->out_ext);
	if(tmp)
	{
		LOG_IFARGS(iargs, "   READ: %s\n  WRITE: %s\n", fname, tmp);
	}
	free(tmp);
}

/**
 * Return true if the given path is to a file. false if the given path is to a
 * directory.
 */
bool is_file(const char *const path, int *err)
{
	// this provides file size and inode info. it might be able to indicate two
	// files are identical?
	//
	struct stat s;
	*err = stat(path, &s);
	if(*err == 0)
	{
		DEBUG_PRINTF("info for %s\n", path);
		DEBUG_PRINTF("mode: %u\n", s.st_mode);
		DEBUG_PRINTF("device: %lu\n", s.st_dev);
		DEBUG_PRINTF("inode: %lu\n", s.st_ino);
		if(S_ISDIR(s.st_mode))
		{
			DEBUG_PRINTF("stat of %s says it is a directory\n", path);
			return false;
		}
		else if(S_ISLNK(s.st_mode))
		{
			DEBUG_PRINTF("stat of %s says it is a link\n", path);
			return false;
		}
		else if(S_ISREG(s.st_mode))
		{
			DEBUG_PRINTF("stat of %s says it is a regular file\n", path);
			return true;
		}
	}

	return false;
}

bool read_dir_filenames(input_args_t *iargs)
{
	struct stat s;
	// will need to consider every input as a directory or a file
	//if(stat(iargs->directory, &s) == 0)
	{
		if(!S_ISDIR(s.st_mode))
		{
			//ELOG_IFARGS(iargs, "ERROR: Expected '%s' to be a directory\n",
					//iargs->directory);
			return false;
		}
	}
	//else
	{
		//ELOG_IFARGS(iargs, "ERROR: Unable to stat directory '%s'\n",
				//iargs->directory);
		return false;
	}

	struct dirent *direntry;
	DIR *dir = nullptr;//opendir(iargs->directory);
	if(!dir)
	{
		//ELOG_IFARGS(iargs, "ERROR: Unable to open directory '%s'\n",
				//iargs->directory);
		return false;
	}

	char *tmp = nullptr;
	while((direntry = readdir(dir)) != nullptr)
	{
		struct stat s;

		const size_t len_dir = 0;
		const size_t len_entry = strlen(direntry->d_name);

		// null terminator plus 1 IFF no trailing '/'
		const uint extra = 0;//(iargs->directory[len_dir - 1] != PATH_SEP_CHAR);

		tmp = calloc(len_dir + len_entry + 1 + extra, sizeof(char));
		CHECK_MALLOC(tmp);

		//strcpy(tmp, iargs->directory);

		if(extra)
		{
			tmp[len_dir] = PATH_SEP_CHAR;
		}

		strcpy(tmp + len_dir + extra, direntry->d_name);

		if(stat(tmp, &s) == 0 && S_ISREG(s.st_mode))
		{
			const char *period = strrchr(direntry->d_name, '.');
			if(period && *(period + 1))
			{
				//if(strcmp(period, iargs->inp_ext) == 0)
				{
					LOG_IFARGS(iargs, "Found regular file with matching input extension: %s\n", tmp);
					// tmp is moved to filenames[].
					tmp = nullptr;

					log_action(direntry->d_name, iargs);
				}
				//else if(!silent_mode(iargs) && strcmp(period, iargs->out_ext) == 0)
				{
					LOG_IFARGS(iargs, "Found regular file with matching output extension: %s\n"
									  "WARNING: will overwrite %s\n", tmp, direntry->d_name);
				}
			}
		}
		free(tmp);
	}

	if(closedir(dir) != 0)
	{
		//ELOG_IFARGS(iargs, "WARNING: Unable to close directory '%s'.\n",
				//iargs->directory);
	}

	return true;
}
#endif

static paths_list_t create_paths_list(uint initial_size)
{
	paths_list_t list;
	list.alloced = initial_size;
	list.paths = malloc(sizeof(path_info_t) * list.alloced);
	CHECK_MALLOC(list.paths);
	list.len = 0;
	return list;
}

static void paths_list_add(paths_list_t list[static 1], const char* path,
		const struct stat s[static 1])
{
	ASSERT(list);
	ASSERT(path);
	ASSERT(path[0]);
	ASSERT(s);
	DEBUG_PRINTF("input path=%s\n", path);

	if(list->len >= list->alloced)
	{
		list->alloced += 3;
		CHECK_REALLOC(list->paths, sizeof(path_info_t) * list->alloced);
	}

	// TODO toggle use_mem_buffer here
	list->paths[list->len].use_mem_buffer = false;
	list->paths[list->len].path = pfb_strdup(path);
	list->paths[list->len].pfb_s.file_size = s->st_size;
	list->paths[list->len].pfb_s.st_dev = s->st_dev;
	list->paths[list->len].pfb_s.st_ino = s->st_ino;
	DEBUG_PRINTF("entry[%d]=%s\n", list->len, list->paths[list->len].path);
	list->len++;
}

/**
 * Given a path, return the realpath(s). if argv is a directory, reads all files
 * from within and returns a list of filenames.
 *
 * returns true if the input is a valid path.
 */
static void read_argv_path_append(const pfb_stat_t pfb_s,
		const char *const argv, paths_list_t pl[static 1])
{
	ASSERT(pl);
	ASSERT(pl->paths);
	ASSERT(pl->alloced > 0);

	struct stat s;
	int err = stat(argv, &s);
	if(err == 0)
	{
		DEBUG_PRINTF("info for %s\n", argv);
		DEBUG_PRINTF("mode: %u\n", s.st_mode);
		DEBUG_PRINTF("device: %lu\n", s.st_dev);
		DEBUG_PRINTF("inode: %lu\n", s.st_ino);

		if(S_ISREG(s.st_mode))
		{
			if(pfb_s.st_dev == s.st_dev && pfb_s.st_ino == s.st_ino)
			{
				ELOG_STDERR("WARNING: IGNORING input %s which is identical to the output path\n", argv);
			}
			else
			{
				FILE *f = fopen(argv, "rb");
				if(!f)
				{
					ELOG_STDERR("WARNING: failed to open %s for reading in binary mode\n", argv);
					return;
				}
				const uint first_bytes = 50;
				char buffer[first_bytes];
				const uint read = fread(buffer, sizeof(char), first_bytes, f);
				// a simple heuristic to check for binary data in the file. if
				// found, we'll presume this is not a plain text file.
				// probably better to do a check that all characters are in the
				// 127 range? but whatabout unicode? but we're only supporting
				// domains.. and unicode doesn't have '\0', right?
				const char *ps = memchr(buffer, '\0', read);
				if(ps)
				{
					// TODO enhance the check to be more reliable.
					ELOG_STDERR("WARNING: IGNORING non-ascii input file %s\n", argv);
				}
				fclose(f);

				if(!ps)
				{
					DEBUG_PRINTF("adding input %s to the list\n", argv);
					// the input arg is a regular file. add it to the set.
					// done here. unless this input is exactly the same as the output...
					paths_list_add(pl, argv, &s);
				}
			}
		}

		// the input is a directory. iterate the contents of it selecting files.
		// recursive iteration may be a future enhancement. if an input in here
		// is a link to another, that is garbage input. if an input in here is a
		// link to the output or is the output, then this fails.
		else if(S_ISDIR(s.st_mode))
		{
			struct dirent *direntry;
			DIR *dir = opendir(argv);
			if(!dir)
			{
				ELOG_STDERR("ERROR: failed to open directory %s\n", argv);
				return;
			}

			char *tmp = nullptr;
			size_t alloc_len = 0;
			const size_t len_dir = strlen(argv);

			while((direntry = readdir(dir)) != nullptr)
			{
				const size_t len_entry = strlen(direntry->d_name);
				const uint extra = (argv[len_dir - 1] != PATH_SEP_CHAR);
				const size_t request_alloc = len_dir + len_entry + 1 + extra;
				if(alloc_len < request_alloc)
				{
					alloc_len = request_alloc;
					CHECK_REALLOC(tmp, request_alloc * sizeof(char));
				}
				ASSERT(tmp);
				strcpy(tmp, argv);

				if(extra)
				{
					tmp[len_dir] = PATH_SEP_CHAR;
				}

				strcpy(tmp + len_dir + extra, direntry->d_name);

				if(stat(tmp, &s) == 0 && S_ISREG(s.st_mode))
				{
					if(s.st_dev == pfb_s.st_dev && s.st_ino == pfb_s.st_ino)
					{
						ELOG_STDERR("WARNING: IGNORING regular file (%s) in dir %s which is identical to the output path\n",
								tmp, argv);
					}
					else
					{
						DEBUG_PRINTF("regular file %s is added to the list\n", tmp);
						paths_list_add(pl, tmp, &s);
					}
				}
			}
			free(tmp);
			if(closedir(dir) != 0)
			{
				ELOG_STDERR("WARNING: Unable to close directory '%s'.\n", argv);
			}
		}
	}
}

static void read_argv_path(const char *const argv, paths_list_t pl[static 1])
{
	ASSERT(pl);
	ASSERT(pl->paths);
	ASSERT(pl->alloced > 0);

	struct stat s;
	stat(argv, &s);
	paths_list_add(pl, argv, &s);

	// if directory, read contents; add each file within to pl.
	// if file, add to pl
}


/**
 * do_parse_input_args() dictates the const-signature of 'argv'.
 */
bool parse_input_args(int argc, char *const * argv, input_args_t iargs[static 1])
{
	if(!do_parse_input_args(argc, argv, iargs))
	{
		return false;
	}

#ifdef BUILD_TESTS
	if(iargs->runtests)
	{
		return true;
	}
#endif

	const int remainder = argc - optind;

	// deduplicate mode is non-default behavior. the primary objective is this
	// will diff two sets of inputs and generate that diff. it can optionally
	// store de-duplicated sets of inputs for future diffing.

	if(iargs->deduplicate_mode)
	{
		DEBUG_PRINTF("dedup mode\n");
		DEBUG_PRINTF("remaining args count=%d\n", remainder);
		if(remainder == 0)
		{
			ELOG_STDERR("ERROR: expecting at least one path argument\n");
			return false;
		}
	}
	else
	{
		DEBUG_PRINTF("two inputs to compute a difference between.\n");
		DEBUG_PRINTF("remaining args count=%d\n", remainder);
		if(remainder <= 0 || remainder > 2)
		{
			ELOG_STDERR("ERROR: expecting exactly two path arguments\n");
			return false;
		}
	}

	// read ascii plain text input and write a de-duplicated sorted output.
	//
	// has a binary header containing a version number, size of file without the
	// header - useful for exporting the plaintext portion, a bit for sorted, a
	// uuid in binary indicating this program is the creator of the file, size
	// of the header block - useful for exporting plaintext portion.

	const_path_info_t output_path_info = {.path=iargs->output_filename};

	// all inputs collected and generate one output. output is plain text to
	// stdout unless an output file is specified. binary if -x is specified.
	// -x and no filename is garbage.
	if(iargs->write_to_output_file)
	{
		// perhaps this needs to open/create a file for output before getting
		// along too far. we can stat the output filename path and find if it
		// exists and is in the list. if it doesn't exist, it's ok to create
		// later. if it exists it is ok to consider - as long as it's not in the
		// input list.
		if(iargs->output_filename == nullptr || strlen(iargs->output_filename) == 0)
		{
			ELOG_STDERR("must specify a non-empty name to write to\n");
			return false;
		}
		else
		{
			// TODO check if file is writable; if not, fail.
			if(!extract_stat_info(output_path_info.path, &output_path_info.pfb_s))
			{
				// must avoid opening the output path prematurely; the path may
				// be in the list of input paths. opening with create mode will
				// zero the file!
				ELOG_STDERR("path specified for output is invalid\n");
				return false;
			}
		}
	}

	if(iargs->deduplicate_mode)
	{
		ASSERT(remainder > 0);
		// have to take the args determine if directory or file and add all
		// filenames to the input args filenames.

		// allocate at least enough for the number of args assuming argv is file
		// paths. if directories are given, all bets are off and allocation
		// is done appropriately.
		iargs->input_paths_list = create_paths_list(remainder + 1);

		// have to do the same here as below. one difference is the set of
		// inputs below are separated into two groups. there's group A and group
		// B. a group can be a single file or a directory containing files.
		for(int i = optind; i < argc; i++)
		{
			DEBUG_PRINTF("parse_input_args:%d: argv[i=%d]=%s\n", __LINE__, i, argv[i]);
			// use realpath to get absolute path resolving . and .. and
			// symlinks. then compare this value to the others to avoid/ensure
			// the output destination is not in the set of inputs. the inputs
			// can overlap; that's user error and will only cause more work to
			// do the same. shouldn't be a problem.
			//
			// goal is to avoid overwriting an input file when using it as the
			// output.
			// argv[i] may be a path to a directory or a file or bogus.
			// the output is a list of pure file paths.
			// add these to the paths_list.
			read_argv_path_append(output_path_info.pfb_s, argv[i],
					&iargs->input_paths_list);
			// add this set of files to the overall collection.
			// it may as well compare each one now against the output path and
			// fail if one of these matches the output path.
		}
	}
	else // diff mode
	{
		ASSERT(remainder == 2);
		for(int i = optind; i < argc; i++)
		{
			DEBUG_PRINTF("parse_input_args:%d: argv[i=%d]=%s\n", __LINE__, i, argv[i]);
		}

		// there may be one file for A and one for B or a directory containing
		// multiple.
		iargs->input_paths_A = create_paths_list(2);
		iargs->input_paths_B = create_paths_list(2);

		// check for non-empty paths and valid paths
		// if any paths are empty, fail.
		// if any paths are invalid, fail.
		// if any paths are identical to the output, fail.
		// if any paths are symlinks, fail?
		read_argv_path(argv[optind],   &iargs->input_paths_A);
		read_argv_path(argv[optind+1], &iargs->input_paths_B);

		ASSERT(iargs->input_paths_A.len == 1);
		ASSERT(iargs->input_paths_B.len == 1);
	}

	return true;
}
