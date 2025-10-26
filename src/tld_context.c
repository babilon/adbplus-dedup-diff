/**
 * tld_context.c
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

const TLD_func_table_t all_impls[] = {
	{
		hash_context_insert_tld,
		hash_context_sort_entries,
		hash_context_create_entry_iter,
		hash_context_next_tld_entry,
		hash_context_free_entry_iter,
		hash_context_free_context,
	},
};

void free_tld_impl(TLD_implementation_t tld_impl[static 1])
{
	ASSERT(tld_impl);
	ASSERT(tld_impl->impl_funcs);
	ASSERT(tld_impl->context);
	tld_impl->impl_funcs->free_tld_impl_context(&tld_impl->context);
	tld_impl->context = nullptr;
}
