/**
 * domain.c
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
#include "logdiagnostics.h"
#include "domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static const uint MAX_DOMAIN_LABEL = 63;
static const uint DOMAIN_INIT_ALLOC = 4;

/**
 * @param fqd malloc'ed char[] holding the full domain name
 * @param len Number of bytes in the fqd.
 * @param match Number indicating match strength of this entry.
 */
void init_DomainView(DomainView_t dv[static 1])
{
	ASSERT(dv);
	*dv = (DomainView_t){};

	dv->match_strength = MATCH_NOTSET;

	// a lot of domains will have at least two. many will have more than two. a
	// default is a starting point. this 'view' is reused. the DomainInfo is a
	// copy of the domain and allocated to hold exactly as many segements as is
	// necessary
	dv->segs_alloc = DOMAIN_INIT_ALLOC;

	dv->label_indexes = malloc(sizeof(size_len_t) * DOMAIN_INIT_ALLOC);
	CHECK_MALLOC(dv->label_indexes);

	dv->lengths = malloc(sizeof(subdomain_len_t) * DOMAIN_INIT_ALLOC);
	CHECK_MALLOC(dv->lengths);
}

void free_DomainView(DomainView_t dv[static 1])
{
	ASSERT(dv);
	free(dv->lengths);
	free(dv->label_indexes);

#ifdef COLLECT_DIAGNOSTICS
	if(dv->count_realloc > 0)
	{
		LOG_DIAG("DomainView", dv,
				"guts realloc'ed %lu times to final size of %lu with %lu used.\n\n",
				(size_t)dv->count_realloc, (size_t)dv->segs_alloc,
				(size_t)dv->max_used);
	}
	else if(!dv->lengths)
	{
		// not an interesting statistic
	}
	else
	{
		LOG_DIAG("DomainView", dv,
				"guts were never realloc'ed beyond initial size of %lu with %lu used.\n\n",
				(size_t)dv->segs_alloc, (size_t)dv->max_used);
	}
#endif

	*dv = (DomainView_t){};
}

/**
 * Used when looking up keys in the tree to find where this domain is to be
 * inserted if at all. as each segment needs to be looked up individually.
 */
static bool get_SubdomainView(DomainView_t *dv, size_len_t idx,
		SubdomainView_t *sdv)
{
	ASSERT(sdv);
#ifdef USE_MEMSET
	memset(sdv, 0, sizeof(SubdomainView_t));
#endif

	if(!dv)
	{
		return false;
	}

	if(!dv->segs_used)
	{
		return false;
	}

	if(idx > dv->segs_used)
	{
		return false;
	}

	sdv->data = dv->fqd.data + dv->label_indexes[idx];
	sdv->len = dv->lengths[idx];

	return true;
}

DomainViewIter_t begin_DomainView(DomainView_t *dv)
{
	DomainViewIter_t it = {.cur_seg=0, .dv=dv};

	return it;
}

/**
 * Get the subdomain string as a pointer into the string and a length. now it
 * can go elsewhere for use like printf.
 */
bool next_DomainView(DomainViewIter_t *it, SubdomainView_t *sdv)
{
	ASSERT(it);
	bool b = false;

	if(it && it->cur_seg < it->dv->segs_used)
	{
		b = get_SubdomainView(it->dv, it->cur_seg, sdv);
		it->cur_seg += b;
	}

	return b;
}

bool null_DomainView(DomainView_t const dv[static 1])
{
	// check one of the pointers should be sufficient
	return dv->lengths == nullptr;
}

static void realloc_labels(DomainView_t *dv, size_len_t count)
{
	dv->segs_alloc += count;
	CHECK_REALLOC(dv->label_indexes, sizeof(size_len_t) * dv->segs_alloc);
	CHECK_REALLOC(dv->lengths, sizeof(subdomain_len_t) * dv->segs_alloc);
#ifdef COLLECT_DIAGNOSTICS
	dv->count_realloc++;
#endif
}

