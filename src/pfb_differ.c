/**
 * pfb_differ.c
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
#include "adbplusline.h"
#include "domain.h"
#include <string.h>
#include <stdlib.h>

enum diff_codes : char
{
	WINNER='+',
	LOSER='-',
	NEUTRAL=' ',
};

// might also act as indexes into an array holding markers for what symbol(s) to
// write to output file.
enum dv_comparison : char
{
	dv_A_eq_dv_B = 0,
	dv_A_blk_dv_B = 1,
	dv_B_blk_dv_A = 2,
	dv_A_isblk_dv_B = 3,
	dv_B_isblk_dv_A = 4,
	dv_A_exclusive = 5,
	dv_B_exclusive = 6,
	dv_A_lt_dv_B_write_A = 7,
	dv_A_gt_dv_B_write_B = 8,
};

typedef struct DV_FILE_iter
{
	// source of the next line: read from FILE specified.
	pfb_context_t *in_context;
	// destination for the output
	pfb_out_context_t *out_context;
	// a transitory holder for a single entry; used for diff'ing
	DomainView_t dv;
	// for the current entry, whether this entry has won against another; reset
	// after advancing to the next entry. when non-zero during the write, skips
	// writing this entry.
	bool written;
	// holds 'a' or 'b' for which DomainView this iter represents
	const char marker;

	// array of offsets and line lengths for entries contained within in_context
	const size_len_t li_used;
	const line_info_t *const li;
	// current index into the line_info_t 'li' array
	size_len_t cur_li_idx;

	// temporary allocated buffer to hold a line from FILE
	char *buffer;
	// length of allocated buffer
	size_len_t len_alloced;
} DV_FILE_iter_t;

typedef struct DV_BUFFER_iter
{
	pfb_out_context_t *out_context;
	DomainView_t dv;
	// when non-zero during the write, skips writing this entry n times.
	bool written;
	// holds 'a' or 'b' for which DomainView this iter represents
	const char marker;

	// than having another pointer to dereference?
	//const LiteLineData_t lld;

	// from LiteLineData: need to know # used and the array itself
	const size_len_t li_used;
	const line_info_t *const li;
	// current index into the line_info_t 'li' array
	size_len_t cur_li_idx;

	// pointer into a buffer allocated on a pfb_context_t to the start of the
	// line holding a domain to compare against.
	// this starts at the beginning of the buffer in pfb_out_buffer_t.
	// increment it by the line length to reach the next chunk/line.
	const char *const buffer;
} DV_BUFFER_iter_t;

static void realloc_buffer(char *buffer[static 1], size_len_t cur_len[static 1],
		size_len_t new_len)
{
	ASSERT(buffer);
	ASSERT(cur_len);
	ASSERT(new_len > 0);

	if(*cur_len >= new_len)
		return;

	CHECK_REALLOC(*buffer, new_len);

	*cur_len = new_len;
}

/**
 * Read the line specified by li into the given buffer from the specified
 * context 'pcc'.
 *
 * buffer must have enough space allocated to hold the line + 1 for null
 * terminator.
 */
static void read_liteline_FILE(pfb_context_t pcc[static 1],
		char buffer[static 1], line_info_t li)
{
	ASSERT(pcc);
	ASSERT(pcc->in_file);
	ASSERT(buffer);

	fseek(pcc->in_file, li.offset, SEEK_SET);
	const size_t read_count = fread(buffer, sizeof(char),
			li.line_len, pcc->in_file);
	buffer[read_count] = '\0';
#ifndef NDEBUG
	if(read_count != li.line_len)
	{
		DEBUG_PRINTF("WARN UNEXPECTED\n");
		DEBUG_PRINTF("read count=%lu\n", read_count);
		DEBUG_PRINTF("offset=%lu\n", li.offset);
		DEBUG_PRINTF("line len=%u\n", li.line_len);
		DEBUG_PRINTF("read=%s\n", buffer);
		DEBUG_PRINTF("buffer: %s\n", buffer);
	}
#endif
	ASSERT(read_count == li.line_len);
}

