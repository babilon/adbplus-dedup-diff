#!/bin/bash

\mkdir lcov-output/
\lcov --capture --directory obj/codecov/src/ --output-file lcov-output/lcov-report-coverage.info
\genhtml lcov-output/lcov-report-coverage.info --output-directory lcov-output/lcov-report-html
