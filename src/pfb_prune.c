/**
 * pfb_prune.c
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
#include "domaintree.h"
#include "domaininfo.h"
#include "pfb_context.h"
#include "domain.h"
#include "adbplusline.h"
#include "rw_pfb_csv.h"
#include "contextpair.h"
#include "pfb_prune.h"
#include "matchstrength.h"
#include "paths_list.h"
#include <limits.h>
#include "logdiagnostics.h"
#include <time.h>

const char LINE_TERMINAL = '\0';

char* pfb_strdup(const char *in)
{
	ASSERT(in);
	if(!in)
	{
		ELOG_STDERR("Input string must be non-nil\n");
		return nullptr;
	}

	const size_t in_size = strlen(in);
	ASSERT(in_size > 0);

	if(!in_size)
	{
		ELOG_STDERR("Input string must be non-empty\n");
		return nullptr;
	}

	char *out_str = malloc(sizeof(char) * in_size + 1);
	CHECK_MALLOC(out_str);
	memcpy(out_str, in, in_size);
	out_str[in_size] = '\0';

	return out_str;
}

char* outputfilename(const char *input, const char *ext)
{
	if(!input || !ext)
	{
		ELOG_STDERR("Input filename and extension must be non-nil\n");
		return nullptr;
	}

	if(!*input || !*ext)
	{
		ELOG_STDERR("Input filename and extension must be non-empty\n");
		return nullptr;
	}

	const char *walker, *period = nullptr;
	for(walker = input; *walker; walker++)
	{
		if(*walker == '.')
		{
			period = walker;
		}
	}

	char *output = nullptr;

	if(!period)
	{
		// walker will be at the nil terminator
		period = walker;
	}

	const size_t len = period - input + strlen(ext);
	ASSERT(len > 0);
	output = malloc(sizeof(char) * len + 1);
	CHECK_MALLOC(output);
	memcpy(output, input, period - input);
	memcpy(output + (period - input), ext, strlen(ext));
	*(output + len) = '\0'; // line 83

	return output;
}

static void pfb_insert(PortLineData_t const pld[static 1],
		pfb_context_t pfbc[static 1], void *data)
{
	ASSERT(data);
	ASSERT(pfbc->in_file);

	ContextPair_t *pc = data;

	DomainView_t *dv = pc->dv;
	const TLD_implementation_t *tld_impl = pc->tld_impl;

	ASSERT(dv);

	// pld has total line length
	// lv has the length of the domain alone to split it into components.
	// lv holds a pointer to the beginning of the domain i.e. past the markers
	// and a length that will go one past the end of the domain i.e. to the end
	// marker if it exists. i.e. number of characters in the FQD.
	AdbplusView_t lv;
	bool valid = parse_adbplus_line(&lv, pld->data);
	if(!valid)
	{
		return;
	}

	const MatchStrength_t ms = lv.ms;
	if(ms == MATCH_COMMENT || ms == MATCH_HEADER)
	{
		// add the line information to list for direct carry over to the final
		// list.
		insert_carry_over(&pfbc->co, pld->li);
	}
	else
	{
		ASSERT(ms == MATCH_FULL);
		// domain view remains the same for adbplus! :-)
		ASSERT(!null_DomainView(dv));
		// the len here is for the FQD *only* e.g. 'ads.google.com'.
		// the DomainInfo requires the line length to be used in the
		// consolidate. this is a significant change!
		if(!update_DomainView(dv, lv.data, lv.len))
		{
			ELOG_STDERR("ERROR: failed to update DomainView; possibly garbage input. insert skipped.\n");
			ASSERT(false);
		}
		else
		{
			// DomainView is valid only during an insert.
			dv->match_strength = ms;
			dv->context = pfbc;
			dv->li = pld->li;

			insert_DomainTree(*tld_impl, dv);
		}
	}
}

static size_t pfb_out_context_write_FILE(const char *buffer, size_len_t count,
		pfb_out_context_t c[static 1])
{
	ASSERT(buffer);
	ASSERT(count > 0);
	ASSERT(c);
	ASSERT(c->out_file);
	const size_t wrote = fwrite(buffer, sizeof(char), count, c->out_file);
	ASSERT(wrote == count);
	fwrite("\n", sizeof(char), 1, c->out_file);
	return wrote;
}

/**
 * Initialize the final output context with the specified output filename. When
 * the output filename is nullptr, output is written to stdout.
 */
