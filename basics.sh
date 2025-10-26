#!/bin/bash
#
# basics.sh
#
# Part of pfb_adbplus_dedup_diff
#
# Copyright (c) 2025 robert.babilon@gmail.com
# All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

source framework.sh

if [ -z ${BIN+x} ];then
	echo "run: testsuite.sh basics.sh"
	exit 42
fi

#
# SUCCESS SCENARIOS
#
# program expected to exit 0 and produce the expected output.
#
${BIN} -D samples/a.txt -o samples/a.out
bail_if_nonzero
zero_differences

${BIN} -s -D samples/a.txt > samples/a.out
bail_if_nonzero
zero_differences

${BIN} -D samples/b.txt -o samples/b.out
bail_if_nonzero
zero_differences

${BIN} samples/a.txt samples/b.txt -o firstdiff.diff
bail_if_nonzero
zero_differences

