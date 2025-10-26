/*`
 * rw_pfb_csv.c
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
#include "rw_pfb_csv.h"
#include "pfb_context.h"
#include <stdlib.h>
#include <string.h>

// 4096 is *probably* a safe sane reasonable default.
static const size_t READ_BUFFER_SIZE = 4096;

// what is a reasonable length for a line in adbplus file?
static const size_t MAX_ACCEPTABLE_LINE_LENGTH = READ_BUFFER_SIZE * 0.5;
static const size_t MAX_ALLOC_LINE = MAX_ACCEPTABLE_LINE_LENGTH + 1; // for null terminator

/**
 * Holds a copy of line data. Guaranteed to be null terminated and correspond to
 * a line in the file that ended in \r and/or \n. The data will not contain \r
 * or \n.
 */
typedef struct LineData
{
	char *buffer;
	char *pos;
	size_t alloc;
	line_info_t li;
} LineData_t;

size_t default_buffer_len()
{
	return READ_BUFFER_SIZE;
}

size_t get_max_line_len()
{
	return MAX_ACCEPTABLE_LINE_LENGTH;
}

static void init_LineData(LineData_t ld[static 1])
{
	*ld = (LineData_t){};

	static const size_t initial_size = 100;

	ld->buffer = malloc(sizeof(char) * initial_size);
	CHECK_MALLOC(ld->buffer);

	ld->pos = ld->buffer;
	ld->alloc = initial_size;
}

static void free_LineData(LineData_t ld[static 1])
{
	ASSERT(ld);
	free(ld->buffer);
	*ld = (LineData_t){};
}

static void reset_LineData(LineData_t ld[static 1])
{
	ld->pos = ld->buffer;
	ld->li = (line_info_t){};
}

/**
 * Need to shift the buffer contents or use a circular buffer and also able to
 * add to the buffer or pause and return later. the buffer can remain at a fixed
 * size for the duration of reading. might be a tuneable thing.
 *
 * Most efficient is going to be raw array increment. not a complex circular
 * buffer that has overloaded operators. able to return to the filling is ideal.
 * save the location of LineData_t and fill in the holes on re-entrance.
 */
static bool load_LineData(char const *buffer, char const *end_buffer,
		char const **pos_buffer, LineData_t ld[static 1])
{
	ASSERT(buffer);
	ASSERT(end_buffer);
	ASSERT(pos_buffer);

	// invalid to call w/o a span of bytes to process
	ASSERT(end_buffer - buffer);

	char const *c = buffer;

	// the input buffer is maxed to the buffer size read from disk. the maximum
	// length will be more than the acceptable line length.
	while(c != end_buffer && *c != '\n' && *c != '\r')
	{
		c++;
	}

	// update position of output buffer. the next line or next block of data
	// will begin with this position.
	*pos_buffer = c;

	// this will be at most READ_BUFFER_SIZE which is well below the maximum a
	// size_t can hold.
	const size_t real_len = c - buffer;

	// newline found before end of input buffer.
	const bool found_newline = (ld->li.line_len + real_len) && (c != end_buffer);

	// +1 for null terminator
	const size_t request_alloc = ld->li.line_len + real_len + 1;

	// when skipping lines of no interest, at minimum, the "length" of the line
	// data must be updated even if the buffer is nil. it's important to not
	// read the buffer despite the length. this length is a factor in
	// determining whether a newline was found, to increment the line counter.
	ld->li.line_len += real_len;

	// there are lines in excess of 5k characters long. that's obnoxious for a
	// domain entry. the cases observed are for non-domain-level blocking e.g.
	// elements on a web page, scripts, cookies, etc.
	if(request_alloc > MAX_ALLOC_LINE)
	{
		ELOG_STDERR("WARNING: requested line length %lu exceeds acceptable maximum of %lu characters.",
				request_alloc, MAX_ACCEPTABLE_LINE_LENGTH);
		DEBUG_PRINTF("line_len=%u\n", ld->li.line_len);
		const size_t peak_len = 50;
		DEBUG_PRINTF("First %lu characters of ld->buffer:", peak_len);
		fwrite(ld->buffer, 1, MIN(ld->li.line_len, peak_len), stderr);
		DEBUG_PRINTF("\ncurrent buffer first %lu bytes:", peak_len);
		fwrite(buffer, 1, MIN(real_len, peak_len), stderr);
		DEBUG_PRINTF("\n");

		// nuke the entire line.
		ld->pos = ld->buffer;
		*ld->pos = '\0';
		return found_newline;
	}

	if(request_alloc > ld->alloc)
	{
		const size_t offset = ld->pos - ld->buffer;
		CHECK_REALLOC(ld->buffer, sizeof(char) * request_alloc);
		ld->pos = ld->buffer + offset;
		ld->alloc = request_alloc;
	}

	// null terminator is excluded from the "length" of the string. don't
	// copy the extra byte from 'buffer'. it might be out of bounds. only
	// copy the number of bytes read in this iteration. this is appending
	// data.
	memcpy(ld->pos, buffer, real_len);
	// if more to add, start at 'pos'
	ld->pos += real_len;

	// null terminator for easy consumption
	if(found_newline)
	{
		*ld->pos = '\0';
	}

	// true indicates a \n or \r was read
	// false indicates more reading necessary.
	return found_newline;
}

