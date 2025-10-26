#pragma once
#include "tld_context.h"

static constexpr const TLD_type hash_impl_type = 0x00;
static const char *const hash_impl_desc = "Using a UTHash for the TLD with standard UTHash search algorithms.";

extern TLD_implementation_t create_tld_hash_impl();

extern void hash_context_sort_entries(TLD_context_t);
extern struct DomainTree** hash_context_insert_tld(TLD_context_t, struct SubdomainView);
extern struct DomainTree** hash_context_next_tld_entry(TLD_EntryIter_t);
extern void hash_context_free_context(TLD_context_t c[static 1]);

extern void hash_context_create_entry_iter(TLD_context_t c,
		TLD_EntryIter_t iter[static 1], struct DomainTree **dt[static 1]);
extern void hash_context_free_entry_iter(TLD_EntryIter_t iter[static 1]);
