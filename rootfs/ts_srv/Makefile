CFLAGS ?= -O2 -W -Wall -funsafe-math-optimizations
LDLIBS = -lm

ts_srv: ts_srv.o digitizer.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f ts_srv *.o