/**
 * How to identify A is not at all like B? e.g.,
 * ads.google.com
 * ads.yahoo.com
 *
 * another layer to consider is match strength. if raw domains are supported,
 * the comparison here becomes a level more complicated as a full match is moot
 * until a strong match comes along.
 */
static int compare_dv(DomainView_t dv_A, DomainView_t dv_B)
{
	// probably points to an array of the full set of domainviews that
	// themselves point to entries in the buffer which is held by the context.
	// the first two segments must match for the remainder comparison to make
	// any sense. as all FQD must have at least two components.

	// have to check the segments for equality
	// could compute a hash, store it, then comare against a hash of the other.

	int last_cmp = -1;
	ASSERT(dv_A.fqd.len > 0);
	ASSERT(dv_B.fqd.len > 0);
	ASSERT(dv_A.fqd.data);
	ASSERT(dv_B.fqd.data);
	ASSERT(dv_A.segs_used > 0);
	ASSERT(dv_B.segs_used > 0);
	//printf("A segs used=%u\n", dv_A.segs_used);
	//printf("B segs used=%u\n", dv_B.segs_used);
	for(uint i = 0, j = 0; i < dv_A.segs_used && j < dv_B.segs_used;
			i++, j++)
	{
		int ret = memcmp(
				dv_A.fqd.data + dv_A.label_indexes[i],
				dv_B.fqd.data + dv_B.label_indexes[j],
				MIN(dv_A.lengths[i], dv_B.lengths[j]));

		if(ret == 0)
		{
			ret = dv_A.lengths[i] - dv_B.lengths[j];
		}

#if 0
		printf("A the segment under inspection: >");
		fwrite(dv_A.fqd.data + dv_A.label_indexes[i], 1, dv_A.lengths[i], stdout);
		printf("<\n");

		printf("VS\nB the segment under inspection: >");
		fwrite(dv_B.fqd.data + dv_B.label_indexes[i], 1, dv_B.lengths[i], stdout);
		printf("<\n\n");
#endif

		if(ret == 0)
		{
			// ads.ru.com
			//     ru.com
			//
			// ads.google.com
			//     google.com
			//
			//   a.google.com - A* wins! write
			// b.a.google.com - B obliterated; write the loss. advance B
			//
			//   a.google.com - A* wins x2!
			// c.a.google.com - B obliterated; write the loss. advance B
			//
			// STEP(4)
			// ads.zoomed.com.ru - A
			//     zoomed.com.ru - B* write
			//
			// ads.google.com - A - write; advance
			// ads.google.com - B - advance
			//long long ret_seg = dv_A.segs_used - dv_B.segs_used;
			if(dv_A.segs_used == dv_B.segs_used)
			{
				last_cmp = dv_A_eq_dv_B;
			}
			else if(dv_A.segs_used < dv_B.segs_used)
			{
				last_cmp = dv_A_blk_dv_B;
			}
			else
			{
				last_cmp = dv_B_blk_dv_A;
			}
		}
		else if(ret < 0)
		{
			// STEP-B(1)
			// ads.google.com - A (wins) write; advance A...
			// ads.google.ru  - B
			//
			// STEP-B(2)
			// ads.google.org - A (wins) write; advance A...
			// ads.google.ru  - B
			//
			// STEP-A(1)
			// a.google.com - A (wins) write; advance A...
			// c.google.com - B*
			//
			// STEP-A(2)
			// b.google.com - A (wins) write; advance A...
			// c.google.com - B*
			//
			// ads.a.google.com
			//     b.google.com
			//
			// STEP(1)
			// ads.google.com.ru - A write
			//     yahood.com.ru - B*
			//
			// STEP(2)
			// ad1.google.com.ru - A write
			//     yahood.com.ru - B*
			last_cmp = dv_A_lt_dv_B_write_A;
			break;
		}
		else // ret > 0
		{
			// STEP-B(3)
			// ads.google.zu  - A (lost)
			// ads.google.ru  - B (wins) write; advance B...
			//
			// STEP-A(3)
			// d.google.com - A*
			// c.google.com - B write
			//
			// b.google.com
			// a.google.com
			//
			// STEP(3)
			// ads.zoomed.com.ru - A*
			//     yahood.com.ru - B write
			last_cmp = dv_A_gt_dv_B_write_B;
			break;
		}
	}

	ASSERT(last_cmp != -1);
	return last_cmp;
}


