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

using GLib;
using DBus;
using Hal;
using Smart;

errordomain Error {
        SYSTEM,
        NOT_FOUND,
        SLEEPING,
        UNKNOWN_TEST
}

[DBus (name = "net.poettering.SmartKit.Manager")]
public interface ManagerAPI {
        public abstract DBus.ObjectPath getDiskByUDI(string udi) throws Error;
        public abstract DBus.ObjectPath getDiskByPath(string path) throws Error;

        /* public abstract DBus.ObjectPath[] getDisks() throws Error; */
}

[DBus (name = "net.poettering.SmartKit.Disk")]
public interface DiskAPI {

        public abstract string getPath() throws Error;
        public abstract string getUDI() throws Error;

        public abstract uint64 getSize() throws Error;

        public abstract bool checkSleepMode() throws Error;

        public abstract bool isIdentifyAvailable() throws Error;
        public abstract string getIdentifySerial() throws Error;
        public abstract string getIdentifyFirmware() throws Error;
        public abstract string getIdentifyModel() throws Error;

        public abstract bool isSmartAvailable() throws Error;
        public abstract bool checkSmartStatus() throws Error;
        public abstract void readSmartData(bool wakeup) throws Error;

        public abstract void startSelfTest(string test) throws Error;
        public abstract void abortSelfTest() throws Error;

        public abstract string getOfflineDataCollectionStatus() throws Error;
        public abstract uint getTotalOfflineDataCollectionSeconds() throws Error;

        public abstract string getSelfTestExecutionStatus() throws Error;
        public abstract uint getSelfTestExecutionPercentRemaining() throws Error;

        public abstract bool getConveyanceTestAvailable() throws Error;
        public abstract bool getShortAndExtendedTestAvailable() throws Error;
        public abstract bool getStartTestAvailable() throws Error;
        public abstract bool getAbortTestAvailable() throws Error;

        public abstract uint getShortTestPollingMinutes() throws Error;
        public abstract uint getExtendedTestPollingMinutes() throws Error;
        public abstract uint getConveyanceTestPollingMinutes() throws Error;
}

public class Disk : GLib.Object, DiskAPI {
        private Smart.Disk disk;
        public string dbus_path;

        public string path { get; construct; }
        public string udi { get; construct; }
        public DBus.Connection connection { get; construct; }

        Disk(DBus.Connection connection, string path, string udi) {
                this.connection = connection;
                this.path = path;
                this.udi = udi;
        }

        private string clean_path(string s) {
                var builder = new StringBuilder ();
                string t;

                for (int i = 0; i < s.size(); i++)
                        if (s[i].isalnum() || s[i] == '_')
                                builder.append_unichar(s[i]);
                        else
                                builder.append_unichar('_');

                return builder.str;
        }

        public void open() throws Error {
                if (Smart.Disk.open(this.path, out this.disk) < 0)
                        throw new Error.SYSTEM("open() failed");

                weak Smart.IdentifyParsedData *d;

                if (this.disk.identify_parse(out d) >= 0)
                        this.dbus_path = "/disk/%s/%s".printf(clean_path(d->model), clean_path(d->serial));
                else
                        this.dbus_path = "/disk/%s".printf(clean_path(this.path));

                stderr.printf("Registering D-Bus path %s\n", this.dbus_path);
                this.connection.register_object(this.dbus_path, this);

                this.disk.smart_read_data();
        }

        public string getPath() throws Error {
                return this.path;
        }

        public string getUDI() throws Error {
                return this.udi;
        }

        public uint64 getSize() throws Error {
                uint64 s;

                if (this.disk.get_size(out s) < 0)
                        throw new Error.SYSTEM("get_size() failed: %s", Smart.strerror(Smart.errno));

                return s;
        }

        public bool checkSleepMode() throws Error {
                bool awake;

                if (this.disk.check_sleep_mode(out awake) < 0)
                        throw new Error.SYSTEM("check_sleep_mode() failed: %s", Smart.strerror(Smart.errno));

                return awake;
        }

        public bool isIdentifyAvailable() throws Error {
                bool available;

                if (this.disk.identify_is_available(out available) < 0)
                        throw new Error.SYSTEM("identify_is_available() failed: %s", Smart.strerror(Smart.errno));

                return available;
        }