pfb_out_context_t pfb_init_out_context(const char *out_fname)
{
	pfb_out_context_t ret = {
		.writer_cb = pfb_out_context_write_FILE
	};

	if(out_fname != nullptr)
	{
		ret.out_fname = pfb_strdup(out_fname);
		DEBUG_PRINTF("out fname: %s\n", ret.out_fname);
	}
	else
	{
		ret.out_file = stdout;
		DEBUG_PRINTF("output to stdout\n");
	}

	return ret;
}

static void pfb_init_in_contexts(paths_list_t in_paths_list,
		pfb_context_collect_t pcc[static 1])
{
	ASSERT(pcc);

	// using calloc for the included 'memset' to zero the DomainView instance.
	pcc->in_contexts.begin_context = calloc(in_paths_list.len, sizeof(pfb_context_t));
	CHECK_MALLOC(pcc->in_contexts.begin_context);
	pcc->in_contexts.end_context = pcc->in_contexts.begin_context + in_paths_list.len;

	uint i = 0;
	for(pfb_context_t *c = pcc->in_contexts.begin_context;
			c != pcc->in_contexts.end_context; c++, i++)
	{
		ASSERT(!c->in_file);
		ASSERT(in_paths_list.paths[i].path);
		ASSERT(strlen(in_paths_list.paths[i].path) > 0);
		c->use_mem_buffer = in_paths_list.paths[i].use_mem_buffer;
		c->in_fname = pfb_strdup(in_paths_list.paths[i].path);
		c->file_size = in_paths_list.paths[i].pfb_s.file_size;
	}
}

void realloc_litelines(LiteLineData_t litelines[static 1])
{
	if(litelines->used == litelines->alloc)
	{
		const size_len_t growth = 10 + litelines->alloc * 0.5;
		ASSERT(growth > 0);
		CHECK_REALLOC(litelines->li, sizeof(line_info_t) *
				(litelines->alloc + growth));
		litelines->alloc += growth;
#ifndef NDEBUG
		litelines->re_alloc_counter++;
#endif
	}
}

/**
 * This might also create a PortLineData_t for each line. The diff code then
 * only has to iterate the array of these lines ignoring those lines that are
 * irrelevant for diffing. this context may also have a means to skip writing
 * lines that are not adblock plus syntax - will need to either parse the line
 * buffer or pass meta data to here.
 *
 * the carry over might be disabled earlier. i.e. this does exactly what it is
 * told to: write a line of data. or also collect line offsets.
 *
 * @param buffer input data to write out to the given out context
 * @param count number of bytes in buffer to write
 */
