/**
 * paths_list.h
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
#include <unistd.h>

/**
 * Contains information from a struct stat useful to identify a path.
 */
typedef struct pfb_stat
{
	__off_t file_size;
	__dev_t st_dev;
	__ino_t st_ino;
} pfb_stat_t;

/**
 * Details of output for the program.
 */
typedef struct const_path_info
{
	const char *const path;
	pfb_stat_t pfb_s;
} const_path_info_t;

/**
 * Details of an input to the program.
 */
typedef struct path_info
{
	// this can be set by the user or via a rationality check early on.
	// when true, will attempt to allocate a buffer equal to the size of the
	// file and read the entire file into memory.
	// when false, will read the file in chunks.
	bool use_mem_buffer;
	char *path;
	pfb_stat_t pfb_s;
} path_info_t;

/**
 * Contains a list of path_info.
 */
typedef struct paths_list
{
	path_info_t *paths;
	uint len;
	uint alloced;
} paths_list_t;

