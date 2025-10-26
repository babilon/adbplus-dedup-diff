#!/bin/bash

scan-build \
	-analyze-headers \
    -enable-checker nullability.NullableDereferenced \
    -enable-checker nullability.NullablePassedToNonnull \
    -enable-checker nullability.NullableReturnedFromNonnull \
    -enable-checker optin.core.EnumCastOutOfRange \
    -enable-checker optin.taint.TaintedAlloc \
	-o scan-build \
	make -f Makefile main
