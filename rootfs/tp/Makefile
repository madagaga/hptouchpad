CFLAGS ?= -O2 -W -Wall
OBJS = main.o daemon.o cmd.o input.o power.o sound.o camera.o cgi.o util.o devmem.o

all: tp devmem-small

tp: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS) -lm

$(OBJS): tp.h

# devmem alone (a few KB), for the initramfs that cannot hold tp
devmem-small: devmem.c tp.h
	$(CC) $(CFLAGS) -Os -s -DDEVMEM_STANDALONE $(LDFLAGS) -o $@ devmem.c

clean:
	rm -f tp devmem-small *.o