static size_t pfb_out_context_write_BUFFER(const char *buffer,
		const size_len_t count, pfb_out_context_t c[static 1])
{
	ASSERT(buffer);
	ASSERT(count > 0);
	ASSERT(c);
	// note: this is the same location as FILE*.
	ASSERT(c->out_buffer);
	ASSERT(c->out_buffer->alloc_len > 0);
	ASSERT(c->out_buffer->next_idx < c->out_buffer->alloc_len);

	// +1 for null terminator
	if(count + 1 < c->out_buffer->alloc_len - c->out_buffer->next_idx)
	{
	}
	else
	{
		const size_t growth = c->out_buffer->alloc_len * 0.5;
		CHECK_REALLOC(c->out_buffer->buffer, sizeof(char) *
				(c->out_buffer->alloc_len + growth));
		ASSERT(c->out_buffer);
		c->out_buffer->alloc_len += growth;
		ASSERT(count + 1 < c->out_buffer->alloc_len - c->out_buffer->next_idx);
	}

	const linenumber_t offset = c->out_buffer->next_idx;
	char *ptr = c->out_buffer->buffer + c->out_buffer->next_idx;
	memcpy(ptr, buffer, count);
	c->out_buffer->next_idx += count;

	// this never goes to disk. it is null terminated for the AdbplusView
	// parsing which requires null terminated strings.
	c->out_buffer->buffer[c->out_buffer->next_idx] = LINE_TERMINAL;

	// account for null terminator
	c->out_buffer->next_idx++;

	realloc_litelines(&c->out_buffer->litelines);
	ASSERT(c->out_buffer->litelines.li);
	ASSERT(c->out_buffer->litelines.alloc > 0);
	ASSERT(c->out_buffer->litelines.used < c->out_buffer->litelines.alloc);
	c->out_buffer->litelines.li[c->out_buffer->litelines.used].line_len = count;
	c->out_buffer->litelines.li[c->out_buffer->litelines.used].offset = offset;
	c->out_buffer->litelines.used++;

	return count;
}

pfb_context_collect_t pfb_init_contexts_FILE(paths_list_t in_paths_list,
		FILE out_file[static 1])
{
	ASSERT(in_paths_list.len > 0);
	ASSERT(out_file);

	pfb_context_collect_t pcc = {
		.out_context.out_fname = nullptr,
		.out_context.out_file = out_file,
		.out_context.writer_cb = pfb_out_context_write_FILE
	};

	pfb_init_in_contexts(in_paths_list, &pcc);

	return pcc;
}

pfb_context_collect_t pfb_init_contexts_BUFFER(paths_list_t in_paths_list,
		pfb_out_buffer_t out_buffer[static 1])
{
	ASSERT(in_paths_list.len > 0);

	pfb_context_collect_t pcc = {
		.out_context.out_fname = nullptr,
		.out_context.out_buffer = out_buffer,
		.out_context.writer_cb = pfb_out_context_write_BUFFER
	};

	pfb_init_in_contexts(in_paths_list, &pcc);

	return pcc;
}

/**
 * Initialize a collection of input contexts and the one output context for the
 * de-duplication, sort, and consolidate to a single output.
 */
pfb_context_collect_t pfb_init_contexts(paths_list_t in_paths_list,
		const char out_fname[static 1])
{
	ASSERT(in_paths_list.len > 0);

	pfb_context_collect_t pcc = {};

	pcc.out_context = pfb_init_out_context(out_fname);

	pfb_init_in_contexts(in_paths_list, &pcc);

	return pcc;
}

void pfb_open_out_context(pfb_out_context_t c[static 1], bool append_output)
{
	// if out_fname is nullptr, write to stdout.
	if(c->out_fname && c->out_file)
	{
		ELOG_STDERR("ERROR: OUTPUT file is already open: %s\n",
				c->out_fname);
		ASSERT(false && "ERROR: OUTPUT file is already open");
		return;
	}

	if(c->out_fname)
	{
		DEBUG_PRINTF("out fname: %s\n", c->out_fname);
		c->out_file = fopen(c->out_fname, append_output ? "ab" : "wb");
	}

	// c->out_buffer overlays out_file
	if(!c->out_file)
	{
		ELOG_STDERR("ERROR: failed to open file for writing in binary mode: %s\n",
				c->out_fname);
		ASSERT(false && "ERROR: failed to open file for writing in binary mode");
		return;
	}
}

