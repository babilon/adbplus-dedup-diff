/**
 * test.c
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
#include <stdio.h>
#include "pfb_context.h"
#include "rw_pfb_csv.h"
#include "pfb_prune.h"
#include "test.h"

static void do_test_end2end(const int argc, char *const *argv_i);

/**
 * Full end to end test with an empty output file b/c all of its inputs are
 * dropped during the building of the tree.
 */
static void test_end2end()
{
	char *const argv_i[] = {"tests/unit_pfb_prune/E2ETestInput_1.txt",
							"tests/unit_pfb_prune/E2ETestInput_2.txt",
							"tests/unit_pfb_prune/E2ETestInput_3.txt"};

	do_test_end2end(3, argv_i);
}

/**
 * A file with lines and a file with zero lines.
 */
static void test_end2end_empty()
{
	char *const argv_i[] = {"tests/unit_pfb_prune/E2ETestInput_1.txt",
							"tests/unit_pfb_prune/E2ETest_Empty.txt"};

	do_test_end2end(2, argv_i);
}

/**
 * Files with varying amount of lines with regexes.
 *
 * Run ./test.real -t -i 10 for maximum effect.
 * -i for initial allocation. This is exercising the regex lines carried over
 * during the consolidation step.
 * -r is optional.
 */
static void test_carry_over_end2end()
{
							// exactly 10 lines in all
	char *const argv_i[] = {"tests/unit_pfb_prune/E2ETestRegexInput_1.txt",
							// 8 non-regex, 3 regex: allocate one more
							"tests/unit_pfb_prune/E2ETestRegexInput_2.txt",
							// space remaining for more: 5 lines
							"tests/unit_pfb_prune/E2ETestRegexInput_3.txt",
							// empty file
							"tests/unit_pfb_prune/E2ETest_Empty.txt",
							// 10 regular, 1 regex
							"tests/unit_pfb_prune/E2ETestRegexInput_4.txt",
							// several
							"tests/unit_pfb_prune/E2ETestRegexInput_5.txt"
	};

	do_test_end2end(6, argv_i);
}

static void do_test_end2end(const int argc, char *const */*argv_i*/)
{
	bool use_shared_buffer = true;
	// TODO
	UNUSED(use_shared_buffer);

	//const char *out_ext = ".fulle2e";
	//const size_t alloc_contexts = argc;
	pfb_contexts_t contexts; //= pfb_init_contexts(alloc_contexts, out_ext, argv_i);

	//assert(contexts.begin_context->dt);
	//assert(!contexts.begin_context->dt[0]);

	for(pfb_context_t *c = contexts.begin_context; c != contexts.end_context; c++)
	{
		// every context references the same dt. only have to null the place holder
		// for one of the contexts.
		//assert(c->dt == contexts.begin_context->dt);
		//assert(!c->dt[0]);

	}

	// perhaps iterate over all input arguments create context for each one and
	// open the files to verify all files can be read. and open output files to
	// verify those can be written. then begin the work of things in a separate
	// loop. maybe this consolidates clean up. can use hash or an index into an
	// array.
	//assert(contexts.begin_context->dt);
	//assert(contexts.begin_context->dt[0] == nullptr);

	//pfb_read_csv(&contexts);

	// confirm that reading didn't bork a pointer
	//assert(contexts.begin_context->dt);
	//assert(contexts.begin_context->dt[0]);

	for(pfb_context_t *c = contexts.begin_context; c != contexts.end_context; c++)
	{
		//assert(c->dt == contexts.begin_context->dt);
		//assert(c->dt[0] == contexts.begin_context->dt[0]);
	}

	//ArrayDomainInfo_t array_di;
	//init_ArrayDomainInfo(&array_di, pfb_len_contexts(&contexts));
	//array_di.begin_pfb_context = contexts.begin_context;

	for(int i = 0; i < argc; i++)
	{
		//printf("alloc contextdomain=[%d]=%lu\n", i, (size_t)array_di.cd[i].alloc_linenumbers);
	}


	//pfb_consolidate(contexts.begin_context->dt, &array_di);
	//assert(contexts.begin_context->dt);
	//assert(!contexts.begin_context->dt[0]);

	//pfb_write_csv(&contexts, &array_di, use_shared_buffer);

	pfb_free_contexts(&contexts);
	//free_ArrayDomainInfo(&array_di);

	assert(!contexts.begin_context);
	assert(!contexts.end_context);
	//assert(!array_di.begin_pfb_context);
}

void run_tests()
{
	printf("Running tests...");
	test_domain();
	test_DomainTree();
	test_rw_pfb_csv();
	//test_pfb_prune();
	test_end2end();
	test_end2end_empty();
	//test_input_args();
	test_carry_over();
	test_carry_over_end2end();
	printf("OK.\n");

	printf("Printing info of structs...\n");
	info_domain();
	info_DomainTree();
	//info_pfb_prune();
	printf("OK.\n");
}
