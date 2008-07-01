CFLAGS=-pipe -Wall -W -O0 -g -I.
LIBS=

all: skdump sktest smartkitd gnome-disk-health gnome-disk-health.ui

skdump: smart.o skdump.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

sktest: smart.o sktest.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

smartkitd: smart.c smartkitd.vala
	valac --save-temps -g -o $@ --vapidir=. --pkg=smart --pkg=hal --pkg=dbus-glib-1 --Xcc=-I. $^

gnome-disk-health: gnome-disk-health.vala
	valac --save-temps -g -o $@ --pkg=gtk+-2.0 --pkg=dbus-glib-1 $^

gnome-disk-health.ui: gnome-disk-health.glade
	gtk-builder-convert $< $@

clean:
	rm -f skdump sktest *.o smartkitd gnome-disk-health gnome-disk-health.ui 
