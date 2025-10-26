/**
 * pfb_prune.h
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
#include "pfb_context.h"
#include "tld_context.h"

struct DomainTree;

extern char* pfb_strdup(const char *in);
extern void pfb_consolidate(TLD_implementation_t, struct pfb_out_context[static 1]);
extern void pfb_read_all(TLD_implementation_t tld_impl, pfb_contexts_t cs[static 1]);
extern void realloc_litelines(LiteLineData_t litelines[static 1]);
extern void pfb_write_carry_over(pfb_context_collect_t pcc[static 1]);
