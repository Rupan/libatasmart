CFLAGS=`pkg-config --cflags glib-2.0` -pipe -Wall -W -O0 -g
LIBS=`pkg-config --libs glib-2.0`

smartkit: smartkit.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
