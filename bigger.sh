#!/bin/bash
#
# bigger.sh
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
	echo "run: testsuite.sh bigger.sh"
	exit 42
fi

${BIN} -D samples/pro.txt -o samples/pro.out
bail_if_nonzero
zero_differences

${BIN} -D -M samples/pro.txt -o samples/pro.out
bail_if_nonzero
zero_differences

${BIN} -D samples/19319e73-1a4e-4c84-8202-fc96329a33bc.adlist -o samples/19319e73-1a4e-4c84-8202-fc96329a33bc.out
bail_if_nonzero
zero_differences

${BIN} samples/pro.txt samples/19319e73-1a4e-4c84-8202-fc96329a33bc.adlist -o bigdiff.diff
bail_if_nonzero
zero_differences

${BIN} -M samples/pro.txt samples/19319e73-1a4e-4c84-8202-fc96329a33bc.adlist -o bigdiff.diff
bail_if_nonzero
zero_differences

${BIN} -D samples/f54a20c1-bb7a-48c1-ac1a-f58a1dcf0cab.adlist -o samples/f54a20c1-bb7a-48c1-ac1a-f58a1dcf0cab.out
bail_if_nonzero
zero_differences

${BIN} -D samples/oisd.big.adlist -o samples/oisd.out
bail_if_nonzero
zero_differences

${BIN} samples/oisd.big.adlist samples/hagezi.pro.adlist -o samples/oisd.big-vs-hagezi.pro.diff
bail_if_nonzero
zero_differences