/**
 * apply appropriate, necessary action to the iter. write to disk, advance to
 * the next line.
 *
 * this requires an iterator that is agnostic to the source.
 */
static void action_dv(int cmp,
		void (*write_dv_line)(void*, char code),
		bool (*advance_dv_iter)(void*),
		void *dviter_A, void *dviter_B)
{
	if(cmp == dv_A_eq_dv_B)
	{
		// write A (B is identical)
		// advance A
		// advance B
		write_dv_line(dviter_A, NEUTRAL);
		advance_dv_iter(dviter_A);
		advance_dv_iter(dviter_B);
	}
	else if(cmp == dv_A_lt_dv_B_write_A)
	{
		// write A
		// advance A
		write_dv_line(dviter_A, WINNER);
		advance_dv_iter(dviter_A);
		// hold B - it might win later.
	}
	else if(cmp == dv_A_gt_dv_B_write_B)
	{
		// write B
		// advance B
		write_dv_line(dviter_B, WINNER);
		advance_dv_iter(dviter_B);
		// hold A - it might win later.
	}
	else if(cmp == dv_A_blk_dv_B)
	{
		// write A; keep its location; it might win again.
		// write B as a loss
		// advance B
		write_dv_line(dviter_A, WINNER);
		write_dv_line(dviter_B, LOSER);
		advance_dv_iter(dviter_B);
	}
	else if(cmp == dv_B_blk_dv_A)
	{
		// write B; keep its location; it might win again.
		// write A as a loss
		// advance A
		write_dv_line(dviter_B, WINNER);
		write_dv_line(dviter_A, LOSER);
		advance_dv_iter(dviter_A);
	}
	else
	{
		ASSERT(false && "cmp is out of range");
	}
}


static void core_write_DV(FILE *out_file, const char buffer[static 1],
		size_len_t line_len, char code, char marker)
{
	// options to write only the new entries added by 'a', or mark only those
	// added by 'a' as plus, mark all removals from 'a' and 'b', etc.
	if(code == NEUTRAL)
	{
		ASSERT(code == ' ');
		fprintf(out_file, "  ");
	}
	else if(code == WINNER)
	{
		if(marker == 'b')
		{
			fprintf(out_file, " %c", marker);
		}
		else if(marker == 'a')
		{
			fprintf(out_file, "%c%c", code, marker);
		}
	}
	else
	{
		fprintf(out_file, "%c%c", code, marker);
	}
	fwrite(buffer, sizeof(char), line_len, out_file);
	fprintf(out_file, "\n");
}

static void write_DV_FILE_iter(void *iter_in, char code)
{
	DV_FILE_iter_t *iter = iter_in;
	ASSERT(iter);
	ASSERT(iter->cur_li_idx < iter->li_used);
	if(iter->written == false)
	{
		core_write_DV(iter->out_context->out_file, iter->buffer,
				iter->li[iter->cur_li_idx].line_len, code, iter->marker);
		iter->written = true;
	}
}

static void write_DV_BUFFER_iter(void *iter_in, char code)
{
	DV_BUFFER_iter_t *iter = iter_in;
	ASSERT(iter);
	ASSERT(iter->cur_li_idx < iter->li_used);
	if(iter->written == false)
	{
		// although this *could* change iter->buffer if it were non-const and
		// rely on the 'line_len', it will have to compensate for n number of
		// newline characters that come between. theoretically one newline.
		core_write_DV(iter->out_context->out_file, iter->buffer +
				iter->li[iter->cur_li_idx].offset,
				iter->li[iter->cur_li_idx].line_len,
				code, iter->marker);
		iter->written = true;
	}
}


