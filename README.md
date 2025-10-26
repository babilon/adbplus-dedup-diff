# adbplus dedup diff

A solution to deduplicate and compute the difference of Adblock Plus syntax
block lists.

Default behavior when given two or more arguments is to compute the difference
between the two sets of inputs and write to stdout the computed difference:

./bin/main.real a.adlist b.adlist

Specify -o and a filename to write the difference to the specified file:

./bin/main.real a.adlist b.adlist -o a_vs_b.diff

Deduplicate and sort a set of inputs with -D:

./bin/main.real -D a.adlist b.adlist c.adlist -o combined_sorted.adlist
