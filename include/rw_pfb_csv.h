/**
 * rw_pfb_csv.h
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
#include "pfb_context.h"

struct ContextPair;

typedef struct PortLineData
{
	// null terminated string
	char const *data;
	// length of the null terminated string to avoid strlen(x) calculations
	line_info_t li;
} PortLineData_t;

extern size_t default_buffer_len();
extern size_t get_max_line_len();

extern void pfb_read_one_context(struct pfb_context[static 1],
		void(*do_stuff)(PortLineData_t const *const plv, struct pfb_context*,
			void*), void *data);