static void pfb_open_context(pfb_context_t c[static 1])
{
	// using the file handle given earlier
	ASSERT(c->in_fname);

	if(c->in_file)
	{
		ELOG_STDERR("ERROR: INPUT file is already open: %s\n",
				c->in_fname);
		ASSERT(false && "ERROR: INPUT file is already open");
		return;
	}

	c->in_file = fopen(c->in_fname, "rb");
	if(!c->in_file)
	{
		ELOG_STDERR("ERROR: failed to open file for reading in binary mode: %s\n",
				c->in_fname);
		ASSERT(false && "ERROR: failed to open file for reading in binary mode");
		return;
	}
}

static void pfb_close_context(pfb_context_t c[static 1])
{
	// if file is created outside of the context and outside of a filename
	// specified i.e. it was created by temp() then the FILE* should remain open
	// and handled externally
	if(c->in_fname && c->in_file)
	{
		fclose(c->in_file);
		c->in_file = nullptr;
	}
	c->in_file = nullptr;
}

void pfb_close_out_context(pfb_out_context_t c[static 1])
{
	if(c->out_fname && c->out_file)
	{
		fclose(c->out_file);
	}
	// cannot close stdout. and if FILE* is given from outside this context,
	// then closing cannot be assumed safe inside.
	c->out_file = nullptr;
	// the out_file and out_buffer are in a union. the out_buffer is managed
	// outside of the out context similar to 'stdout'
	ASSERT(c->out_buffer == nullptr);
}

void pfb_free_out_context(pfb_out_context_t c[static 1])
{
	pfb_close_out_context(c);

	free(c->out_fname);
	c->out_fname = nullptr;

	free(c->buffer);
	c->buffer = nullptr;

	// these are in a union
	c->out_file = nullptr;
	ASSERT(c->out_buffer == nullptr);
}

void pfb_free_context(pfb_context_t c[static 1])
{
	pfb_close_context(c);
	free(c->in_fname);
	c->in_fname = nullptr;
	free(c->mem_buffer);
	c->mem_buffer = nullptr;
	free_carry_over(&c->co);
}

/**
 * Free all context collectively. There is no "pfb_free_context" because the set
 * of them share a pointer to DomainTree and only the first context has a
 * pointer to the DomainTree_t*. Yes, double pointer.
 */
void pfb_free_contexts(pfb_contexts_t cs[static 1])
{
	if(cs->begin_context)
	{
		for(pfb_context_t *c = cs->begin_context; c < cs->end_context; c++)
		{
			pfb_free_context(c);
		}

		free(cs->begin_context);
	}
	cs->begin_context = nullptr;
	cs->end_context = nullptr;
}

void pfb_free_context_collect(pfb_context_collect_t pcc[static 1])
{
	pfb_free_contexts(&pcc->in_contexts);
	pfb_free_out_context(&pcc->out_context);
}

void pfb_open_contexts(pfb_contexts_t in_contexts[static 1])
{
	for(pfb_context_t *pfbc = in_contexts->begin_context;
			pfbc < in_contexts->end_context; pfbc++)
	{
		pfb_open_context(pfbc);
	}
}

void pfb_close_contexts(pfb_contexts_t in_contexts[static 1])
{
	for(pfb_context_t *pfbc = in_contexts->begin_context;
			pfbc < in_contexts->end_context; pfbc++)
	{
		pfb_close_context(pfbc);
	}
}

/**
 * Provides a callback that is specific to handling reading the CSV file
 * pfBlockerNG produces and adds appropriate entries to the DomainTree.
 */
void pfb_read_all(TLD_implementation_t tld_impl, pfb_contexts_t cs[static 1])
{
	ASSERT(cs->begin_context);
	ASSERT(cs->begin_context != cs->end_context);

	DomainView_t dv;
	init_DomainView(&dv);

	ContextPair_t pc = {&tld_impl, &dv};

	for(pfb_context_t *pfbc = cs->begin_context; pfbc < cs->end_context; pfbc++)
	{
		if(pfbc->in_fname)
		{
			DEBUG_PRINTF("Reading %s...\n", pfbc->in_fname);
		}
		else
		{
			DEBUG_PRINTF("Reading from unnamed input\n");
		}
		pfb_read_one_context(pfbc, pfb_insert, &pc);
	}

	free_DomainView(&dv);
}