/**
 * This is given an iterator to a list of line numbers; when 'next' returns
 * false, then this will terminate and if necessary break out of loops.
 *
 * When 'nextline' is NOT zero and NOT LINENUMBER_MAX, this will skip lines
 * until it has read ONE line before the line number that is requested.
 *
 * @param pfbc Context for FILE to be read/written.
 *
 * @param nextline Pointer to a storage location holding the next line number to
 * be read. LINENUMBER_MAX indicates read and call 'do_stuff' callback for ALL
 * lines. Zero is the invalid line number and indicates no more lines to be read
 * or as an initial value, do not read any lines. All other values are treated
 * as line numbers in the file and only when the matching line number is read
 * will this call 'do_stuff' callback with the provided context and the line
 * read.
 *
 * @param shared_buffer Optional externally managed buffer that this can utilize
 * as a temporary buffer. If nullptr, allocates and frees a buffer within.
 *
 * @param buffer_size Size of optionally provided buffer; otherwise number of
 * bytes to read from file.
 *
 * @param do_stuff Callback that is expected to increment 'nextline'.
 *
 * @param context Context for callback 'do_stuff'. It is expected to increment
 * 'nextline'.
 *
 * @return The number of lines read unless nextline is initially zero in which
 * case it returns 0.
 */
static void read_pfb_line(pfb_context_t pfbc[static 1],
		void *shared_buffer, size_t buffer_size,
		void(*do_stuff)(PortLineData_t const *const pld, pfb_context_t*, void*),
		void *context)
{
	ASSERT(buffer_size > 0);
	ASSERT(do_stuff);

	// TODO XXX works exclusively with FILE*. if the buffer is exclusively in
	// memory, this will fail.
	FILE *f = pfbc->in_file;
	ASSERT(f);
	// programmer error to not rewind before entering this function.
	assert(ftell(f) == 0 && "tests only");
	linenumber_t f_location = ftell(f);

	char **buffer;
	char *local_buffer;
	const char *pos_buffer, *end_buffer;

	// if shared_buffer is nullptr, i.e., not using shared buffer, ensure a valid
	// pointer is available for a space to reference.
	buffer = &local_buffer;
	// if shared is not-nil, then the input buffer is used. otherwise the value
	// of shared_buffer is nullptr and assigning nullptr to local_buffer is safe.
	*buffer = (char*)shared_buffer;

	if(!*buffer)
	{
		// a buffer for each function call allows this to be called by different
		// threads acting on different files.
		*buffer = malloc(sizeof(char) * buffer_size);
		CHECK_MALLOC(*buffer);
	}

	// one LineData for the entire read operation. this is also tracking the
	// lines read IN this context.
	LineData_t ld;
	init_LineData(&ld);

	do
	{
		const size_t read_count = fread(*buffer, sizeof(char), buffer_size, f);
		if(read_count != 0)
		{
			// reset buffer pointers to new 'end' and new current 'pos'
			end_buffer = *buffer + read_count;
			pos_buffer = *buffer;

			// idea is to read all data from the buffer before reading next
			// chunk. get as many LineData's as are available filled.
			do
			{
				// nextline = 1
				// ld.linenumber = 0
				//
				//	begins here
				//	v					v <-- ends here
				// >1| www.first.domain.com
				//  2| sec.domain.stuff.com
				//  3| 3rd.domain.com
				// (nextline - 1) > 0 = FALSE so .. keep line
				//
				// after reading, linenumber is 1
				//
				//	v
				//  1| www.first.domain.com
				// >2| sec.domain.stuff.com
				//  3| 3rd.domain.com

				bool newline = load_LineData(pos_buffer,
						end_buffer, &pos_buffer, &ld);

				if(newline)
				{
					// save current location into this LineData_t
					ld.li.offset = f_location;
					// where we are now, after consuming newline characters, is
					// the beginning of the next line
					f_location += ld.li.line_len;

					// temporary struct to pass the necessary data to the
					// callback for CSV parsing or another line format.
					PortLineData_t pld;
					pld.data = ld.buffer;
					pld.li = ld.li;

					//printf("call do stuff..\n");
					//printf("ld.li off=%ld\n", ld.li.offset);
					//printf("ld.li len=%u\n", ld.li.line_len);
					ASSERT(pld.li.line_len == ld.li.line_len);
					ASSERT(pld.li.offset == ld.li.offset);

					do_stuff(&pld, pfbc, context);

					reset_LineData(&ld);
				}

				// move past the newline characters
				while(pos_buffer != end_buffer && (*pos_buffer == '\r' || *pos_buffer == '\n'))
				{
					pos_buffer++;
					f_location++;
				}
			} while(pos_buffer != end_buffer);
		}
	} while(feof(f) == 0);

	// the last line. if the file ends w/o a newline, it's still a line.
	if(ld.li.line_len)
	{
		*ld.pos = '\0';
		ld.li.offset = f_location;

		PortLineData_t pld;
		pld.data = ld.buffer;
		pld.li = ld.li;

		// if the last line is over the allocation limit, then ld.buffer will be
		// length 0 i.e. "\0". do_stuff is effectively moot.
		// the parse line data will return false i.e. it's bogus. this won't
		// run.
		do_stuff(&pld, pfbc, context);
	}

	free_LineData(&ld);

	// if using shared, local_buffer is equal to shared and free(nil) is handled
	// externally. if using local_buffer, free() is necessary.
	if(!shared_buffer)
	{
		free(local_buffer);
	}
}