/**
 * Process clean lines. Expectations:
 *
 * null terminated, MATCH_FULL
 */
static bool process_one_line(DomainView_t dv[static 1], const char *const str)
{
	// requires a null terminated string.
	AdbplusView_t lv;

	const bool parsed_ok = parse_adbplus_line(&lv, str);
	ASSERT(parsed_ok);
	UNUSED(parsed_ok);

	ASSERT(lv.ms == MATCH_FULL);

	const bool update_dv_ok = update_DomainView(dv, lv.data, lv.len);
	ASSERT(update_dv_ok);
	UNUSED(update_dv_ok);

	return true;
}

static bool advance_DV_FILE_iter(void *file_iter)
{
	DV_FILE_iter_t *iter = file_iter;
	ASSERT(iter);
	iter->written = false;

	ASSERT(iter->li_used > 0);
	iter->cur_li_idx++;

#if 0
	DEBUG_PRINTF("li used=%u\n", iter->li_used);
	DEBUG_PRINTF("cur li=%u\n", iter->cur_li_idx);
#endif
	if(iter->cur_li_idx >= iter->li_used)
	{
		// reached end of this context's list of entries
		return false;
	}

	ASSERT(iter->cur_li_idx < iter->li_used);

#if 0
	DEBUG_PRINTF("read cur li=%u\n", iter->cur_li_idx);
	DEBUG_PRINTF("line len=%u\n", iter->li[iter->cur_li_idx].line_len);
#endif
	realloc_buffer(&iter->buffer, &iter->len_alloced,
			iter->li[iter->cur_li_idx].line_len + 1);

	if(iter->len_alloced == 0)
		return false;

#if 0
	if(iter->li[iter->cur_li_idx].line_len > 90)
	{
		printf("highly suspicious line length\n");
		printf("line len is > 90: %u\n", iter->li[iter->cur_li_idx].line_len);
		printf("cur li idx=%u\n", iter->cur_li_idx);
		printf("total lines used %u\n", iter->li_used);
	}
	ASSERT(iter->li[iter->cur_li_idx].line_len < 90);
#endif
	read_liteline_FILE(iter->in_context, iter->buffer, iter->li[iter->cur_li_idx]);

	return process_one_line(&iter->dv, iter->buffer);
}

static bool advance_DV_BUFFER_iter(void *iter_in)
{
	DV_BUFFER_iter_t *iter = iter_in;
	ASSERT(iter);

	// reset writes counter to avoid writing the same entry more than once
	iter->written = false;

	ASSERT(iter->li_used > 0);
	iter->cur_li_idx++;

	if(iter->cur_li_idx >= iter->li_used)
	{
		// reached end of this context's list of entries
		return false;
	}

	ASSERT(iter->cur_li_idx < iter->li_used);

	// must move past the \0 that replaced the \n
	const char *const ptr = iter->buffer + iter->li[iter->cur_li_idx].offset;
#if 0
	printf("\n\n----advance buffer iter---- MARKER: %c\n", iter->marker);
	printf("idx=%u offset=%lu len=%u we're going to process: >",
			iter->cur_li_idx,
			iter->li[iter->cur_li_idx].offset,
			iter->li[iter->cur_li_idx].line_len);
	fwrite(ptr, 1, iter->li[iter->cur_li_idx].line_len, stdout);
	printf("<\n");
#endif

	bool ret = process_one_line(&iter->dv, ptr);

	return ret;
}

/**
 * Initialize the components on the DV iterator. After calling this, the iter's
 * DomainView_t is ready for comparison against another.
 */
