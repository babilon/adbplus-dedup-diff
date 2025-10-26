#!/bin/bash
#
# framework.sh
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

# https://flokoe.github.io/bash-hackers-wiki/commands/builtin/caller/#simple-stack-trace

function bail_if_nonzero () {
	ret=$?
	local frame=0
	while caller $frame; do
		((++frame));
	done
	if [[ $ret -ne 0 ]]; then
		echo "expected zero exit code $ret"
		exit $ret
	fi
}

function bail_if_zero () {
	ret=$?
	local frame=0
	while caller $frame; do
		((++frame));
	done
	if [[ $ret -eq 0 ]]; then
		echo "expected non-zero exit code $ret"
		exit $ret
	fi
}

function zero_differences () {
	local frame=0
	while caller $frame; do
		((++frame));
	done
	ret=$(git status -s -uno --porcelain)
	if [[ -n "$ret" ]]; then
		echo "differences found!"
		exit 1
	fi
	bail_if_nonzero $ret
}

