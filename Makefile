# Destination directory for installation (intended for packagers)
DESTDIR = 
PREFIX = /usr/local

OPTFLAGS=-O3 -flto
#OPTFLAGS=-g
WARNFLAGS=-Wall -Wstrict-prototypes -Wundef
CPPFLAGS=-I.
CFLAGS=$(OPTFLAGS) $(WARNFLAGS)
LDFLAGS=$(OPTFLAGS)
LDLIBS=-lm

# Comment this out (or use "VALGRIND=" on cmdline) if you don't have valgrind.
VALGRIND=valgrind --quiet --leak-check=full --error-exitcode=5
STATS_CMD=$(VALGRIND) ./stats

all: stats

check: stats
	$(STATS_CMD) < test/test.in | diff -u - test/test.expected
	dd bs=5 if=test/test.in 2>/dev/null | $(STATS_CMD) | diff -u - test/test.expected
	$(STATS_CMD) --count < test/test.in | diff -u - test/test.count.expected
	$(STATS_CMD) --trim-outliers test/test.outliers.in | diff -u - test/test.outliers.expected
	$(STATS_CMD) --trim-outliers --count test/test.outliers.in | diff -u - test/test.outliers+count.expected
	$(STATS_CMD) --csv test/test.csv.in | diff -u - test/test.csv.expected
	$(STATS_CMD) --csv test/test.in | diff -u - test/test.base.csv.expected
	$(STATS_CMD) --skip=1 test/test.skip.in | diff -u - test/test.skip.expected
	$(STATS_CMD) --csv --count test/test.csv.in | diff -u - test/test.csv+count.expected
	$(STATS_CMD) --suppress-invariant test/test.suppress.in | diff -u - test/test.suppress.expected

install: stats
	mkdir -p -m 755 ${DESTDIR}${PREFIX}/bin
	install -m 0755 $< ${DESTDIR}${PREFIX}/bin/

CFILES=stats.c ccan/err/err.c ccan/hash/hash.c ccan/htable/htable.c ccan/list/list.c ccan/opt/helpers.c ccan/opt/opt.c ccan/opt/parse.c ccan/opt/usage.c ccan/rbuf/rbuf.c ccan/str/debug.c ccan/str/str.c ccan/tally/tally.c

OFILES=$(CFILES:.c=.o)

$(OFILES): config.h

config.h: tools/configurator
	if $< > $@.tmp; then mv $@.tmp $@; else rm -f $@.tmp; fi

stats: $(OFILES)

distclean: clean
	rm -f config.h tools/configurator
clean:
	rm -f stats $(OFILES)
