/**
 * domaintree.c
 *
 * Part of pfb_adplus_dedup_diff
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
#include "domaintree.h"
#include "domaininfo.h"
#include "domain.h"
#include "tld_context.h"

/**
 * Create and initialize a DomainInfo_t. Returned pointer must be freed with
 * free_DomainInfo().
 */
static DomainInfo_t* init_DomainInfo(size_len_t/* len*/)
{
	DomainInfo_t *di = calloc(1, sizeof(DomainInfo_t));
	CHECK_MALLOC(di);

	return di;
}

void free_DomainInfo(DomainInfo_t **di)
{
	free(*di);
	*di = nullptr;
}

/**
 * Return a malloc'ed DomainInfo_t that contains a copy of the data from the
 * given DomainView_t. The returned pointer must be freed with
 * free_DomainInfo().
 */
DomainInfo_t *convert_DomainInfo(DomainView_t *dv)
{
	DomainInfo_t *di = init_DomainInfo(dv->fqd.len);

	di->match_strength = dv->match_strength;
	di->context = dv->context;
	di->li = dv->li;

	return di;
}

void free_DomainTreePtr(DomainTree_t **dt)
{
	free(*dt);
	*dt = nullptr;
}

/**
 * This being extern allows outside sources use it in a callback to visit every
 * item in their own tree/list/struct to delete the DomainInfo held. And keeps
 * the free_DomainInfo hidden to within here.
 *
 * This being static .. keeps it local to this as it is a callback and the void*
 * is bogus to outsiders for the sake of free() calls. it's also ignored by this
 * particular callback.
 */
static void DomainInfo_deleter(DomainInfo_t **di, void*)
{
	free_DomainInfo(di);
	ASSERT(!*di);
}

static int sort_by_tld(const char *a, uchar len_a, const char *b,
		uchar len_b)
{
	ASSERT(a);
	ASSERT(b);
	ASSERT(len_a > 0);
	ASSERT(len_b > 0);
	int first_n = memcmp(a, b, MIN(len_a, len_b));
	int ret;
	if(first_n == 0)
	{
		ret = (int)len_a - (int)len_b;
	}
	else
		ret = first_n;
	return ret;
}

static int sort_DomainTree_by_tld(DomainTree_t a[static 1], DomainTree_t b[static 1])
{
	ASSERT(a);
	ASSERT(b);
	ASSERT(a->tld);
	ASSERT(b->tld);
	return sort_by_tld(a->tld, a->len, b->tld, b->len);
}

/**
 * Visits every leaf of the tree depth first and calls the given collector
 * passing the DomainInfo of that leaf along with the given context. Then frees
 * the internal structures. The collector owns all DomainInfo instances once
 * held by the tree.
 *
 * The DomainTree is free'd after this operation and the given root is NIL.
 */
void transfer_DomainInfo(DomainTree_t **root,
		void(*collector)(DomainInfo_t **di, void *context), void *context)
{
	ASSERT(root);
	if(*root == nullptr)
	{
		return;
	}

	// sort is delayed until the last minute and is always exercised before
	// transfering, i.e., destroying the uthash.
	HASH_SRT(hh, *root, sort_DomainTree_by_tld);

	DomainTree_t *current = nullptr, *tmp = nullptr;
	HASH_ITER(hh, *root, current, tmp)
	{
		// must visit each child
		transfer_DomainInfo(&(*root)->child, collector, context);

		HASH_DEL(*root, current);

		if(current->di)
		{
			ASSERT(current->di->match_strength > MATCH_NOTSET);
			// this callback might end up being the one that writes straight to
			// the output? then it doesn't collect into an array and then
			// write.. caveat is it will read from whichever input file the line
			// that is represented and write that out.
			collector(&(current->di), context);
			ASSERT(!current->di);
		}
		free_DomainTreePtr(&current);
	}

	*root = nullptr;
}

/**
 * Delete the tree starting at the given root.
 *
 *			.google.com : nil-di
 *			^ free all items below this
 *		 abc.google.com : nil-di
 *		 ^
 * blarg.abc.google.com : di
 * clobr.abc.google.com : di
 * ^
 *		 www.google.com : nil-di
 *		 ^
 * blarg.www.google.com : di
 * ^
 */
void free_DomainTree(DomainTree_t **root)
{
	transfer_DomainInfo(root, DomainInfo_deleter, nullptr);
	ASSERT(!*root);
}

