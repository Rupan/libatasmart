CFLAGS=-pipe -Wall -W -O0 -g -I.
LIBS=

all: skdump sktest

skdump: smart.o skdump.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

sktest: smart.o sktest.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f skdump sktest *.o