static void write_line_from_buffer(pfb_context_t in_c[static 1], line_info_t li,
		pfb_out_context_t out_c[static 1])
{
	ASSERT(in_c);
	ASSERT(out_c);
	ASSERT(in_c->in_file);
	ASSERT(out_c->out_file);
	ASSERT(in_c->mem_buffer);

	// out_c might have a flag to write to disk or write to an in-memory
	// buffer.. in the diff scenario, it's one less disk i/o operation.

	const size_t wrote_size = fwrite(&in_c->mem_buffer[li.offset], sizeof(char),
			li.line_len, out_c->out_file);
	UNUSED(wrote_size);
	ASSERT(wrote_size == li.line_len);

	// write the trailing \n character
	out_c->writer_cb("\n", 1, out_c);
}

static const uint rw_buffer_size = 512;

static void write_line_from_file(pfb_context_t in_c[static 1], line_info_t li,
		pfb_out_context_t out_c[static 1])
{
	ASSERT(in_c);
	ASSERT(out_c);
	ASSERT(in_c->in_file);
	ASSERT(out_c->out_file);
	ASSERT(out_c->buffer);

	fseek(in_c->in_file, li.offset, SEEK_SET);

	// the first time through, we know we have a domain consisting of at
	// least four characters: a.tu
	// the second loop through if the first doesn't find a newline, we might
	// have in the first character the newline character.
	//
	// this could be on a context that combines the outfile and this once
	// allocated buffer to be used during the write phase.
	//
	// TODO XXX the out_c buffer is a fixed size. if the line length is longer
	// than the allocated buffer, we have overwrite. the read has to be done in
	// multiple parts.
	ASSERT(li.line_len <= rw_buffer_size);
	const size_t read_size = fread(out_c->buffer, sizeof(char),
			li.line_len, in_c->in_file);

	ASSERT(read_size == li.line_len);
	ASSERT(read_size > 0);

#if 0
	DEBUG_PRINTF("write from file .. line len=%u\n", li.line_len);
	DEBUG_PRINTF("write from file .. offset=%lu\n", li.offset);
#endif
	const size_t wrote_size = out_c->writer_cb(out_c->buffer, read_size, out_c);
	UNUSED(wrote_size);
	ASSERT(wrote_size == read_size);
}

/**
 * Reads from the input context the amount of data specified by the given
 * line_info_t and writes the same amount of data to the output context.
 *
 * NOTE: when the input context is held entirely in memory, the read operation
 * is literally an index into a buffer. it might remain a memcpy to ensure a
 * single \n character is appended. the input might be in bogus \r\n mode e.g.
 * alternatively, it writes directly form the input context the amount of data
 * specified and follows up with writing a single \n character.
 */
static void pfb_write_line(pfb_context_t in_c[static 1], line_info_t li,
		pfb_out_context_t out_c[static 1])
{
	ASSERT(in_c);
	ASSERT(out_c);
	ASSERT(in_c->in_file);
	ASSERT(out_c->out_file);

	// defer decision of in memory write or read from file write until this
	// moment. if earlier in the read, a buffer for input was allocated, we'll
	// re-read from it to do the write.
#if 0
	DEBUG_PRINTF("write line mem buffer? %p\n", in_c->mem_buffer);
	DEBUG_PRINTF("li offset=%lu\n", li.offset);
	DEBUG_PRINTF("li len=%u\n", li.line_len);
#endif
	if(in_c->mem_buffer)
	{
		write_line_from_buffer(in_c, li, out_c);
	}
	else
	{
		// otherwise, ensure an output buffer is allocated and write by reading
		// from input file into this output buffer and write to disk from the
		// output buffer.
		if(out_c->buffer == nullptr)
		{
			out_c->buffer = malloc(rw_buffer_size * sizeof(char));
			CHECK_MALLOC(out_c->buffer);
		}
		write_line_from_file(in_c, li, out_c);
	}
}