/**
 * Read initial CSV input file. Do not skip ANY lines. All lines are processed.
 */
void pfb_read_one_context(pfb_context_t pfbc[static 1],
		void(*do_stuff)(PortLineData_t const *const pld, pfb_context_t*, void*),
		void *context)
{
	ASSERT(pfbc);

	// reading from a file.

	// TODO determine a sane value for this large in memory buffer. ideally it
	// might depend on system ram and available ram at the moment. and also how
	// many input files are in play.
	const __off_t some_obnoxiously_large_value = 1000 * 1024;

	const __off_t sz = pfbc->file_size;
	ASSERT(pfbc->in_file != nullptr);
	ASSERT(ftell(pfbc->in_file) == 0);
#ifndef NDEBUG
	fseek(pfbc->in_file, 0L, SEEK_END);
	const __off_t test_sz = ftell(pfbc->in_file);
	fseek(pfbc->in_file, 0L, SEEK_SET);
	// if the file is empty, we'll have a problem
	ASSERT(sz > 0);
	ASSERT(test_sz > 0);
	ASSERT(sz == test_sz);

	DEBUG_PRINTF("act size=%ld\nmax size=%ld\n",
			sz, some_obnoxiously_large_value);
#endif

	// TODO XXX what even happens in an empty file scenario? allocating zero
	// bytes is a problem right away.
	if(pfbc->use_mem_buffer && sz > 0 && sz < some_obnoxiously_large_value)
	{
		// this allocation is held until the final file is written. if we're
		// reading multiple files, then multiple large blocks are held in
		// ram until all are closed. the writing has to be done per file too
		// then if the input file is allocated in memory, it writes from
		// memory. if the input is on disk due to excessive size, then
		// writing has to be done the slow way.
		pfbc->mem_buffer = malloc(sizeof(char) * sz);
		if(pfbc->mem_buffer)
		{
			DEBUG_PRINTF("mem buffer mode\n");
			ASSERT(do_stuff);
			read_pfb_line(pfbc, pfbc->mem_buffer, sz, do_stuff, context);
		}
		else
		{
			DEBUG_PRINTF("alloc failed; re-read from disk mode\n");
			ASSERT(do_stuff);
			read_pfb_line(pfbc, nullptr, READ_BUFFER_SIZE, do_stuff, context);
		}
	}
	else
	{
		DEBUG_PRINTF("re-read from disk mode\n");
		ASSERT(do_stuff);
		read_pfb_line(pfbc, nullptr, READ_BUFFER_SIZE, do_stuff, context);
	}
}