/**
 * Replace the given DomainTree's DomainInfo with one described by the given
 * DomainView. Creates a new DomainInfo from the given DomainView and assigns to
 * the given entry. The old DomainInfo is freed before overwriting. No
 * modifications are done to the DomainTree's structure.
 */
static void replace_DomainInfo(DomainTree_t *entry, DomainView_t *dv)
{
	free_DomainInfo(&entry->di);
	entry->di = convert_DomainInfo(dv);
}

static DomainTree_t *init_DomainTree(SubdomainView_t const *sdv)
{
	// the DomainTree_t instance must be memset since it is inserted into a
	// UT_hash. alternate options available such as defining a hash function and
	// comparison. default requires all padding be zero'ed.
	DomainTree_t *ndt = calloc(1, sizeof(DomainTree_t) + sizeof(char) * sdv->len);
	CHECK_MALLOC(ndt);

	ndt->len = sdv->len;

	memcpy(ndt->tld, sdv->data, sdv->len);

	return ndt;
}

/**
 * Internal to the construction of the tree. though it could be used outside to
 * initialize the tree.
 */
static DomainTree_t* ctor_DomainTree(DomainTree_t **dt, DomainViewIter_t it[static 1],
		SubdomainView_t sdv[static 1])
{
	ASSERT(dt);

	DomainTree_t *ndt = nullptr;

	// (0) enters with 'com' or 'net' or 'org' a TLD
	do
	{
		// (0) enters with 'com' or 'net' or 'org' a TLD
		// (1) create entry for 'google'
		// (2) create entry for 'www'
		ndt = init_DomainTree(sdv);
		ASSERT(ndt);

		// (0) nullptr dt is OK it means the HASH table is empty
		// (0) add 'com' to the HASH table next to 'org', 'net', etc.
		// (1) add 'google' to NEW HASH table [(0) 'dt->child']
		// (2) add 'www' to NEW HASH table [(1) 'dt->child']
#ifndef RELEASE
		//for debug/testing phase
		DomainTree_t *unexpected_existing = nullptr;
		HASH_FIND(hh, *dt, ndt->tld, ndt->len, unexpected_existing);
		ASSERT(!unexpected_existing);
#endif

		HASH_ADD_KEYPTR(hh, *dt, ndt->tld, ndt->len, ndt);

#if 0
		DEBUG_PRINTF("\tafter hash add, dt has %lu %.*s\n", (size_t)HASH_COUNT(*dt), (int)(*dt)->len, (*dt)->tld);
		DomainTree_t *rt = *dt, *t = nullptr, *tmp;
		HASH_ITER(hh, rt, t, tmp) {
			DEBUG_PRINTF("t %.*s\n", (int)t->len, t->tld);
		}
#endif
		// dt will be the next HASH table to insert items into. it is OK to be
		// nullptr. it means empty HASH.
		// (0) dt will be nullptr; this is a new entry for subdomains of 'com'
		// (1) dt will be nullptr; this is a new entry for subdomains of
		// 'google.com'
		// (2) dt will be nullptr; this is new entry for subdomains of 'www'
		dt = &(ndt->child);
		// (0) parent will be 'com'
		// (1) parent will be 'google'
		// (2) parent will be 'www'

	// (1) set sdv to 'google' in 'www.google.com'
	// (2) set sdv to 'www' in 'www.google.com'
	// (3) set sdv to <nullptr>; end of iteration
	} while(next_DomainView(it, sdv));

	// (3) sdv is nullptr; prev_sdv is 'www'
	// need to set the DomainInfo for 'www' held at ndt
	ASSERT(ndt);
	ASSERT(it->dv->match_strength > MATCH_NOTSET);
	ndt->di = convert_DomainInfo(it->dv);

	return ndt;
}

static void replace_if_stronger(DomainTree_t entry[static 1],
		DomainView_t dv[static 1])
{
	ASSERT(entry);
	ASSERT(dv);
	ASSERT(dv->match_strength > MATCH_NOTSET);
	ASSERT(dv->match_strength != MATCH_REGEX);

	if(entry->di == nullptr || dv->match_strength > entry->di->match_strength)
	{
		replace_DomainInfo(entry, dv);

		ASSERT(entry->di);
		if(entry->di->match_strength == MATCH_FULL)
		{
			free_DomainTree(&entry->child);
		}
		//DEBUG_PRINTF("[%s:%d] %s replace existing entry with stronger match; inserted.\n", __FILE__, __LINE__, __FUNCTION__);
		//DEBUG_PRINTF("\ttld=%.*s\n", (int)entry->len, entry->tld);
	}
	else // not strong enough to override
	{
		//DEBUG_PRINTF("[%s:%d] %s identical; skip insert.\n", __FILE__, __LINE__, __FUNCTION__);
		//DEBUG_PRINTF("\ttld=%.*s\n", (int)entry->len, entry->tld);
	}
}

