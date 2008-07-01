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


public DiskHealth disk_health;

public class DiskHealth : Gtk.Builder {

        string uifile = "gnome-disk-health.ui";

        public bool create_widgets(string disk_string) {
                test = null;

                try {
                        add_from_file (uifile);
                        Gtk.Widget window = (Gtk.Widget) get_object("DiskHealthDialog");

                        Gtk.TreeView tree_view = (Gtk.TreeView) get_object ("attributeTreeView");

                        /* id, name, text, icon, current, worst, threshold, type, verdict */
                        list_store = new Gtk.ListStore(9, typeof(uint8), typeof(string), typeof(string), typeof(string), typeof(string), typeof(string), typeof(string), typeof(string), typeof(string));

                        tree_view.set_model(list_store);

                        tree_view.insert_column_with_attributes (-1, null, new Gtk.CellRendererPixbuf(), "icon-name", 3, null);
                        tree_view.insert_column_with_attributes(-1, "Name", new Gtk.CellRendererText(), "text", 1, null);

                        Gtk.CellRenderer cell = new Gtk.CellRendererText();
                        cell.set("xalign", 1.0, null);
                        tree_view.insert_column_with_attributes (-1, "Value", cell, "text", 2, null);

                        if (!setup_connection(disk_string))
                                return false;

                        if (!fill_in_data())
                                return false;

                        setup_refresh_timer();

                        window.show_all();
                        window.destroy += Gtk.main_quit;

                        ((Gtk.Button) get_object("closeButton")).clicked += Gtk.main_quit;

                        ((Gtk.Button) get_object("refreshButton")).clicked += refresh;

                        ((Gtk.Button) get_object("selfTestButton")).clicked += selfTest;

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
        private Gtk.ListStore list_store;
        private string test;

        public bool setup_connection(string disk_string) {
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
                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                        return false;
                }

                return true;
        }

        public void refresh() {
                try {
                        disk.readSmartData(true);
                        fill_in_data();
                        setup_refresh_timer();
                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                }
        }

        public uint refresh_time;
        public uint refresh_timer = 0;

        public void setup_refresh_timer() {

                uint t;

                if (refresh_time <= 0)
                        t = 5*60;
                else if (refresh_time == 1)
                        t = 5; /* if 1 min is request, we do 10s
                                * instead, since we know that
                                * the minimal value is 1min */
                else
                        t = refresh_time*60;

                if (t > 5*60)
                        t = 5*60;

                stderr.printf("Setting wakeup to %u (%u)\n", t, refresh_time);

                disk_health = this;

                if (refresh_timer > 0)
                        Source.remove(refresh_timer);
                refresh_timer = Timeout.add_seconds(t,
                                () => {
                                            stderr.printf("Woke up\n");
                                            disk_health.refresh();
                                            return false;
                                });
        }

        public void selfTest() {
                try {
                        disk.startSelfTest(test);
                        refresh();
                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                }
        }

        public bool fill_in_data() {

                try {
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

                        if (b)
                                return fill_in_smart_data();
                        else
                                return fill_in_no_smart_data();

                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                        return false;
                }
        }

        public bool fill_in_no_smart_data() {

                ((Gtk.Label) get_object("smartLabel")).set_markup("Disk health functionality (S.M.A.R.T.) is <b>not</b> available.");
                ((Gtk.Label) get_object("healthLabel")).set_label("");
                ((Gtk.Label) get_object("badSectorsLabel")).set_label("");
                ((Gtk.Label) get_object("temperatureLabel")).set_label("");
                ((Gtk.Label) get_object("powerOnLabel")).set_label("");

                return fill_in_no_self_test_data();
        }

        public string format_pretty(uint64 value, string unit) {

                switch (unit) {

                        case "mseconds":

                                if (value >= (uint64)1000*60*60*24*365)
                                        return "%0.1f years".printf(((double) value)/(1000.0*60*60*24*365));
                                else if (value >= (uint64) 1000*60*60*24*30)
                                        return "%0.1f months".printf(((double) value)/(1000.0*60*60*24*30));
                                else if (value >= 1000*60*60*24)
                                        return "%0.1f days".printf(((double) value)/(1000.0*60*60*24));
                                else if (value >= 1000*60*60)
                                        return "%0.1f h".printf(((double) value)/(1000.0*60*60));
                                else if (value >= 1000*60)
                                        return "%0.1f min".printf(((double) value)/(1000.0*60));
                                else if (value >= 1000)
                                        return "%0.1f s".printf(((double) value)/(1000.0));
                                else
                                        return "%llu ms".printf(value);

                        case "mkelvin":
                                return "%0.1f °C".printf(((double) value - 273150) / 1000);

                        case "sectors":
                                return "%llu sectors".printf(value);

                        case "none":
                                return "%llu".printf(value);

                }

                return "n/a";

        }

        public bool fill_in_smart_data() {
                try {

                        ((Gtk.Label) get_object("smartLabel")).set_label("Disk health functionality (S.M.A.R.T.) is available.");

                        bool b = disk.checkSmartStatus();

                        if (b)
                                ((Gtk.Label) get_object("healthLabel")).set_label("The disk reports to be healthy.");
                        else
                                ((Gtk.Label) get_object("healthLabel")).set_label("The disk reports that it has already failed or is expected to fail in the next 24h.");

                        DBus.ObjectPath[] attrs = disk.getAttributes();

                        uint64 temp = 0;
                        uint64 rsc = 0;
                        uint64 rec = 0;
                        uint64 cps = 0;
                        uint64 ou = 0;
                        uint64 pon = 0;

                        list_store.clear();

                        foreach (DBus.ObjectPath a in attrs) {
                                dynamic DBus.Object attribute;
                                uint8 id;
                                bool good, online, prefailure;
                                string name;
                                uint64 pretty_value;
                                string pretty_unit;
                                uint8 current, worst, threshold;

                                attribute = connection.get_object("net.poettering.SmartKit", a, "net.poettering.SmartKit.Attribute");

                                /* id, name, text, icon, current, worst, threshold, type, verdict */

                                id = attribute.getId();
                                name = attribute.getName();
                                pretty_value = attribute.getPrettyValue();
                                pretty_unit = attribute.getPrettyUnit();
                                good = attribute.isGood();
                                current = attribute.getCurrentValue();
                                worst = attribute.getWorstValue();
                                threshold = attribute.getThreshold();
                                online = attribute.isOnline();
                                prefailure = attribute.isPrefailure();

                                Gtk.TreeIter iter;
                                list_store.append(out iter);
                                list_store.set(iter,
                                               0, id,
                                               1, name,
                                               2, format_pretty(pretty_value, pretty_unit),
                                               3, good ? "dialog-ok" : "dialog-warning",
                                               4, "%u".printf(current),
                                               5, "%u".printf(worst),
                                               6, "%u".printf(threshold),
                                               7, "%s/%s".printf(online ? "Online" : "Offline", prefailure ? "Prefailure" : "Old Age"),
                                               8, good ? "OK" : "FAILURE",
                                               -1);

                                if (name == "reallocated-sector-count")
                                        rsc = pretty_value;
                                if (name == "reallocated-event-count")
                                        rec = pretty_value;
                                if (name == "current-pending-sector")
                                        cps = pretty_value;
                                if (name == "offline-uncorrectable")
                                        ou = pretty_value;

                                if (name == "temperature-celsius-1" ||
                                    name == "temperature-celsius-2" ||
                                    name == "airflow-temperature-celsius")
                                        if (pretty_value > temp)
                                                temp = pretty_value;

                                if (name == "power-on-minutes" ||
                                    name == "power-on-seconds" ||
                                    name == "power-on-hours")
                                        pon = pretty_value;
                        }

                        uint64 sectors = cps + ou + (rsc > rec ? rsc : rec);
                        if (sectors > 0)
                                ((Gtk.Label) get_object("badSectorsLabel")).set_markup("<b>Warning:</b> The disk reports to have %llu bad sectors.".printf(sectors));
                        else
                                ((Gtk.Label) get_object("badSectorsLabel")).set_label("The disk reports to have no bad sectors.");

                        if (temp > 0)
                                ((Gtk.Label) get_object("temperatureLabel")).set_label("The current temperature of the disk is %0.1f °C.".printf((double) (temp - 273150) / 1000));
                        else
                                ((Gtk.Label) get_object("temperatureLabel")).set_label("");


                        if (pon > 0)
                                ((Gtk.Label) get_object("powerOnLabel")).set_label("The disk has been powered on for %s.".printf(format_pretty(pon, "mseconds")));
                        else
                                ((Gtk.Label) get_object("powerOnLabel")).set_label("");

                        bool a = disk.getStartTestAvailable();
                        b = disk.getConveyanceTestAvailable();
                        bool c = disk.getShortAndExtendedTestAvailable();

                        if (a && (b || c))
                                return fill_in_self_test_data(c, b);
                        else
                                return fill_in_no_self_test_data();

                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                        return false;
                }
        }

        public bool fill_in_self_test_data(bool sae, bool c) {
                try {
                        bool show_percent = false;
                        string text;

                        string state = disk.getSelfTestExecutionStatus();

                        switch (state) {
                                case "success-or-never":
                                        text = "The previous self-test completed without error or no self-test has ever been run.";
                                        break;
                                case "aborted":
                                        text = "The previous self-test was aborted by the user.";
                                        break;
                                case "interrupted":
                                        text = "The previous self-test was interrupted by the host with a hardware or software reset.";
                                        break;
                                case "fatal":
                                        text = "A fatal error or unknown test error occurred while the device was executing the previous self-test and the device was unable to complete the self-test.";
                                        break;
                                case "error-unknown":
                                        text = "The previous self-test completed having a test element that failed.";
                                        break;
                                case "error-electrical":
                                        text = "The previous self-test completed having the electrical element of the test failed.";
                                        break;
                                case "eror-servo":
                                        text = "The previous self-test completed having the servo (and/or seek) test element of the test failed.";
                                        break;
                                case "error-read":
                                        text = "The previous self-test completed having the read element of the test failed.";
                                        break;
                                case "error-handling":
                                        text = "The previous self-test completed having a test element that failed and the device is suspected of having handling damage.";
                                        break;
                                case "inprogress":
                                        text = "Self-test in progress...";
                                        show_percent = true;
                                        break;

                                default:
                                        text = "Unknown state";
                                        break;
                        }

                        ((Gtk.Label) get_object("selfTestLabel")).set_label(text);

                        if (show_percent) {
                                uint percent = disk.getSelfTestExecutionPercentRemaining();

                                ((Gtk.ProgressBar) get_object("selfTestProgressBar")).set_fraction((double) (100-percent)/100);
                                ((Gtk.ProgressBar) get_object("selfTestProgressBar")).set_text("%u%% remaining".printf(percent));
                        } else {
                                ((Gtk.ProgressBar) get_object("selfTestProgressBar")).set_fraction(0.0);
                                ((Gtk.ProgressBar) get_object("selfTestProgressBar")).set_text("n/a");
                        }

                        ((Gtk.ProgressBar) get_object("selfTestProgressBar")).set_sensitive(show_percent);

                        if (state == "inprogress") {
                                ((Gtk.Button) get_object("selfTestButton")).set_label("Abort Self-Test");
                                bool b = disk.getAbortTestAvailable();
                                ((Gtk.Button) get_object("selfTestButton")).set_sensitive(b);

                                this.test = "abort";

                                if (sae)
                                        this.refresh_time = disk.getShortTestPollingMinutes();
                                else
                                        this.refresh_time = disk.getConveyanceTestPollingMinutes();


                        } else {
                                uint saet, ct;

                                ((Gtk.Button) get_object("selfTestButton")).set_label("Start Self-Test");
                                ((Gtk.Button) get_object("selfTestButton")).set_sensitive(true);

                                if (sae)
                                        this.test = "short";
                                else
                                        this.test = "conveyance";

                                this.refresh_time = 0;
                        }

                } catch (DBus.Error e) {
                        stderr.printf("D-Bus error: %s\n", e.message);
                        return false;
                }

                return true;
        }

        public bool fill_in_no_self_test_data() {
                ((Gtk.Label) get_object("selfTestLabel")).set_label("Self-test functionality is not available.");
                ((Gtk.Button) get_object("selfTestButton")).set_sensitive(false);
                ((Gtk.ProgressBar) get_object("selfTestProgressBar")).set_sensitive(false);

                test = null;
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

                if (args.length != 2) {
                        stderr.printf("Please specify device to check health for.\n");
                        return 1;
                }

                var dh  = new DiskHealth();

                if (dh.create_widgets(args[1]))
                        Gtk.main ();

                return 0;
        }
}