#ifdef BUILD_TESTS
static void test_free_LineData()
{
	LineData_t ld, ld_zero;
	memset(&ld, 0, sizeof(LineData_t));
	memset(&ld_zero, 0, sizeof(LineData_t));

	free_LineData(&ld);

	assert(!memcmp(&ld, &ld_zero, sizeof(LineData_t)));
}

static void test_init_LineData()
{
	LineData_t ld;
	init_LineData(&ld);

	assert(ld.li.line_len == 0);
	assert(ld.alloc == 100);
	assert(ld.buffer);
	assert(ld.pos == ld.buffer);
	assert(ld.li.offset == 0);

	free_LineData(&ld);

	assert(ld.li.line_len == 0);
	assert(ld.alloc == 0);
	assert(ld.buffer == nullptr);
	assert(ld.pos == ld.buffer);
	assert(ld.li.offset == 0);
}

/**
 * One newline character found at the beginning of the second buffer.
 */
static void test_load_LineData1()
{
	LineData_t ld;
	const char *buffer = nullptr;
	const char *end_buffer, *pos_buffer;
	bool found_nl = false;

	init_LineData(&ld);

	buffer = "here is the start of a line of input to load line data";
	end_buffer = buffer + 54;

	found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);
	assert(!found_nl);

	assert(ld.alloc == 100);
	assert(ld.li.line_len == 54);
	assert(pos_buffer == end_buffer);

	buffer = "\nblarg glarb flarg klarf";
	end_buffer = buffer + 25;
	pos_buffer = buffer;

	found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);
	assert(pos_buffer == buffer);
	assert(found_nl);
	assert(ld.li.line_len = 54);
	assert(ld.alloc == 100);
	assert(strlen(ld.buffer) == ld.li.line_len);

	const char *expected = "here is the start of a line of input to load line data";
	assert(!memcmp(ld.buffer, expected, 55));

	free_LineData(&ld);
}

/**
 * A line that is exactly 100 characters long and requires an allocation of 1
 * byte for the null terminator.
 */
static void test_load_LineData100()
{
	LineData_t ld;
	const char *buffer = nullptr;
	const char *end_buffer, *pos_buffer;
	bool found_nl = false;

	init_LineData(&ld);

	buffer = "part 1 of 2 strings to form a string that is 100  ";
	end_buffer = buffer + 50;

	found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);
	assert(!found_nl);

	assert(ld.alloc == 100);
	assert(ld.li.line_len == 50);
	assert(pos_buffer == end_buffer);

	buffer = "characters long. at the end of part 2 ze have a nl\n";
	end_buffer = buffer + 51;
	pos_buffer = buffer;

	found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);
	assert(pos_buffer == buffer + 50);
	assert(found_nl);
	assert(ld.li.line_len = 100);
	assert(ld.alloc == 101);
	assert(strlen(ld.buffer) == ld.li.line_len);

	const char *expected = "part 1 of 2 strings to form a string that is 100  "
		"characters long. at the end of part 2 ze have a nl\n";
	assert(!memcmp(ld.buffer, expected, 100));

	free_LineData(&ld);
}

/**
 * A line that is longer than what is considered reasonable and is thus
 * truncated.
 */
static void test_load_LineDataMAX()
{
	LineData_t ld;
	char *buffer = nullptr;
	const char *end_buffer, *pos_buffer;
	bool found_nl = false;

	// "blargblogblargblog...blargblogdeadfood\n"
	const char *blurb = "blorgblog";
	char *w = nullptr;
	buffer = malloc(sizeof(char) * MAX_ACCEPTABLE_LINE_LENGTH + 10);
	CHECK_MALLOC(buffer);
	memset(buffer, '1', MAX_ACCEPTABLE_LINE_LENGTH + 9);
	buffer[MAX_ACCEPTABLE_LINE_LENGTH + 9] = '\0';
	end_buffer = buffer + MAX_ACCEPTABLE_LINE_LENGTH + 10;

	assert(strlen(buffer) == (MAX_ACCEPTABLE_LINE_LENGTH + 9));

	char *end_blurb = buffer + MAX_ACCEPTABLE_LINE_LENGTH;

	for(w = buffer; (w + 9) < end_blurb; w += 9)
	{
		memcpy(w, blurb, 9);
	}
	const char *deadfood = "deadf00d";
	w = end_blurb;
	memcpy(w, deadfood, 8);
	w += 8;
	*w++ = '\n';
	*w = '\0';

	assert(strlen(buffer) == (MAX_ACCEPTABLE_LINE_LENGTH + 9));
	assert(buffer[MAX_ACCEPTABLE_LINE_LENGTH + 9] == '\0');

	init_LineData(&ld);

	found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);
	assert(found_nl);

	assert(ld.alloc == MAX_ALLOC_LINE);
	assert(ld.li.line_len == MAX_ACCEPTABLE_LINE_LENGTH);
	assert(pos_buffer == end_blurb + 8);

	assert(!memcmp(ld.buffer, buffer, MAX_ACCEPTABLE_LINE_LENGTH));

	free(buffer);
	free_LineData(&ld);
}

