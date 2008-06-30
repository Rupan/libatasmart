/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
    This file is part of SmartKit.

    Copyright 2008 Lennart Poettering

    SmartKit is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 2.1 of the
    License, or (at your option) any later version.

    SmartKit is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with SmartKit. If not, If not, see
    <http://www.gnu.org/licenses/>.
***/

using DBus;
using GLib;
using Gtk;

public class DiskHealth : Gtk.Builder {

        string uifile = "gnome-disk-health.ui";

        public bool create_widgets(string disk_string) {
                try {
                        add_from_file (uifile);
                        Gtk.Widget window = (Gtk.Widget) get_object("DiskHealthDialog");

                        if (!fill_in_data(disk_string))
                                return false;

                        window.show_all();
                        window.destroy += Gtk.main_quit;
                } catch (GLib.Error e) {
                        stderr.printf("Failed to create main window: %s\n", e.message);
                        return false;
                }

                return true;
        }

        private DBus.Connection connection;
        private dynamic DBus.Object manager;
        private DBus.ObjectPath dbus_path;
        private dynamic DBus.Object disk;

        public bool fill_in_data(string disk_string) {

                try {
                        connection = DBus.Bus.get(DBus.BusType.SYSTEM);

                        manager = connection.get_object("net.poettering.SmartKit", "/", "net.poettering.SmartKit.Manager");

                        DBus.ObjectPath p;

                        try {
                                p = manager.getDiskByPath(disk_string);
                        } catch (DBus.Error e) {
                                try {
                                        p = manager.getDiskByUDI(disk_string);
                                } catch (DBus.Error e) {
                                        return false;
                                }
                        }

                        stderr.printf("Using D-Bus path %s\n", p);

                        disk = connection.get_object("net.poettering.SmartKit", p, "net.poettering.SmartKit.Disk");

                        ((Gtk.Label) get_object("pathLabel")).set_label(disk.getPath());
                        ((Gtk.Label) get_object("sizeLabel")).set_label(pretty_size(disk.getSize()));

                        bool b = disk.isIdentifyAvailable();

                        if (b) {
                                ((Gtk.Label) get_object("modelLabel")).set_label(disk.getIdentifyModel());
                                ((Gtk.Label) get_object("serialLabel")).set_label(disk.getIdentifySerial());
                                ((Gtk.Label) get_object("firmwareLabel")).set_label(disk.getIdentifyFirmware());
                        } else {
                                ((Gtk.Label) get_object("modelLabel")).set_label("n/a");
                                ((Gtk.Label) get_object("serialLabel")).set_label("n/a");
                                ((Gtk.Label) get_object("firmwareLabel")).set_label("n/a");
                        }

                        if (b)
                                b = disk.isSmartAvailable();

                        if (b) {
                                ((Gtk.Label) get_object("smartLabel")).set_label("Disk health functionality (S.M.A.R.T.) is available.");
                        } else {
                                ((Gtk.Label) get_object("smartLabel")).set_markup("Disk health functionality (S.M.A.R.T.) is <b>not</b> available.");
                                ((Gtk.Label) get_object("healthLabel")).set_label("");
                                ((Gtk.Label) get_object("badSectorsLabel")).set_label("");
                                ((Gtk.Label) get_object("temperatureLabel")).set_label("");
                        }

                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                        return false;
                }

                return true;
        }

        public static string pretty_size(uint64 size) {

                if (size >= (uint64)1024*(uint64)1024*(uint64)1024*(uint64)1024)
                        return "%0.1f TiB".printf((double) size/1024/1024/1024/1024);
                else if (size >= (uint64)1024*(uint64)1024*(uint64)1024)
                        return "%0.1f GiB".printf((double) size/1024/1024/1024);
                else if (size >= (uint64)1024*(uint64)1024)
                        return "%0.1f MiB".printf((double) size/1024/1024);
                else if (size >= (uint64)1024)
                        return "%0.1f KiB".printf((double) size/1024);
                else
                        return "%u B".printf((uint) size);
        }


        public static int main (string[] args) {
                Gtk.init(ref args);

                var dh  = new DiskHealth();

                if (dh.create_widgets(args[1]))
                        Gtk.main ();

                return 0;
        }
}
