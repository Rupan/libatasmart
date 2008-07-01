CFLAGS=-pipe -Wall -W -O0 -g -I.
LIBS=

all: skdump sktest

skdump: atasmart.o skdump.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

sktest: atasmart.o sktest.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

clean:
	rm -f skdump sktest *.o
