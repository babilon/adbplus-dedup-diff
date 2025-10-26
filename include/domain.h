/**
 * domain.h
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
#include "matchstrength.h"
#include "const_str_type.h"
#include "carry_over.h"

/**
 * This is to reference a domain as it is being inserted into the DomainTree.
 * One instance of this struct per thread could be used to process the file.
 * Values within are reassigned as a line is read and processed.
 *
 * This breaks a domain apart into labels so insertion into the tree is
 * 'trivial'.
 *
 * This is a 'view'. It does not hold a copy; instead, it points to an existing
 * char[] held elsewhere for an intermittent time, until all segments are
 * inserted into the DomainTree or the entire domain is discarded.
 */
typedef struct DomainView
{
	// indicates which file this line was read from.
	// captured for de-duplication and carried until transfered to a DomainInfo
	// for final output.
	struct pfb_context *context;
	line_info_t li;

	// input:
	// blarg.ignored.www.google.com 0
	// 2.blarg.ignored.www.google.com 0
	// www.google.com 1
	//	 google.com 0
	// www.google.com
	//
	// after a '1' match is encountered, the segs_alloc will never grow.
	const_str_t fqd;

	// blarg.ignored. www. google. com 0
	// 0	 6	   13   16	  22
	// 2.blarg.ignored. www. google. com 0
	// 0 2	 8	   15   19	  25
	// www.google.com 1
	// 0   3	  9
	//	 google.com 0
	// www.google.com
	//
	// this array is in reverse order like the 'labels' was designed so
	// adding and removing is a matter of referencing segs_used.
	// new entries are always 0
	// removed entries can be ignored.
	// decrement the entries remaining.
	size_len_t* label_indexes;
	subdomain_len_t* lengths;
	size_len_t segs_used;
	size_len_t segs_alloc;

	// used to carry until the domain is inserted into the DomainTree.
	enum MatchStrength match_strength;

#ifdef COLLECT_DIAGNOSTICS
	size_len_t count_realloc;
	size_len_t max_used;
#endif
} DomainView_t;

typedef struct DomainViewIter
{
	size_len_t cur_seg;
	DomainView_t *dv;
} DomainViewIter_t;

/**
 * Temporary struct to reference a subdomain of a Domain and share it elsewhere
 * like hashing to find entry location into DomainTree_t.
 */
typedef struct SubdomainView
{
	// pointer into DomainView::fqd
	char const *data;
	// number of bytes in 'data' to read
	subdomain_len_t len;
} SubdomainView_t;

extern void init_DomainView(DomainView_t dv[static 1]);
extern bool update_DomainView(DomainView_t dv[static 1], char const *fqd, size_len_t len);
extern void free_DomainView(DomainView_t dv[static 1]);

extern DomainViewIter_t begin_DomainView(DomainView_t *dv);
extern bool next_DomainView(DomainViewIter_t *it, SubdomainView_t *sdv);
extern bool null_DomainView(DomainView_t const dv[static 1]);