static bool leaf_DomainTree(DomainTree_t const *dt)
{
	ASSERT(dt);
	// HASH_CNT(<hh>, <obj>)
	// even if 'child' is nullptr, HASH_COUNT has defined behavior and returns 0.
	return HASH_COUNT(dt->child) == 0;
}

// this also takes a TreeRoot_t ..
static DomainTree_t* find_leaf_Domain(DomainTree_t **root_dt, DomainViewIter_t it)
{
	ASSERT(root_dt);

	DomainTree_t **dt = root_dt;
	DomainTree_t *entry = nullptr;
	SubdomainView_t sdv;

	while(next_DomainView(&it, &sdv))
	{
		ASSERT(dt);
		ASSERT(sdv.data);
		ASSERT(sdv.len > 0);
		HASH_FIND(hh, *dt, sdv.data, sdv.len, entry);

		if(!entry)
		{
			// by this point, we don't know which level we're at. if at root, it
			// would need a new TreeRoot_t; otherwise it's a DomainTree. if the
			// first domain were inserted above this level and this took over
			// with only DomainTree..
			entry = ctor_DomainTree(dt, &it, &sdv);
			ASSERT(entry);
			ASSERT(entry->di);
			ASSERT(entry->di->match_strength > MATCH_NOTSET);
			return entry;
		}

		if(leaf_DomainTree(entry))
		{
			ASSERT(entry->di);
			ASSERT(entry->di->match_strength > MATCH_NOTSET);
			ASSERT(entry->di->match_strength != MATCH_REGEX);
			if(entry->di->match_strength == MATCH_FULL)
			{
				return nullptr;
			}
		}

		dt = &entry->child;
	}

	// if 'entry' is null, then 'dv' is garbage, i.e., not a domain.
	ASSERT(entry);
	return entry;
}

// returns DomainTree_t* for tests
// might have that disabled for non-test builds...
void insert_DomainTree(const TLD_implementation_t tld_impl,
		DomainView_t *dv)
{
	ASSERT(tld_impl.context);
	ASSERT(tld_impl.impl_funcs);
	ASSERT(dv);

	// mandate the match strength be set before inserting to communicate that
	// the tree insertion REQUIRES knowing this information or else it'll be a
	// bad day in hell to know why the matches are bogused. the insertion
	// evaluates this information.
	// should be impossible to get to without a programmer introduced error.
	if(dv->match_strength == MATCH_NOTSET)
	{
		ELOG_STDERR("ERROR: DomainView has uninitialized match_strength set; skip insertion.\n");
		return;
	}

	if(dv->match_strength == MATCH_BOGUS)
	{
		ELOG_STDERR("ALERT: DomainView has bogus match_strength set; skip insertion.\n");
		return;
	}

	// TODO search the TLD list to find the DomainTree_t** to pass to the second
	// call insert_Domain() and the DomainViewIter_t to skip the TLD element.

	// find the DomainTree_t in the RootTree_t; pass that dt to this.. and the
	// iter for domain view and it'll jump to the next one e.g. 'google' in
	// ads.google.com.
	DomainViewIter_t it = begin_DomainView(dv);
	SubdomainView_t sdv;
	const bool found = next_DomainView(&it, &sdv);
	// the domain view must have at least two segments to be valid. earlier
	// parsing should have ensured this is the case.
	UNUSED(found);
	ASSERT(found);
	DomainTree_t **dt = tld_impl.impl_funcs->insert_dt_entry_for_tld(
			tld_impl.context, sdv);
	// goal is to move the tld layer to a smaller struct that is possibly stored
	// in a binary tree instead of a hash table and is built at startup with the
	// most common tlds encountered with diagnostics for new entries.
	// one solution uses a flat array and binary search
	// 2nd solution uses the hash tree
	// 3rd solution is with a binary tree

	ASSERT(dt);
	DomainTree_t *entry = find_leaf_Domain(dt, it);
	// above will return nil if the new entry is already blocked by an existing
	// entry.
	if(entry)
	{
		replace_if_stronger(entry, dv);
	}
}

