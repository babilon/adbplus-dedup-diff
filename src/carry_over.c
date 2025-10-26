#include "carry_over.h"
#include <string.h>
#include <stdlib.h>

/**
 * Initialize the given carry_over_t to a default state.
 * Calls to free_carry_over() are safe after calling this.
 */
void init_carry_over(carry_over_t co[static 1])
{
	ASSERT(co);
	co->alloc = 0;
	co->used = 0;
	co->li = nullptr;
}

/**
 * Release memory allocated by calls to alloc_carry_over().
 */
void free_carry_over(carry_over_t co[static 1])
{
	ASSERT(co);
	free(co->li);
	co->li = nullptr;
	co->used = 0;
	co->alloc = 0;
}

/**
 * Appends to the end of the internal array the given linenumber_t.
 *
 * This is safe to call after init_carry_over().
 */
void insert_carry_over(carry_over_t co[static 1], line_info_t li)
{
	ASSERT(co);

	if(co->used == co->alloc)
	{
		if(co->alloc == 0)
		{
			co->alloc = 10;
			co->li = malloc(sizeof(line_info_t) * co->alloc);
			CHECK_MALLOC(co->li);
		}
		else
		{
			CHECK_REALLOC(co->li, sizeof(line_info_t) * (co->alloc + 3));
			co->alloc += 3;
		}
	}

	co->li[co->used++] = li;
}

#ifdef BUILD_TESTS
static void test_init_carry_over()
{
	carry_over_t co, co_zero;
	memset(&co, 0xf, sizeof(carry_over_t));
	memset(&co_zero, 0, sizeof(carry_over_t));

	// legal to call free on a zero'd carry_over_t
	free_carry_over(&co_zero);

	init_carry_over(&co);

	assert(0 == memcmp(&co, &co_zero, sizeof(carry_over_t)));

	free_carry_over(&co);

	assert(0 == memcmp(&co, &co_zero, sizeof(carry_over_t)));

	// legal to call free on a free'd carry_over_t
	free_carry_over(&co);
}

static void test_len_carry_over()
{
	carry_over_t co;
	init_carry_over(&co);
	assert(0 == co.used);

	line_info_t li = {.offset=33, .line_len=10};
	insert_carry_over(&co, li);

	assert(1 == co.used);

	free_carry_over(&co);

	assert(0 == co.used);
}

static void test_insert_carry_over()
{
	carry_over_t co;
	init_carry_over(&co);

	line_info_t li1 = {.offset=3, .line_len=10};
	insert_carry_over(&co, li1);
	line_info_t li2 = {.offset=33, .line_len=10};
	insert_carry_over(&co, li2);
	line_info_t li3 = {.offset=2, .line_len=10};
	insert_carry_over(&co, li3);
	line_info_t li4 = {.offset=22, .line_len=10};
	insert_carry_over(&co, li4);

	assert(4 == co.used);

	// these numbers probably wrong
	assert(co.li[0].offset == 4);
	assert(co.li[1].offset == 3);
	assert(co.li[2].offset == 33);
	assert(co.li[3].offset == 2);
	assert(co.li[4].offset == 22);

	free_carry_over(&co);

	assert(0 == co.used);
}

void test_transfer()
{
	carry_over_t co;
	init_carry_over(&co);

	line_info_t li1 = {.offset=101, .line_len=10};
	insert_carry_over(&co, li1);
	line_info_t li2 = {.offset=202, .line_len=10};
	insert_carry_over(&co, li2);
	line_info_t li3 = {.offset=303, .line_len=10};
	insert_carry_over(&co, li3);
	line_info_t li4 = {.offset=404, .line_len=10};
	insert_carry_over(&co, li4);
	line_info_t li5 = {.offset=505, .line_len=10};
	insert_carry_over(&co, li5);

	const linenumber_t count = co.used;
	assert(count == 5);
	linenumber_t *xfered = calloc(count, sizeof(linenumber_t));
	// TODO this was deleted.
	//transfer_linenumbers(xfered, &co);

	free_carry_over(&co);

	assert(co.used == 0);

	for(long i = 0; i < count; i++)
	{
		assert(xfered[i] == ((i + 1) * 101));
	}

	free(xfered);
}

void test_carry_over()
{
	test_init_carry_over();
	test_len_carry_over();
	test_insert_carry_over();
	test_transfer();
}
#endif
