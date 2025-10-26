include Makefile

BIGTEST := -o .bigin tests/001_inputs/bcan_anudeepND_ads.fat tests/001_inputs/EasyList_France.fat tests/001_inputs/ccan_StevenBlack_hosts.fat tests/001_inputs/acan_hagezi_pro_plus.fat
SMLTEST := -o .smallin tests/001_inputs/bcan_anudeepND_ads.fat
TEST001 := -o .t01 -x .fat -d tests/001_inputs/
TEST003 := -o .t03 -x .fat -d tests/003_inputs/
SORT001 := -o .txt -x .fat -d tests/001_inputs/

VALGRIND := valgrind
LEAKARGS := -s --track-origins=yes --tool=memcheck --leak-check=full --show-leak-kinds=all

valgrind: main
	@$(VALGRIND) $(LEAKARGS) ./${BINDIR}/$<.real $(SMLTEST)

valgrindbig: main
	@$(VALGRIND) $(LEAKARGS) ./${BINDIR}/$<.real $(BIGTEST)

valgrindtest: test
	@$(VALGRIND) $(LEAKARGS) ./${BINDIR}/$<.real -t -i 10 -r 5

cachegrindbig: main
	@$(VALGRIND) --tool=cachegrind --log-file=cachegrindbig.log ./${BINDIR}/$<.real $(BIGTEST)

callgrindbig: main
	@$(VALGRIND) --tool=callgrind --log-file=callgrindbig.log ./${BINDIR}/$<.real $(BIGTEST)

mainbig: main
	time ./${BINDIR}/$<.real $(BIGTEST)

test001: main
	time ./$(BINDIR)/$<.real $(TEST001)

test003: main
	time ./$(BINDIR)/$<.real $(TEST003)

realbig: release
	time ./${BINDIR}/$<.real $(BIGTEST)

sort001: release
	time ./$(BINDIR)/$<.real $(SORT001)
	./prune_and_compare.sh
