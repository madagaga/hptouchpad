CFLAGS ?= -O2 -W -Wall
OBJS = main.o rtsp.o rtp.o capture.o encoder.o audio.o

tp-rtsp: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS) -lasound

$(OBJS): tp-rtsp.h

clean:
	rm -f tp-rtsp *.o