/**
 * Input is "\n", a single newline character.
 */
static void test_load_LineDataLF()
{
	LineData_t ld;
	char *buffer = "\n";
	const char *end_buffer = buffer + 1;
	const char *pos_buffer = nullptr;

	init_LineData(&ld);

	const bool found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);

	assert(!found_nl);
	assert(ld.alloc == 100);
	assert(ld.li.line_len == 0);
	// pos_buffer does not advance b/c the first character is \n and the load
	// line call does not eat newline characters. the responsibility to eat
	// characters is the caller.
	assert(pos_buffer == buffer);

	free_LineData(&ld);
}

static void test_load_LineDataCR()
{
	LineData_t ld;
	char *buffer = "\r";
	const char *end_buffer = buffer + 1;
	const char *pos_buffer = nullptr;

	init_LineData(&ld);

	const bool found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);

	assert(!found_nl);
	assert(ld.alloc == 100);
	assert(ld.li.line_len == 0);
	// pos_buffer does not advance b/c the first character is \n and the load
	// line call does not eat newline characters. the responsibility to eat
	// characters is the caller.
	assert(pos_buffer == buffer);

	free_LineData(&ld);
}

/**
 * Input is a bunch of \n and \r mixed in
 */
static void test_load_LineDataCRLF()
{
	LineData_t ld;
	const char *buffer = "\r\n\n";
	const char *end_buffer = buffer + 3;
	const char *pos_buffer = nullptr;

	init_LineData(&ld);

	const bool found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);

	assert(!found_nl);
	assert(ld.alloc == 100);
	assert(ld.li.line_len == 0);
	// pos_buffer does not advance b/c the first character is \n and the load
	// line call does not eat newline characters. the responsibility to eat
	// characters is the caller.
	assert(pos_buffer == buffer);

	free_LineData(&ld);
}

/**
 * Test skip functionality.
 */
static void test_load_LineDataSKIP()
{
	LineData_t ld;
	char *buffer = "skip\r";
	const char *end_buffer = buffer + 5;
	const char *pos_buffer = nullptr;

	init_LineData(&ld);

	const bool found_nl = load_LineData(buffer, end_buffer, &pos_buffer, &ld);

	assert(found_nl);
	assert(ld.alloc == 100);
	assert(ld.li.line_len == 4);
	assert(pos_buffer == buffer + 4);

	free_LineData(&ld);
}

static void test_writeline()
{
	PortLineData_t pld;

	pld.data = "something,that,has,many,columns,breaking,pfb_insert,wildly";
	pld.li.offset = 10;
	pld.li.line_len = 58;

	FILE *f = fopen("tests/unit_pfb_prune/FileInput_1.work", "wb");
	// TODO what was this doing?
	//writeline(f, &pld);
	fclose(f);

	char *buffer = malloc(sizeof(char) * 100);
	CHECK_MALLOC(buffer);

	FILE *f_in = fopen("tests/unit_pfb_prune/FileInput_1.work", "rb");
	size_t read = fread(buffer, sizeof(char), 100, f_in);
	fclose(f_in);
	assert(read == pld.li.line_len + 1); // for the newline
	assert(!memcmp(pld.data, buffer, pld.li.line_len));
	assert(buffer[pld.li.line_len] == '\n');

	free(buffer);
}

void test_rw_pfb_csv()
{
	test_free_LineData();
	test_init_LineData();
	test_load_LineData1();
	test_load_LineData100();
	test_load_LineDataMAX();
	test_load_LineDataLF();
	test_load_LineDataCR();
	test_load_LineDataCRLF();
	test_load_LineDataSKIP();

	test_writeline();
}
#endif
