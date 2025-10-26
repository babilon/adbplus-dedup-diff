#!/bin/bash
#
# run_testsuite.sh
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

zero_differences

make clean
bail_if_nonzero

TARGET="${TARGET:-main}"

make $TARGET
bail_if_nonzero

BIN=./bin/${TARGET}.real

suite=$1
if [[ -z "$suite" ]]; then
	echo "run all"
else
	echo "run $suite"
	source "${suite}"
	echo "SUCCESS! : ${suite}"
	exit 0
fi

source failures.sh

# stdout includes prints. put those to stderr and pipe stderr to /dev/null
#${BIN} -D samples/pro.txt > samples/pro.out
#zero_differences

source basics.sh

source bigger.sh

echo "SUCCESSFULLY COMPLETED ${TARGET}!"