static bool begin_DV_BUFFER_iter(DV_BUFFER_iter_t iter[static 1])
{
	ASSERT(iter);
	ASSERT(iter->cur_li_idx == 0);
	ASSERT(iter->buffer != nullptr);
	ASSERT(iter->written == false);
	ASSERT(iter->li);
	// what otherwise is the point in exercising a diff?
	ASSERT(iter->li_used > 0);

	init_DomainView(&iter->dv);

	ASSERT(iter->li[iter->cur_li_idx].offset == 0);

	// the in-memory buffer in this case is null terminated strings concat'ed in
	// one single stream. in lieu of \n, there are \0.
	ASSERT(iter->li[iter->cur_li_idx].line_len > 0);

	return true;
}

/**
 * Initialize the components on the DV iterator. After calling this, the iter's
 * DomainView_t is ready for comparison against another.
 */
static bool begin_DV_FILE_iter(DV_FILE_iter_t iter[static 1])
{
	ASSERT(iter);
	ASSERT(iter->cur_li_idx == 0);
	ASSERT(iter->buffer == nullptr);
	ASSERT(iter->len_alloced == 0);
	ASSERT(iter->written == false);
	ASSERT(iter->li);
	// what otherwise is the point in exercising a diff?
	ASSERT(iter->li_used > 0);

	init_DomainView(&iter->dv);

	ASSERT(iter->li[iter->cur_li_idx].line_len > 0);
	realloc_buffer(&iter->buffer, &iter->len_alloced,
			iter->li[iter->cur_li_idx].line_len + 1);

	ASSERT(iter->len_alloced > 0);
	ASSERT((size_len_t)iter->li[iter->cur_li_idx].line_len + 1 == iter->len_alloced);
	if(iter->len_alloced == 0)
		return false;

	read_liteline_FILE(iter->in_context, iter->buffer, iter->li[iter->cur_li_idx]);

	return true;
}

static void free_DV_FILE_iter(DV_FILE_iter_t iter[static 1])
{
	free_DomainView(&iter->dv);

	free(iter->buffer);
	iter->buffer = nullptr;
	iter->len_alloced = 0;

	iter->out_context = nullptr;
	iter->cur_li_idx = 0;
	iter->written = false;
}

static void free_DV_BUFFER_iter(DV_BUFFER_iter_t iter[static 1])
{
	free_DomainView(&iter->dv);

	// everything else is zeroing
	iter->out_context = nullptr;
	// we don't want this to change; the offsets in li are based on the origin
	// to the incorrect pointer.
	//iter->buffer = nullptr;
	iter->cur_li_idx = 0;
	iter->written = false;
}


