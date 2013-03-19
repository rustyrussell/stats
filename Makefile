PREFIX=/usr/local
OPTFLAGS=-O3 -flto
#OPTFLAGS=-g
WARNFLAGS=-Wall -Wstrict-prototypes -Wundef
CPPFLAGS=-I.
CFLAGS=$(OPTFLAGS) $(WARNFLAGS)
LDFLAGS=$(OPTFLAGS)
LDLIBS=-lm

all: stats

check: stats
	./stats < test/test.in | diff -u - test/test.expected
	dd bs=5 if=test/test.in 2>/dev/null | ./stats | diff -u - test/test.expected
	./stats --trim-outliers test/test.outliers.in | diff -u - test/test.outliers.expected
	./stats --csv test/test.csv.in | diff -u - test/test.csv.expected

install: stats
	cp $< $(PREFIX)/bin/

CFILES=stats.c ccan/err/err.c ccan/hash/hash.c ccan/htable/htable.c ccan/list/list.c ccan/opt/helpers.c ccan/opt/opt.c ccan/opt/parse.c ccan/opt/usage.c ccan/rbuf/rbuf.c ccan/str/debug.c ccan/str/str.c

OFILES=$(CFILES:.c=.o)

$(OFILES): config.h

config.h: tools/configurator
	if $< > $@.tmp; then mv $@.tmp $@; else rm -f $@.tmp; fi

stats: $(OFILES)

distclean: clean
	rm -f config.h tools/configurator
clean:
	rm -f stats $(OFILES)
