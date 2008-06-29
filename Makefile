CFLAGS=`pkg-config --cflags glib-2.0` -pipe -Wall -W -O0 -g
LIBS=`pkg-config --libs glib-2.0`

skdump: smart.o skdump.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