/**
 * Suppose this is fed domains which are certain to be subdomains of the given
 * DomainView_t or entirely unique and a new DomainView_t is required. this then returns
 * the same DomainView_t or a new one to update in a container elsewhere.
 *
 * @param d DomainView_t Must be non-nil and ready. Internals will be reallocated as
 * necessary and initialized to segments of the given fqd. The given fqd will be
 * given to the DomainView_t for ownership.
 *
 * @rationale Split a given string holding a domain into segments and store the
 * segmented information in the given DomainView_t instance. If a nil DomainView_t
 * instance is provided, one will be created. The idea is to break the domain
 * into the segments to facilitate insertion into the DomainTree_t which will
 * examine each segment starting at the end or TLD traversing backwards along
 * the domain for each subdomain and finding where it is unique among the
 * existing entries in the tree. If a unique place is found, the DomainView_t is
 * inserted and kept. or possibly transformed to a different type with
 * infomration like which line in the csv file the original data is from and
 * match strength.
 */
bool update_DomainView(DomainView_t dv[static 1], char const *fqd, size_len_t len)
{
	if(!fqd || len == 0)
	{
		return false;
	}

	if(null_DomainView(dv))
	{
		return false;
	}

	dv->fqd.data = fqd;
	dv->fqd.len = len;
	dv->segs_used = 0;

	char const *begin = dv->fqd.data;
	char const *end = begin + dv->fqd.len - 1;
	char const *c = end;
	char const *prev = c;

	while(c != begin)
	{
		if(*c == '.')
		{
			if(dv->segs_used >= dv->segs_alloc)
			{
				realloc_labels(dv, 4);
			}
			// 01234567890
			// www.google.com
			// b		 c  e
			//
			dv->label_indexes[dv->segs_used] = (size_t)(c - begin) + 1;
			// 0123456789
			// google.com
			//	   c  p
			//	   6  9 => 9-6=3
			const size_t tmp = prev - c;
			if(tmp > MAX_DOMAIN_LABEL)
			{
				if(tmp > UCHAR_MAX)
				{
					ELOG_STDERR("ERROR: segment is longer than allowable in unsigned char.\n");
					return false;
				}
				else
				{
					ELOG_STDERR("WARNING: segment is longer than allowable maximum.\n");
				}
			}
			dv->lengths[dv->segs_used] = tmp;
			dv->segs_used++;

			// move behind the '.'
			// google.com
			//	  c
			//	  p
			c--;
			prev = c;
		}
		else
		{
			c--;
		}
	}

	if(dv->segs_used >= dv->segs_alloc)
	{
		realloc_labels(dv, 1);
	}

	dv->label_indexes[dv->segs_used] = 0;
	dv->lengths[dv->segs_used] = prev - c + 1; // off by one
	dv->segs_used++;
#ifdef COLLECT_DIAGNOSTICS
	if(dv->segs_used > dv->max_used)
		dv->max_used = dv->segs_used;
#endif

	return true;
}

#ifdef BUILD_TESTS
DomainView_t parse_Domain(char const *fqd, size_len_t len)
{
	DomainView_t dv;

	init_DomainView(&dv);

	update_DomainView(&dv, fqd, len);

	return dv;
}

static DomainViewIter_t end_DomainView(DomainView_t *dv)
{
	DomainViewIter_t it;

#ifdef USE_MEMSET
	memset(&it, 0, sizeof(DomainViewIter_t));
#endif

	it.dv = dv;
	it.cur_seg = it.dv->segs_used;

	return it;
}

static bool null_SubdomainView(SubdomainView_t const *const sdv)
{
	return sdv->data == nullptr;
}

/**
 * Only for tests.
 */
static bool prev_DomainView(DomainViewIter_t *it, SubdomainView_t *sdv)
{
	bool b = false;

	if(it && it->cur_seg <= it->dv->segs_used)
	{
		it->cur_seg--;
		b = get_SubdomainView(it->dv, it->cur_seg, sdv);
	}

	return b;
}

static void test_null_Domain()
{
	DomainView_t dv;
	memset(&dv, 1, sizeof(DomainView_t));

	assert(!null_DomainView(&dv));

	memset(&dv, 0, sizeof(DomainView_t));

	assert(null_DomainView(&dv));

	init_DomainView(&dv);

	assert(!null_DomainView(&dv));

	free_DomainView(&dv);

	assert(null_DomainView(&dv));
}

static void test_free_Domain()
{
	DomainView_t dv, dv_zero;
	memset(&dv, 0, sizeof(DomainView_t));
	memset(&dv_zero, 0, sizeof(DomainView_t));

	free_DomainView(&dv);

	// free didn't change the struct
	assert(!memcmp(&dv_zero, &dv, sizeof(DomainView_t)));
}