        public string getIdentifySerial() throws Error {
                weak Smart.IdentifyParsedData *d;

                if (this.disk.identify_parse(out d) < 0)
                        throw new Error.SYSTEM("identify_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->serial;
        }

        public string getIdentifyFirmware() throws Error {
                weak Smart.IdentifyParsedData *d;

                if (this.disk.identify_parse(out d) < 0)
                        throw new Error.SYSTEM("identify_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->firmware;
        }

        public string getIdentifyModel() throws Error {
                weak Smart.IdentifyParsedData *d;

                if (this.disk.identify_parse(out d) < 0)
                        throw new Error.SYSTEM("identify_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->model;
        }

        public bool isSmartAvailable() throws Error {
                bool available;

                if (this.disk.smart_is_available(out available) < 0)
                        throw new Error.SYSTEM("smart_is_available() failed: %s", Smart.strerror(Smart.errno));

                return available;
        }

        public bool checkSmartStatus() throws Error {
                bool good;

                if (this.disk.smart_status(out good) < 0)
                        throw new Error.SYSTEM("smart_status() failed: %s", Smart.strerror(Smart.errno));

                return good;
        }

        public void readSmartData(bool wakeup) throws Error {
                bool awake;

                if (!wakeup) {
                        if (this.disk.check_sleep_mode(out awake) < 0)
                                throw new Error.SYSTEM("check_sleep_mode() failed: %s", Smart.strerror(Smart.errno));

                        if (!awake)
                                throw new Error.SLEEPING("Disk is in sleep mode");
                }

                if (this.disk.smart_read_data() < 0)
                        throw new Error.SYSTEM("smart_read_data() failed: %s", Smart.strerror(Smart.errno));
        }

        public void startSelfTest(string test) {
                SmartSelfTest t;

                switch (test) {
                        case "short":
                                t = SmartSelfTest.SHORT;
                                break;
                        case "extended":
                                t = SmartSelfTest.EXTENDED;
                                break;
                        case "conveyance":
                                t = SmartSelfTest.CONVEYANCE;
                                break;
                        default:
                                throw new Error.UNKNOWN_TEST("Test %s not known", test);
                }

                if (this.disk.smart_self_test(t) < 0)
                        throw new Error.SYSTEM("smart_self_test() failed: %s", Smart.strerror(Smart.errno));
        }

        public void abortSelfTest() {

                if (this.disk.smart_self_test(SmartSelfTest.ABORT) < 0)
                        throw new Error.SYSTEM("smart_self_test() failed: %s", Smart.strerror(Smart.errno));

        }

        public string getOfflineDataCollectionStatus() throws Error {
                weak SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                switch (d->offline_data_collection_status) {
                        case SmartOfflineDataCollectionStatus.NEVER:
                                return "never";
                        case SmartOfflineDataCollectionStatus.SUCCESS:
                                return "success";
                        case SmartOfflineDataCollectionStatus.INPROGRESS:
                                return "inprogress";
                        case SmartOfflineDataCollectionStatus.SUSPENDED:
                                return "suspended";
                        case SmartOfflineDataCollectionStatus.ABORTED:
                                return "aborted";
                        case SmartOfflineDataCollectionStatus.FATAL:
                                return "fatal";
                        default:
                                return "unknown";
                }
        }

        public uint getTotalOfflineDataCollectionSeconds() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->total_offline_data_collection_seconds;
        }

        public string getSelfTestExecutionStatus() throws Error {
                weak SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                switch (d->self_test_execution_status) {
                        case SmartSelfTestExecutionStatus.SUCCESS_OR_NEVER:
                                return "success-or-never";
                        case SmartSelfTestExecutionStatus.ABORTED:
                                return "aborted";
                        case SmartSelfTestExecutionStatus.INTERRUPTED:
                                return "interrupted";
                        case SmartSelfTestExecutionStatus.FATAL:
                                return "fatal";
                        case SmartSelfTestExecutionStatus.ERROR_UNKNOWN:
                                return "error-unknown";
                        case SmartSelfTestExecutionStatus.ERROR_ELECTRICAL:
                                return "error-electrical";
                        case SmartSelfTestExecutionStatus.ERROR_SERVO:
                                return "error-servo";
                        case SmartSelfTestExecutionStatus.ERROR_READ:
                                return "error-read";
                        case SmartSelfTestExecutionStatus.ERROR_HANDLING:
                                return "error-handling";
                        case SmartSelfTestExecutionStatus.INPROGRESS:
                                return "inprogress";
                        default:
                                return "unknown";
                }
        }

        public uint getSelfTestExecutionPercentRemaining() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->self_test_execution_percent_remaining;
        }

        public bool getConveyanceTestAvailable() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->conveyance_test_available;
        }