void diff_adbplus_adlists_FILE(pfb_context_t pcc_A[static 1],
		const LiteLineData_t litelines_A[static 1],
		pfb_context_t pcc_B[static 1], const LiteLineData_t litelines_B[static 1],
		pfb_out_context_t out_context[static 1])
{
	ASSERT(pcc_A);
	ASSERT(pcc_B);
	ASSERT(pcc_A->in_file);
	ASSERT(pcc_B->in_file);
	ASSERT(pcc_A->in_fname == nullptr);
	ASSERT(pcc_B->in_fname == nullptr);

	// the final output containing the diff
	ASSERT(out_context);
	// stdout is a valid output option; out_fname will be nullptr
	ASSERT(out_context->out_file);

	ASSERT(litelines_A->li != nullptr);
	ASSERT(litelines_B->li != nullptr);
	// if one of them is empty, the output is effectively the +/- of the other
	ASSERT(litelines_A->used > 0);
	ASSERT(litelines_B->used > 0);

	DV_FILE_iter_t dv_iterA = {
		.in_context = pcc_A,
		.out_context = out_context,
		.dv = {},
		.written = false,
		.marker = 'a',
		.li_used = litelines_A->used,
		.li = litelines_A->li,
		.cur_li_idx = 0,
		.buffer = nullptr,
		.len_alloced = 0,
	};
	if(!begin_DV_FILE_iter(&dv_iterA))
	{
		// fatal allocation error
		return;
	}
	process_one_line(&dv_iterA.dv, dv_iterA.buffer);

	DV_FILE_iter_t dv_iterB = {
		.in_context = pcc_B,
		.out_context = out_context,
		.dv = {},
		.written = false,
		.marker = 'b',
		.li_used = litelines_B->used,
		.li = litelines_B->li,
		.cur_li_idx = 0,
		.buffer = nullptr,
		.len_alloced = 0,
	};
	if(!begin_DV_FILE_iter(&dv_iterB))
	{
		// fatal allocation error
		return;
	}
	process_one_line(&dv_iterB.dv, dv_iterB.buffer);

#if 0
	DEBUG_PRINTF("iterA used %u\n", dv_iterA.li_used);
	DEBUG_PRINTF("lite used %u\n", litelines_A->used);
	DEBUG_PRINTF("lite allo %u\n", litelines_A->alloc);
	DEBUG_PRINTF("iterB used %u\n", dv_iterB.li_used);
	DEBUG_PRINTF("lite used %u\n", litelines_B->used);
	DEBUG_PRINTF("lite allo %u\n", litelines_B->alloc);
	for(size_t i = 0; i < dv_iterA.li_used; i++)
	{
		if(dv_iterB.li[i].line_len > 90)
		{
			printf("suspicious line length!\n");
			printf("iter B i=%lu len=%u\n", i, dv_iterB.li[i].line_len);
		}
		ASSERT(dv_iterA.li[i].line_len < 90);
	}
	for(size_t i = 0; i < dv_iterB.li_used; i++)
	{
		if(dv_iterB.li[i].line_len > 90)
		{
			printf("suspicious line length!\n");
			printf("iter B i=%lu len=%u\n", i, dv_iterB.li[i].line_len);
		}
		ASSERT(dv_iterB.li[i].line_len < 90);
	}
#endif

	do
	{
		int cmp = compare_dv(dv_iterA.dv, dv_iterB.dv);
		action_dv(cmp, write_DV_FILE_iter, advance_DV_FILE_iter, &dv_iterA, &dv_iterB);
	} while(dv_iterA.cur_li_idx < dv_iterA.li_used &&
			dv_iterB.cur_li_idx < dv_iterB.li_used);

#if 0
	DEBUG_PRINTF("after loop over A and B\n");
	DEBUG_PRINTF("A idx=%u\n", dv_iterA.cur_li_idx);
	DEBUG_PRINTF("A used=%u\n", dv_iterA.li_used);
	DEBUG_PRINTF("B idx=%u\n", dv_iterB.cur_li_idx);
	DEBUG_PRINTF("B used=%u\n", dv_iterB.li_used);
#endif

	// write the remainder of the two iterators. whatever remains is already
	// sorted and is ADDED if it is in B and REMOVED if it is in A: the diff is
	// from A to B.
	while(dv_iterA.cur_li_idx < dv_iterA.li_used)
	{
		write_DV_FILE_iter(&dv_iterA, WINNER);
		advance_DV_FILE_iter(&dv_iterA);
	}
	while(dv_iterB.cur_li_idx < dv_iterB.li_used)
	{
		write_DV_FILE_iter(&dv_iterB, WINNER);
		advance_DV_FILE_iter(&dv_iterB);
	}

	free_DV_FILE_iter(&dv_iterA);
	free_DV_FILE_iter(&dv_iterB);
}


/**
 * AdbplusView_t requires null terminated strings.
 *
 * The input buffer here is literally the entire file - with \n for line
 * separation.
 *
 * it is writable ... it could abuse things by overwriting the \n with \0 and
 * either replace the \0 with \n or move along. we have all offsets already.
 *
 * possibly the construction of the buffer adds \0 at strategic locations since
 * the input is no longer a meaningful file to write to disk.
 */