static void pfb_write_DomainInfo(DomainInfo_t *di[static 1], void *context)
{
	ASSERT(*di);
	ASSERT(context);

	// contains output context?
	pfb_out_context_t *output_context = (pfb_out_context_t*)context;
	pfb_context_t *input_context = (*di)->context;
	ASSERT(input_context);
	ASSERT(output_context);

	pfb_write_line(input_context, (*di)->li, output_context);

	// this "collector" transfers the info on DomainInfo into the new container.
	// the DomainInfo is effectively dangling. it's parent holder, the
	// DomainTree, will be destroyed soon after returning from this function.
	free_DomainInfo(di);
	ASSERT(*di == nullptr);

	output_context->counter++;
}

void pfb_write_carry_over(pfb_context_collect_t pcc[static 1])
{
	if(pcc->in_contexts.end_context - pcc->in_contexts.begin_context == 1)
	{
		for(size_len_t i = 0; i < pcc->in_contexts.begin_context->co.used;
				i++)
		{
			// write carry over lines to output context
			pfb_write_line(pcc->in_contexts.begin_context,
					pcc->in_contexts.begin_context->co.li[i],
					&pcc->out_context);
		}
	}
	else
	{
		// write a header consisting of each file name / full path of the
		// original set of files that this list is composed from.
		// include a generic header for the Adblplus syntax marker.
	}
}


/**
 * Move all DomainInfo into a flat array of DomainInfo. Sort the DomainInfo
 * based on line number.
 */
void pfb_consolidate(TLD_implementation_t tld_impl, pfb_out_context_t out_context[static 1])
{
	ASSERT(tld_impl.impl_funcs);
	ASSERT(tld_impl.context);

	tld_impl.impl_funcs->sort_domain_entries(tld_impl.context);

	TLD_EntryIter_t it = nullptr;
	DomainTree_t **dt = nullptr;
	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &it, &dt);
	// TODO may be cases where the tree is empty...
	ASSERT(*dt);

	while(dt != nullptr && *dt != nullptr)
	{
		transfer_DomainInfo(dt, pfb_write_DomainInfo, out_context);
		free_DomainTreePtr(dt);
		dt = tld_impl.impl_funcs->next_used_tld_entry(it);
	}

	tld_impl.impl_funcs->free_entry_iter(&it);
}

pfb_context_t pfb_context_from_FILE(FILE *tmp)
{
	pfb_context_t ret = {
		.in_file = tmp,
		.in_fname = nullptr,
		// this is for diff'ing. when diffing files on disk, the read is done
		// twice: once to collect litelines, then 2nd time for each line to
		// diff.
		.use_mem_buffer = false,
		.mem_buffer = nullptr,
	};

	fseek(tmp, 0L, SEEK_SET);
	fseek(tmp, 0L, SEEK_END);
	ret.file_size = ftell(tmp);
	fseek(tmp, 0L, SEEK_SET);

	return ret;
}

void pfb_free_out_buffer(pfb_out_buffer_t pob[static 1])
{
	ASSERT(pob);
	DEBUG_PRINTF("pob final buffer allocation=%lu\n", pob->alloc_len);
	DEBUG_PRINTF("pob final buffer used=%lu\n", pob->next_idx);
	DEBUG_PRINTF("pob buffer overalloc=%lu\n", pob->alloc_len - pob->next_idx);
	DEBUG_PRINTF("pob li alloc=%u\n", pob->litelines.alloc);
	DEBUG_PRINTF("pob li used=%u\n", pob->litelines.used);
	DEBUG_PRINTF("pob li overalloc=%u\n", pob->litelines.alloc - pob->litelines.used);
	free(pob->buffer);
	pob->buffer = nullptr;
	pob->alloc_len = 0;
	free(pob->litelines.li);
	pob->litelines.li = nullptr;
}

