/**
 * carry_over.h
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

typedef struct line_info
{
	linenumber_t offset;
	line_len_t line_len;
} line_info_t;

typedef struct carry_over
{
	size_len_t alloc;
	size_len_t used;
	struct line_info *li;
} carry_over_t;

extern void insert_carry_over(carry_over_t co[static 1], line_info_t li);
extern void init_carry_over(carry_over_t co[static 1]);
extern void free_carry_over(carry_over_t co[static 1]);
