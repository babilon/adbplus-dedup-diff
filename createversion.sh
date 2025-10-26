#!/bin/sh

ref=$(git log --pretty="format:%h" -1)
sed -E "s/\<NOREF\>/${ref}/g" include/version.h > include/version.nogit.h