        public bool getShortAndExtendedTestAvailable() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->short_and_extended_test_available;
        }

        public bool getStartTestAvailable() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->start_test_available;
        }

        public bool getAbortTestAvailable() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->abort_test_available;
        }

        public uint getShortTestPollingMinutes() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->short_test_polling_minutes;
        }

        public uint getExtendedTestPollingMinutes() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->extended_test_polling_minutes;
        }

        public uint getConveyanceTestPollingMinutes() throws Error {
                weak Smart.SmartParsedData *d;

                if (this.disk.smart_parse(out d) < 0)
                        throw new Error.SYSTEM("smart_parse() failed: %s", Smart.strerror(Smart.errno));

                return d->conveyance_test_polling_minutes;
        }

/*     public uint64 size { */
/*         get { */
/*             uint64 s; */
/*             this.disk.get_size(out s); */
/*             return s; */

/*         } */
/*     } */
}

public class Manager : GLib.Object, ManagerAPI {
        public DBus.Connection connection { get; construct; }
        public List<Disk> disks;

        public DBus.RawConnection raw_connection;
        public Hal.Context hal_context;

        Manager(DBus.Connection connection) {
                this.connection = connection;
        }

        public void start() throws Error {
                DBus.RawError err;

                this.connection.register_object("/", this);

                this.raw_connection = DBus.RawBus.get(DBus.BusType.SYSTEM, ref err);

                this.hal_context = new Hal.Context();
                this.hal_context.set_dbus_connection(this.raw_connection);

                string[] haldisks = this.hal_context.find_device_by_capability("storage", ref err);

                foreach (string udi in haldisks) {
                        string bdev = this.hal_context.device_get_property_string(udi, "block.device", ref err);

                        stderr.printf("Found device %s\n", bdev);

                        try {
                                Disk disk = new Disk(this.connection, bdev, udi);
                                disk.open();
                                this.disks.append(#disk);
                        } catch (Error e) {
                                stderr.printf("Failed to open disk %s: %s\n", bdev, e.message);
                        }
                }
        }

        public DBus.ObjectPath getDiskByUDI(string udi) throws Error {

                foreach (Disk d in this.disks)
                        if (d.udi == udi)
                                return new DBus.ObjectPath(d.dbus_path);

                throw new Error.NOT_FOUND("Device not found");
        }

        public DBus.ObjectPath getDiskByPath(string path) throws Error {
                foreach (Disk d in this.disks)
                        if (d.path == path)
                                return new DBus.ObjectPath(d.dbus_path);

                throw new Error.NOT_FOUND("Device not found");
        }

/*     public DBus.ObjectPath[] getDisks() throws Error { */
/*         DBus.ObjectPath[] o = new DBus.ObjectPath[this.disks.length()]; */

/*         int i = 0; */
/*         foreach (Disk d in this.disks) */
/*             o[i++] = new DBus.ObjectPath(d.dbus_path); */

/*         return o; */
/*     } */

}

int main() {

        try {
                var c = DBus.Bus.get(DBus.BusType.SYSTEM);

                dynamic DBus.Object bus = c.get_object("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");

                uint request_name_result = bus.RequestName("net.poettering.SmartKit", (uint) 0);

                if (request_name_result == DBus.RequestNameReply.PRIMARY_OWNER) {

                        MainLoop loop = new MainLoop(null, false);
                        Manager manager = new Manager(c);

                        manager.start();

                        stdout.printf("Started\n");
                        loop.run();
                }

        } catch (Error e) {
                stderr.printf("Error: %s\n", e.message);
                return 1;
        }

        return 0;
}
