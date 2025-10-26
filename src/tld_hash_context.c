/**
 * tld_hash_context.c
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
#include "tld_context.h"
#include "tld_hash_context.h"
#include "uthash.h"
#include "domaintree.h"
#include "domain.h"

typedef struct TLD_context_impl
{
	struct TLD_entry_impl *root;
} TLD_context_impl_t;

typedef struct TLD_entry_impl
{
	UT_hash_handle hh;
	/**
	 * The uthash table for the subdomains of this TLD.
	 */
	struct DomainTree *child;
	/**
	 * length in characters of the tld array.
	 */
	size_len_t len;
	/**
	 * Holds the TLD.
	 */
	char tld[];
} TLD_entry_impl_t;

TLD_implementation_t create_tld_hash_impl()
{
	TLD_context_impl_t *hc = calloc(1, sizeof(TLD_context_impl_t));
	CHECK_MALLOC(hc);
	TLD_implementation_t impl = {
		hc,
		&all_impls[hash_impl_type],
	};
	return impl;
}

typedef struct TLD_entryiter_impl
{
	TLD_entry_impl_t *root;
} TLD_entryiter_impl_t;

void hash_context_create_entry_iter(TLD_context_t c,
		TLD_EntryIter_t iter[static 1], DomainTree_t **dt[static 1])
{
	ASSERT(c);
	ASSERT(iter);
	ASSERT(*iter == nullptr);
	ASSERT(dt);
	ASSERT(*dt == nullptr);
	TLD_entryiter_impl_t *entryiter = malloc(sizeof(TLD_entryiter_impl_t));
	CHECK_MALLOC(entryiter);
	TLD_context_impl_t *impl = (TLD_context_impl_t*)c;

	ASSERT(impl->root); // TODO handle empty tree scenario; this is nil.
	entryiter->root = impl->root;
	ASSERT(entryiter->root);

	*iter = entryiter;
	*dt = &entryiter->root->child;
}

void hash_context_free_entry_iter(TLD_EntryIter_t iter[static 1])
{
	TLD_entryiter_impl_t *entryiter = (TLD_entryiter_impl_t*)*iter;
	free(entryiter);
	*iter = nullptr;
}

/**
 * Returns the TLD abstract container that has the DomainTree* that will
 * contain the rest of the domain. insert_Domain is fed the child of the
 * returned entry. DomainView_t is at 'google' by this time in ads.google.com.
 */
DomainTree_t** hash_context_next_tld_entry(TLD_EntryIter_t entryiter)
{
	ASSERT(entryiter);
	TLD_entryiter_impl_t *h_entryiter = (TLD_entryiter_impl_t*)entryiter;

	h_entryiter->root = h_entryiter->root->hh.next;
	if(h_entryiter->root)
	{
		return &h_entryiter->root->child;
	}

	return nullptr;
}

static int sort_TLD_entry_by_tld(TLD_entry_impl_t *a, TLD_entry_impl_t *b)
{
	int first_n = memcmp(a->tld, b->tld, MIN(a->len, b->len));
	int ret;
	if(first_n == 0)
		ret = a->len - b->len;
	else
		ret = first_n;
	return ret;
}

DomainTree_t** hash_context_insert_tld(TLD_context_t ic, SubdomainView_t sdv)
{
	ASSERT(ic);
	TLD_context_impl_t *c = (TLD_context_impl_t*)ic;

	TLD_entry_impl_t *entry = nullptr;
	HASH_FIND(hh, c->root, sdv.data, sdv.len, entry);

	if(!entry)
	{
		TLD_entry_impl_t *ntld_entry = calloc(1, sizeof(TLD_entry_impl_t)
				+ sizeof(char) * sdv.len);
		CHECK_MALLOC(ntld_entry);
		memcpy(ntld_entry->tld, sdv.data, sdv.len);
		ntld_entry->len = sdv.len;
		// create entry here with the data..
		HASH_ADD_KEYPTR(hh, c->root, ntld_entry->tld, sdv.len, ntld_entry);
		ASSERT(ntld_entry->child == nullptr);
		return &ntld_entry->child;
	}

	return &entry->child;
}

void hash_context_sort_entries(TLD_context_t context)
{
	TLD_context_impl_t *h_context = (TLD_context_impl_t*)context;

	HASH_SRT(hh, h_context->root, sort_TLD_entry_by_tld);
}

void hash_context_free_context(TLD_context_t c[static 1])
{
	ASSERT(c);
	TLD_context_impl_t *hc = *c;
	ASSERT(hc);

	TLD_entry_impl_t *current = nullptr, *tmp = nullptr;
	HASH_ITER(hh, hc->root, current, tmp)
	{
		// expect the DomainTree_t held in 'child' to have been destroyed and
		// cleared prior to the destruction of the top level.
		ASSERT(hc->root->child == nullptr);
		HASH_DEL(hc->root, current);
		free(current);
	}

	free(hc);
	*c = nullptr;
}
