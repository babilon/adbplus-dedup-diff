/**
 * tld_context.h
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

struct SubdomainView;

// this might be an index into an array for the description and other meta data
// that might be beneficial for expansion.
typedef uint TLD_type;
typedef void* TLD_context_t;
typedef void* TLD_EntryIter_t;

// used in insert_DomainTree()
typedef struct DomainTree** (*tld_impl_context_sdv_cb)(TLD_context_t,
		struct SubdomainView);

typedef void (*tld_impl_context_cb)(TLD_context_t);
typedef struct DomainTree** (*tld_impl_entryitr_entry_cb)(TLD_EntryIter_t);

typedef void (*tld_impl_context_entryiter_dt_cb)(TLD_context_t,
		TLD_EntryIter_t[static 1], struct DomainTree**[static 1]);

typedef void (*tld_impl_context_ptr_cb)(TLD_context_t[static 1]);
typedef void (*tld_entryiter_cb)(TLD_EntryIter_t[static 1]);

typedef struct TLD_func_table
{
	tld_impl_context_sdv_cb insert_dt_entry_for_tld;
	tld_impl_context_cb sort_domain_entries;
	tld_impl_context_entryiter_dt_cb create_entry_iter;
	tld_impl_entryitr_entry_cb next_used_tld_entry;
	tld_entryiter_cb free_entry_iter;
	tld_impl_context_ptr_cb free_tld_impl_context;
} TLD_func_table_t;

extern const TLD_func_table_t all_impls[];

typedef struct TLD_implementation
{
	TLD_context_t context;
	TLD_func_table_t const *const impl_funcs;
} TLD_implementation_t;

void free_tld_impl(TLD_implementation_t tld_impl[static 1]);
