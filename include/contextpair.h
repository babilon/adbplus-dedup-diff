/**
 * contextpair.h
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

typedef struct ContextPair
{
	const struct TLD_implementation *tld_impl;

	/**
	 * Transitional payload during pfb_insert(). Carries CsvLineView data
	 * through insert_DomainTree() for insertion (if unique) into the
	 * DomainTree. Initialized once. Free'ed in pfb_close_context().
	 */
	struct DomainView *dv;
} ContextPair_t;