void diff_adbplus_adlists_BUFFER(pfb_out_buffer_t pcc_A[static 1],
		pfb_out_buffer_t pcc_B[static 1], pfb_out_context_t out_context[static 1])
{
	ASSERT(pcc_A);
	ASSERT(pcc_B);
	ASSERT(pcc_A->buffer);
	ASSERT(pcc_B->buffer);

	// the final output containing the diff
	ASSERT(out_context);
	// the default is to stdout; out_fname will be nullptr
	ASSERT(out_context->out_file);

	ASSERT(pcc_A->litelines.li != nullptr);
	ASSERT(pcc_B->litelines.li != nullptr);
	// if one of them is empty, the output is effectively the +/- of the other
	ASSERT(pcc_A->litelines.used > 0);
	ASSERT(pcc_B->litelines.used > 0);
	ASSERT(pcc_A->litelines.alloc > 0);
	ASSERT(pcc_B->litelines.alloc > 0);

	DV_BUFFER_iter_t dv_iterA = {
		.out_context = out_context,
		.dv = {},
		.written = false,
		.marker = 'a',
		.li_used = pcc_A->litelines.used,
		.li = pcc_A->litelines.li,
		.cur_li_idx = 0,
		.buffer = pcc_A->buffer,
	};
	if(!begin_DV_BUFFER_iter(&dv_iterA))
	{
		// fatal allocation error
		return;
	}

	ASSERT(dv_iterA.cur_li_idx == 0);
#if 0
	DEBUG_PRINTF("\nITER A primes offset=%lu len=%u we're going to process: >", dv_iterA.li[dv_iterA.cur_li_idx].offset,
			dv_iterA.li[dv_iterA.cur_li_idx].line_len);
	fwrite(dv_iterA.buffer, 1, dv_iterA.li[dv_iterA.cur_li_idx].line_len, stderr);
	DEBUG_PRINTF("<\n\n");
#endif

	process_one_line(&dv_iterA.dv, dv_iterA.buffer);

	DV_BUFFER_iter_t dv_iterB = {
		.out_context = out_context,
		.dv = {},
		.written = false,
		.marker = 'b',
		.li_used = pcc_B->litelines.used,
		.li = pcc_B->litelines.li,
		.cur_li_idx = 0,
		.buffer = pcc_B->buffer,
	};
	if(!begin_DV_BUFFER_iter(&dv_iterB))
	{
		// fatal allocation error
		exit(EXIT_FAILURE);
	}

#if 0
	ASSERT(dv_iterB.cur_li_idx == 0);
	DEBUG_PRINTF("\nITER B primes offset=%lu len=%u we're going to process: >", dv_iterB.li[dv_iterB.cur_li_idx].offset,
			dv_iterB.li[dv_iterB.cur_li_idx].line_len);
	fwrite(dv_iterB.buffer, 1, dv_iterB.li[dv_iterB.cur_li_idx].line_len, stderr);
	DEBUG_PRINTF("<\n\n");
#endif

	process_one_line(&dv_iterB.dv, dv_iterB.buffer);

	double action_dv_collector = 0.0;
	UNUSED(action_dv_collector);
	do
	{
		int cmp = compare_dv(dv_iterA.dv, dv_iterB.dv);
		action_dv(cmp, write_DV_BUFFER_iter, advance_DV_BUFFER_iter, &dv_iterA, &dv_iterB);
	} while(dv_iterA.cur_li_idx < dv_iterA.li_used &&
			dv_iterB.cur_li_idx < dv_iterB.li_used);

	// write the remainder of the two iterators. whatever remains is already
	// sorted and is ADDED if it is in B and REMOVED if it is in A: the diff is
	// from A to B.
	while(dv_iterA.cur_li_idx < dv_iterA.li_used)
	{
		write_DV_BUFFER_iter(&dv_iterA, '+');
		advance_DV_BUFFER_iter(&dv_iterA);
	}
	while(dv_iterB.cur_li_idx < dv_iterB.li_used)
	{
		write_DV_BUFFER_iter(&dv_iterB, '+');
		advance_DV_BUFFER_iter(&dv_iterB);
	}

	free_DV_BUFFER_iter(&dv_iterA);
	free_DV_BUFFER_iter(&dv_iterB);
}