static void do_visit_DomainTree(DomainTree_t **root,
		void(*visitor_func)(DomainInfo_t **di, void *context),
		void *context)
{
	ASSERT(visitor_func);

	if(root == nullptr)
	{
		return;
	}

	DomainTree_t *dt, *tmp;

	HASH_ITER(hh, *root, dt, tmp)
	{
		// must visit each child
		do_visit_DomainTree(&(*root)->child, visitor_func, context);

		if(dt->di)
		{
			DEBUG_PRINTF("DT: Visited strength=%d label=%.*s\n", dt->di->match_strength, (int)dt->len, dt->tld);
			(*visitor_func)(&(dt->di), context);
		}
	}

}

void visit_DomainTree(DomainTree_t **root,
		void(*visitor_func)(DomainInfo_t **di, void *context),
		void *context)
{
	ASSERT(visitor_func);

	do_visit_DomainTree(root, visitor_func, context);
}

#ifdef BUILD_TESTS
typedef struct TestTable
{
	line_info_t li;
	UT_hash_handle hh;
} TestTable_t;

static void print_di(line_info_t *li, void *)
{
	ASSERT(li);
	printf("TT: Visited offset=%ld len=%u\n", li->offset, li->line_len);
}

void visit_TestTable(TestTable_t *root,
		void(*visitor_func)(line_info_t *di, void *context),
		void *context)
{
	if(root == nullptr)
		return;

	TestTable_t *tt, *tmp;

	HASH_ITER(hh, root, tt, tmp)
	{
		visitor_func(&tt->li, context);
	}
}

static void print_TestTable(TestTable_t *root)
{
	visit_TestTable(root, &print_di, nullptr);
}

static TestTable_t *root_visited = nullptr;

static void test_visitor(DomainInfo_t **di, void*)
{
	assert(*di);

	TestTable_t *entry, *tmp;

	entry = malloc(sizeof(TestTable_t));
	CHECK_MALLOC(entry);
	memset(entry, 0, sizeof(TestTable_t));

	entry->li = (*di)->li;

	// should not already exist when visiting
	HASH_FIND(hh, root_visited, &(*di)->li, sizeof(line_info_t), tmp);
	assert(!tmp);

	if(!tmp)
	{
		printf("didn't find offset=%ld len=%u\n", (*di)->li.offset, (*di)->li.line_len);
		HASH_ADD_KEYPTR(hh, root_visited, &(*di)->li, sizeof(line_info_t), entry);
	}
	else
		printf("found offset=%ld len=%u\n", (*di)->li.offset, (*di)->li.line_len);
	assert(entry);
}

#define INSERT_DOMAIN(value, strength, nilret) \
	update_DomainView(&dv, value, strlen(value)); \
	dv.li.line_len = strlen(value); \
	dv.li.offset = __LINE__; \
	dv.match_strength = strength; \
	insert_DomainTree(tld_impl, &dv);
#define FREE_VISITED \
	HASH_ITER(hh, root_visited, t, tmp) { \
		HASH_DEL(root_visited, t); \
		free(t); \
	} \
	assert(!root_visited)

extern TLD_implementation_t create_tld_hash_impl();
static void test_duplicates()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.somedomain.com", MATCH_FULL, true);

	// optional to sort for these tests
	//tld_impl.impl_funcs->sort_domain_entries(tld_impl.context);
	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);
	tld_impl.impl_funcs->next_used_tld_entry(eiter);

	// one domain. it is itself unique.
	// TODO need to get the root from the hash context ..
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	print_TestTable(root_visited);
	FREE_VISITED;

	// duplicate: skipped
	INSERT_DOMAIN("abc.www.somedomain.com", MATCH_FULL, false);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(root);
}