static void test_init_Domain()
{
	DomainView_t dv, dv_zero;
	memset(&dv, 0xf, sizeof(DomainView_t));
	memset(&dv_zero, 0, sizeof(DomainView_t));

	init_DomainView(&dv);

	assert(!dv.fqd.data);
	assert(dv.fqd.len == 0);
	assert(dv.segs_used == 0);
	// at least one 'block' of memory allocated to guarantee malloc returns a
	// non-nil pointer to valid memory. malloc of size zero is undefined on some
	// systems. the initialize is intended to prepare the struct for usage and
	// requires a free_CsvLineView() call afterwards.
	assert(DOMAIN_INIT_ALLOC > 0);
	assert(dv.segs_alloc == DOMAIN_INIT_ALLOC);
	assert(dv.label_indexes);
	assert(dv.lengths);
	assert(dv.match_strength == -1);

	dv.segs_used = 1;

	free_DomainView(&dv);

	assert(dv.segs_used == 0);
	assert(dv.segs_alloc == 0);
	assert(!dv.label_indexes);
	assert(!dv.lengths);

	assert(null_DomainView(&dv));

#ifdef USE_MEMSET
	assert(!memcmp(&dv_zero, &dv, sizeof(DomainView_t)));
#endif
}

#define ASSERT_DOMAIN_SEG(value, idx) \
	assert(dv.lengths[idx] == strlen(value)); \
	assert(!memcmp(dv.fqd.data + dv.label_indexes[idx], value, strlen(value))); \

static void test_parse_Domain()
{
	DomainView_t dv;

#define ASSERT_PARSE(value, segments) \
	dv = parse_Domain(value, strlen(value)); \
	assert(dv.fqd.len = strlen(value)); \
	assert(dv.segs_used == segments); \
	assert(dv.segs_alloc >= segments)
	ASSERT_PARSE("this.domain.com", 3);
	ASSERT_DOMAIN_SEG("com", 0);
	ASSERT_DOMAIN_SEG("domain", 1);
	ASSERT_DOMAIN_SEG("this", 2);
#undef ASSERT_PARSE

	free_DomainView(&dv);
}

#define ASSERT_UPDATE(value, ret, segments) \
	assert(ret == update_DomainView(&dv, value, strlen(value))); \
	assert(dv.fqd.len == strlen(value)); \
	assert(dv.segs_used == segments); \
	assert(dv.segs_alloc >= segments)

static void test_DomainIter(DomainView_t *dv, size_len_t segments,
		const char **values)
{
	DomainViewIter_t it, end;
	SubdomainView_t sdv;

	it = begin_DomainView(dv);

	assert(it.cur_seg == 0);
	assert(it.dv = dv);

#define ASSERT_NEXT(idx) \
	assert(next_DomainView(&it, &sdv)); \
	assert(!null_SubdomainView(&sdv)); \
	assert(it.cur_seg == (idx + 1)); \
	assert(sdv.len == strlen(values[idx])); \
	assert(!memcmp(sdv.data, values[idx], strlen(values[idx])))
	for(size_len_t i = 0; i < segments; i++)
	{
		ASSERT_NEXT(i);
	}
	assert(!next_DomainView(&it, &sdv));
#undef ASSERT_NEXT

	end = end_DomainView(dv);
	assert(end.cur_seg == segments);
	assert(end.dv = dv);

#define ASSERT_NEXT(idx) \
	assert(prev_DomainView(&end, &sdv)); \
	assert(!null_SubdomainView(&sdv)); \
	assert(end.cur_seg == idx); \
	assert(sdv.len == strlen(values[idx])); \
	assert(!memcmp(sdv.data, values[idx], strlen(values[idx])))
	for(int i = segments - 1; i >= 0; i--)
	{
		ASSERT_NEXT((size_len_t)i);
	}
	assert(!prev_DomainView(&end, &sdv));
#undef ASSERT_NEXT
}

