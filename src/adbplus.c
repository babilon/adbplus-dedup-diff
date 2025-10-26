/**
 * adbplus.c
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
#include "dedupdomains.h"
#include "adbplusline.h"

/**
 * Initial implementation will take short cut: if any line is bogus then the
 * line is tossed. first line can have [ and ]
 * next lines can be !
 * and after a line starts with something other than ! it has to be || and end ^
 * and the rest must be || .. ^
 */
bool parse_adbplus_line(AdbplusView_t lv[static 1], const char input_line[static 1])
{
	// parse the domain out of a line formatted in adbplus style.
	// need to consider and handle comments and the header stuff starting with [
	// need to consider adbplus files containing non-domain entries.
	// only lines || and ending with ^ contain a domain.
	// the comments begin with ! and can carry over
	// lines for header beginning with [ can also carry over

	ASSERT(lv);
	ASSERT(input_line);

	// this function has a lot of work to validate format.
	// if adbplus syntax, then ! are comments. # are trash.
	// [ is valid for the header first line
	// ! comments after the initial block are also as good as trash since we're
	// sorting.

	const char *c = input_line;
	const char *prev = input_line;
	lv->ms = MATCH_BOGUS;

	switch(*c)
	{
		case '\0':
			return false;
		case '!':
			lv->ms = MATCH_COMMENT;
			break;
		case '[':
			lv->ms = MATCH_HEADER;
			break;
		case '|':
			if(*(++c) == '|')
			{
				prev = ++c;
				lv->ms = MATCH_POSSIBLE;
			}
			break;
		// TODO support # comments
		//case '#':
			//lv->ms = MATCH_COMMENT;
			//break;
		default:
#if 0
			if((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))
			{
				printf("it is within %c\n", *c);
			// assume is a raw domain
			prev = c;
			lv->ms = MATCH_WEAK;
			}
#endif
			lv->ms = MATCH_BOGUS;
			return false;
	}

	// zip to the end of the string
	while(*c && *c != LINE_TERMINAL)
	{
		c++;
	}

	const char peak = *(c - 1);
	ASSERT(peak != '\n'); // encountered this with the in buffer run
	ASSERT(peak != '\r'); // might have this if the original had \r

	// suppose input is ||^
	// || ^
	if(lv->ms == MATCH_COMMENT || lv->ms == MATCH_HEADER)
	{
		return true;
	}

	if(lv->ms == MATCH_POSSIBLE && peak == '^')
	{
		lv->data = prev;
		ASSERT(c - prev > 0);
		lv->len = c - prev - 1;
		lv->ms = MATCH_FULL;
		return true;
	}

	lv->ms = MATCH_BOGUS;
	return false;
}
