/**
 * pfb_differ.h
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

extern void diff_adbplus_adlists_FILE(pfb_context_t pcc_A[static 1],
		const LiteLineData_t litelines_A[static 1],
		pfb_context_t pcc_B[static 1], const LiteLineData_t litelines_B[static 1],
		pfb_out_context_t out_context[static 1]);

extern void diff_adbplus_adlists_BUFFER(pfb_out_buffer_t pcc_A[static 1],
		pfb_out_buffer_t pcc_B[static 1], pfb_out_context_t out_context[static 1]);