static void test_update_Domain()
{
	DomainView_t dv;
	SubdomainView_t sdv;

	init_DomainView(&dv);

	ASSERT_UPDATE("this.domain.com", true, 3);
	ASSERT_DOMAIN_SEG("com", 0);
	ASSERT_DOMAIN_SEG("domain", 1);
	ASSERT_DOMAIN_SEG("this", 2);

	const char* values1[] = {"com", "domain", "this"};
	test_DomainIter(&dv, 3, values1);

	ASSERT_UPDATE("four.segment.world.net", true, 4);
	ASSERT_DOMAIN_SEG("net", 0);
	ASSERT_DOMAIN_SEG("world", 1);
	ASSERT_DOMAIN_SEG("segment", 2);
	ASSERT_DOMAIN_SEG("four", 3);

	const char* values2[] = {"net", "world", "segment", "four"};
	test_DomainIter(&dv, 4, values2);

	ASSERT_UPDATE("very.long.subdomain.that.never.ends.around.world", true, 8);
	ASSERT_DOMAIN_SEG("world", 0);
	ASSERT_DOMAIN_SEG("around", 1);
	ASSERT_DOMAIN_SEG("ends", 2);
	ASSERT_DOMAIN_SEG("never", 3);
	ASSERT_DOMAIN_SEG("that", 4);
	ASSERT_DOMAIN_SEG("subdomain", 5);
	ASSERT_DOMAIN_SEG("long", 6);
	ASSERT_DOMAIN_SEG("very", 7);

#define ASSERT_SUBDV(idx, value) \
	assert(get_SubdomainView(&dv, idx, &sdv)); \
	assert(!null_SubdomainView(&sdv)); \
	assert(sdv.len == strlen(value)); \
	assert(!memcmp(sdv.data, value, strlen(value)))
	ASSERT_SUBDV(4, "that");
	ASSERT_SUBDV(0, "world");
#undef ASSERT_SUBDV

	free_DomainView(&dv);
}
#undef ASSERT_UPDATE

static void test_nil_fqd()
{
	DomainView_t dv;

	init_DomainView(&dv);

	assert(!update_DomainView(&dv, "non-nil.mil", 0));
	assert(!dv.fqd.data);
	assert(dv.fqd.len == 0);
	assert(dv.label_indexes);
	assert(dv.lengths);
	assert(dv.segs_used == 0);
	assert(dv.segs_alloc >= 1);

	assert(!update_DomainView(&dv, nullptr, 10));
	assert(!dv.fqd.data);
	assert(dv.fqd.len == 0);
	assert(dv.label_indexes);
	assert(dv.lengths);
	assert(dv.segs_used == 0);
	assert(dv.segs_alloc >= 1);

	free_DomainView(&dv);
}

static void test_nil_DomainView()
{
	DomainView_t dv;
	SubdomainView_t sdv;
	memset(&dv, 0, sizeof(DomainView_t));

	assert(!update_DomainView(&dv, "non-nil.zil", strlen("non-nil.zil")));
	assert(!get_SubdomainView(nullptr, 1, &sdv));
	assert(!get_SubdomainView(&dv, 1, &sdv));
	assert(!get_SubdomainView(&dv, 0, &sdv));

	free_DomainView(&dv);
}

static void test_long_label()
{
	DomainView_t dv;

	init_DomainView(&dv);

	char *long_label = malloc(sizeof(char) * UCHAR_MAX);
	CHECK_MALLOC(long_label);

	memset(long_label, 'a', UCHAR_MAX);
	long_label[10] = '.';
	long_label[20] = '.';
	long_label[30] = '.';
	long_label[50] = '.';

	// the TLD is well more than MAX_DOMAIN_LABEL (63) BYTES
	assert(update_DomainView(&dv, long_label, UCHAR_MAX));

	free_DomainView(&dv);
	free(long_label);
}

static void test_too_long()
{
	DomainView_t dv;

	init_DomainView(&dv);

	char *too_long = malloc(sizeof(char) * UCHAR_MAX * 2);
	CHECK_MALLOC(too_long);

	memset(too_long, 'a', UCHAR_MAX * 2);
	too_long[10] = '.';
	too_long[20] = '.';
	too_long[30] = '.';
	too_long[50] = '.';

	// the TLD is well beyond what can fit in an 'unsigned char'
	assert(!update_DomainView(&dv, too_long, UCHAR_MAX * 2));

	free_DomainView(&dv);
	free(too_long);
}

void info_domain()
{
	printf("Sizeof DomainView_t: %lu\n", sizeof(DomainView_t));
	printf("Sizeof DomainViewIter_t: %lu\n", sizeof(DomainViewIter_t));
	printf("Sizeof SubdomainView_t: %lu\n", sizeof(SubdomainView_t));
}

void test_domain()
{
	printf("Testing 'Domain'\n");
	test_null_Domain();
	test_free_Domain();
	test_init_Domain();
	test_parse_Domain();
	test_update_Domain();
	test_nil_fqd();
	test_nil_DomainView();
	test_long_label();
	test_too_long();
	printf("Tested 'Domain'\n");
}
#endif
