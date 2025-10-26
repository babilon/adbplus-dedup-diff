/**
 * domaintree.h
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
#include "uthash.h"

typedef struct DomainTree
{
	// domain segments are at most 63 bytes
	subdomain_len_t len;

	struct DomainInfo *di;
	struct DomainTree *child;
	UT_hash_handle hh;

	// malloc'ed string for tld. not null terminated.
	char tld[];
} DomainTree_t;

struct DomainView;
struct TLD_implementation;
extern void insert_DomainTree(const struct TLD_implementation tld_impl,
		struct DomainView *dv);

extern void free_DomainTree(DomainTree_t **root);
extern void free_DomainTreePtr(DomainTree_t **dt);

extern void transfer_DomainInfo(DomainTree_t **root,
		void(*collector)(struct DomainInfo **di, void *context), void *context);

extern void visit_DomainTree(DomainTree_t **root,
		void(*visitor_func)(struct DomainInfo **di, void *context),
		void *context);
