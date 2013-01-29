CCANDIR=/home/rusty/devel/cvs/ccan/
#CFLAGS=-O3 -Wall -flto -I$(CCANDIR)
CFLAGS=-g -Wall -I$(CCANDIR)
LDFLAGS=-O3 -flto

all: stats

check: stats
	./stats < test.in | cmp -s test.expected
	dd bs=5 if=test.in 2>/dev/null | ./stats | cmp -s test.expected
	./stats --trim-outliers test.outliers.in | cmp -s test.outliers.expected
	./stats --csv test.csv.in | cmp -s test.csv.expected

stats: stats.o err.o opt.o opt_parse.o opt_usage.o opt_helpers.o list.o rbuf.o htable.o hash.o str.o

err.o: $(CCANDIR)/ccan/err/err.c
	$(CC) $(CFLAGS) -c -o $@ $<
str.o: $(CCANDIR)/ccan/str/str.c
	$(CC) $(CFLAGS) -c -o $@ $<
list.o: $(CCANDIR)/ccan/list/list.c
	$(CC) $(CFLAGS) -c -o $@ $<
opt.o: $(CCANDIR)/ccan/opt/opt.c
	$(CC) $(CFLAGS) -c -o $@ $<
opt_helpers.o: $(CCANDIR)/ccan/opt/helpers.c
	$(CC) $(CFLAGS) -c -o $@ $<
opt_parse.o: $(CCANDIR)/ccan/opt/parse.c
	$(CC) $(CFLAGS) -c -o $@ $<
opt_usage.o: $(CCANDIR)/ccan/opt/usage.c
	$(CC) $(CFLAGS) -c -o $@ $<
rbuf.o: $(CCANDIR)/ccan/rbuf/rbuf.c
	$(CC) $(CFLAGS) -c -o $@ $<
htable.o: $(CCANDIR)/ccan/htable/htable.c
	$(CC) $(CFLAGS) -c -o $@ $<
hash.o: $(CCANDIR)/ccan/hash/hash.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f stats *.o