static void test_prune3()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	assert(tld_impl.context);

	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.somedomain.com", MATCH_FULL, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// this is inside a loop for the TLD
	//tld_impl.impl_funcs->next_used_tld_entry(eiter);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	// obliterates the other one
	INSERT_DOMAIN("somedomain.com", MATCH_FULL, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_prune2()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("www.somedomain.com", 1, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	// obliterates the other one
	INSERT_DOMAIN("somedomain.com", 1, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_weak3()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.somedomain.com", 0, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	// obliterates the other one
	INSERT_DOMAIN("somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_weak2()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("www.somedomain.com", 0, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	// obliterates the other one
	INSERT_DOMAIN("somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_unique_weak()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.somedomain.com", 0, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	INSERT_DOMAIN("go.abc.www.somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 4);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_unique_weak2()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("go.abc.www.somedomain.com", 0, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	INSERT_DOMAIN("abc.www.somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 4);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_unique_weak_strong()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.somedomain.com", 0, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	INSERT_DOMAIN("go.abc.www.somedomain.com", 0, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 1, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_unique_weak_to_strong()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("go.abc.www.somedomain.com", 1, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("abc.www.somedomain.com", 1, true);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 1, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 1, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_replace_weak_w_strong()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.weak-w-strong.com", 0, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("abc.www.weak-w-strong.com", 1, true);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_uninitialized()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);
	dv.li.offset = __LINE__;

	update_DomainView(&dv, "abc.www.strong-o-weak.com",
			strlen("abc.www.strong-o-weak.com"));
	insert_DomainTree(tld_impl, &dv);

	dv.match_strength = MATCH_BOGUS;
	insert_DomainTree(tld_impl, &dv);

	// TODO handle empty tree scenario; root is nil.
	//tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	//assert(*root);

	dv.match_strength = MATCH_FULL;
	insert_DomainTree(tld_impl, &dv);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_strong_over_weak()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("abc.www.strong-o-weak.com", 1, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("abc.www.strong-o-weak.com", 0, false);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_e2e_discovered()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	/*
	 * ,notlong.com,,0,samplebug,DNSBL_Compilation,1
	 * ,www.somedomain.com,,0,samplebug,DNSBL_Compilation,0
	 * ,somedomain.com,,0,samplebug,DNSBL_Compilation,0
	 * ,01proxy.notlong.com,,0,samplebug,DNSBL_Compilation,1
	 */

	INSERT_DOMAIN("notlong.com", 1, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 0, true);

	// have some issues with multiple domains inserted and visiting. both have
	// the same tld..
	eiter = nullptr;
	root = nullptr;
	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 0, true);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	INSERT_DOMAIN("01proxy.notlong.com", 0, false);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_insert_stronger()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	INSERT_DOMAIN("cdn.lenzmx.com", 0, true);
	INSERT_DOMAIN("lenzmx.com", 0, true);
	INSERT_DOMAIN("lenzmx.com", 1, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

static void test_e2e_discovered2()
{
	TLD_implementation_t tld_impl = create_tld_hash_impl();
	DomainTree_t **root = nullptr;
	TLD_EntryIter_t eiter = nullptr;
	DomainView_t dv;
	TestTable_t *t, *tmp;
	assert(!root_visited);

	init_DomainView(&dv);

	/*
	 * ,01proxy.notlong.com,,0,samplebug,DNSBL_Compilation,1
	 * ,notlong.com,,0,samplebug,DNSBL_Compilation,1
	 * ,www.somedomain.com,,0,samplebug,DNSBL_Compilation,0
	 * ,somedomain.com,,0,samplebug,DNSBL_Compilation,0
	 */
	INSERT_DOMAIN("01proxy.notlong.com", 1, true);

	tld_impl.impl_funcs->create_entry_iter(tld_impl.context, &eiter, &root);
	assert(*root);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("notlong.com", 1, true);

	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 1);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 0, true);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 2);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 0, true);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	INSERT_DOMAIN("www.somedomain.com", 0, false);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	INSERT_DOMAIN("somedomain.com", 0, false);

	// one domain. it is itself unique.
	visit_DomainTree(root, &test_visitor, nullptr);
	assert(HASH_COUNT(root_visited) == 3);
	FREE_VISITED;

	free_DomainView(&dv);
	//free_DomainTree(&root);
}

#undef INSERT_DOMAIN

void info_DomainTree()
{
	printf("Sizeof DomainInfo_t: %lu\n", sizeof(DomainInfo_t));
	printf("Sizeof DomainTree_t: %lu\n", sizeof(DomainTree_t));
}

void test_DomainTree()
{
	printf("Testing DomainTree..\n");
	test_duplicates();
	test_prune3();
	test_weak3();
	test_prune2();
	test_weak2();
	test_unique_weak();
	test_unique_weak2();
	test_unique_weak_strong();
	test_unique_weak_to_strong();
	test_replace_weak_w_strong();
	test_strong_over_weak();
	test_uninitialized();
	test_e2e_discovered();
	test_e2e_discovered2();
	test_insert_stronger();
	printf("Tested DomainTree.\n");
}
#endif
