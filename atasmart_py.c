/*-*- Mode: C; c-basic-offset: 4 -*-*/

/***
    This file is part of libatasmart.

    Copyright (c) 2014 Michael Mohr

    libatasmart is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 2.1 of the
    License, or (at your option) any later version.

    libatasmart is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with libatasmart. If not, If not, see
    <http://www.gnu.org/licenses/>.
***/

#include <Python.h>
#include <structmember.h>

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>

#include <atasmart.h>

static const char *no_smart = "SMART not available for this device";
static PyObject *pySmartError = NULL;

typedef struct {
    PyObject_HEAD
    char name[32];       /* The device to which this object refers */
    pthread_mutex_t mtx; /* The mutex for the SMART read operations */
    SkDisk *sk;          /* The handle for atasmart library calls */
    SkBool can_smart;    /* TRUE is the disk has SMART capability */
    SkBool can_identify; /* TRUE if the disk supports identify */
    int sts, ste, stc, sta; /* SMART self-tests */
} pySkDisk;

static PyObject *pySkDisk_close(pySkDisk *self) {
    if(self->sk != NULL) {
      sk_disk_free(self->sk);
      self->sk = NULL;
      memset((void *)self->name, (int)0x00, sizeof(self->name));
    }
    pthread_mutex_destroy(&self->mtx);
    Py_INCREF(Py_None);
    return Py_None;
}

static void pySkDisk_dealloc(pySkDisk *self) {
    PyObject *ref = pySkDisk_close(self);
    Py_XDECREF(ref);
    Py_XDECREF(pySmartError); /* FIXME: is this needed? */
    self->ob_type->tp_free((PyObject*)self);
}

/* Responsible for creating (as opposed to initializing) pySkDisk objects */
static PyObject *pySkDisk_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    pySkDisk *self;

    if((self = (pySkDisk *)type->tp_alloc(type, 0)) == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate SkDisk");
        return NULL;
    }

    /* initialize members with sane defaults */
    if(pthread_mutex_init(&self->mtx, NULL) != 0) {
        self->ob_type->tp_free((PyObject*)self);
        return PyErr_SetFromErrno(pySmartError);
    }
    memset((void *)self->name, (int)0x00, sizeof(self->name));
    self->sk = NULL;
    self->can_smart = FALSE;
    self->can_identify = FALSE;

    /* test types from SkSmartSelfTest */
    self->sts = SK_SMART_SELF_TEST_SHORT;
    self->ste = SK_SMART_SELF_TEST_EXTENDED;
    self->stc = SK_SMART_SELF_TEST_CONVEYANCE;
    self->sta = SK_SMART_SELF_TEST_ABORT;

    return (PyObject *)self;
}

static int pySkDisk_init(pySkDisk *self, PyObject *args, PyObject *kwds) {
    int sts, name_size;
    const char *name;

    if(!PyArg_ParseTuple(args, "s#", &name, &name_size)) {
        PyErr_SetString(pySmartError, "failed to retrieve device name");
        return -1;
    }

    if(name_size >= (int)sizeof(self->name)) {
        PyErr_SetString(pySmartError, "device name too long");
        return -1;
    }

    sts = sk_disk_open(name, &self->sk);
    if(sts < 0) {
        PyErr_SetString(pySmartError, "Failed to open device");
        self->sk = NULL;
        return -1;
    }

    sts = sk_disk_smart_is_available(self->sk, &self->can_smart);
    if(sts < 0) {
        sk_disk_free(self->sk);
        self->sk = NULL;
        PyErr_SetString(pySmartError, "Unable to check for SMART capability");
        return -1;
    }

    if(self->can_smart == TRUE) {
        sts = sk_disk_smart_read_data(self->sk);
        if(sts < 0) {
            sk_disk_free(self->sk);
            self->sk = NULL;
            PyErr_SetString(pySmartError, "Device reported SMART is available but unable to retrieve SMART data");
            return -1;
        }
    }

    sts = sk_disk_identify_is_available(self->sk, &self->can_identify);
    if(sts < 0) {
        sk_disk_free(self->sk);
        self->sk = NULL;
        PyErr_SetString(pySmartError, "Unable to check for identify capability");
        return -1;
    }

    strncpy(self->name, name, sizeof(self->name));
    return 0;
}

/* attempt to read SMART data from the disk */
static PyObject *pySkDisk_refresh(pySkDisk *self) {
    int sts;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    if(pthread_mutex_lock(&self->mtx) != 0)
        return PyErr_SetFromErrno(pySmartError);
    sts = sk_disk_smart_read_data(self->sk);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    if(pthread_mutex_unlock(&self->mtx) != 0)
        return PyErr_SetFromErrno(pySmartError);

    Py_INCREF(Py_None);
    return Py_None;
}

/* returns the size of the disk in bytes */
static PyObject *pySkDisk_size(pySkDisk *self) {
    int sts;
    uint64_t bytes;

    sts = sk_disk_get_size(self->sk, &bytes);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    /* Py_BuildValue: http://docs.python.org/2/c-api/arg.html */
    return Py_BuildValue("K", bytes);
}

/* determines whether a device is awake */
static PyObject *pySkDisk_awake(pySkDisk *self) {
    int sts;
    PyObject *ret;
    SkBool awake;

    sts = sk_disk_check_sleep_mode(self->sk, &awake);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);

    ret = awake ? Py_True : Py_False;
    Py_INCREF(ret);
    return ret;
}

static PyObject *pySkDisk_identify(pySkDisk *self) {
    int sts;
    const SkIdentifyParsedData *data;

    if(self->can_identify == FALSE) {
        PyErr_SetString(pySmartError, "Identify not available for this device");
        return NULL;
    }
    sts = sk_disk_identify_parse(self->sk, &data);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);

    return Py_BuildValue("{s:s,s:s,s:s}", "serial", data->serial,
                         "firmware", data->firmware, "model", data->model);
}

static PyObject *pySkDisk_overall(pySkDisk *self) {
    int sts;
    SkSmartOverall overall;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    sts = sk_disk_smart_get_overall(self->sk, &overall);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    return Py_BuildValue("s", sk_smart_overall_to_string(overall));
}

static PyObject *pySkDisk_status(pySkDisk *self) {
    int sts;
    PyObject *ret;
    SkBool good;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }

    sts = sk_disk_smart_status(self->sk, &good);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);

    ret = good ? Py_True : Py_False;
    Py_INCREF(ret);
    return ret;
}

static PyObject *pySkDisk_bad_sectors(pySkDisk *self) {
    int sts;
    uint64_t bad;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    sts = sk_disk_smart_get_bad(self->sk, &bad);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    return Py_BuildValue("K", bad);
}

static PyObject *pySkDisk_temp(pySkDisk *self) {
    int sts;
    uint64_t mkelvin;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    sts = sk_disk_smart_get_temperature(self->sk, &mkelvin);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    return Py_BuildValue("K", mkelvin);
}

static PyObject *pySkDisk_power_cycles(pySkDisk *self) {
    int sts;
    uint64_t count;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    sts = sk_disk_smart_get_power_cycle(self->sk, &count);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    return Py_BuildValue("K", count);
}

static PyObject *pySkDisk_power_on(pySkDisk *self) {
    int sts;
    uint64_t mseconds;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    sts = sk_disk_smart_get_power_on(self->sk, &mseconds);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);
    return Py_BuildValue("K", mseconds);
}

/* initiates or terminates one of several SMART self-tests */
static PyObject *pySkDisk_self_test(pySkDisk *self, PyObject *args) {
    int sts, test;

    if(self->can_smart == FALSE) {
        PyErr_SetString(pySmartError, no_smart);
        return NULL;
    }
    if(!PyArg_ParseTuple(args, "i", &test)) return NULL;
    sts = sk_disk_smart_self_test(self->sk, (SkSmartSelfTest)test);
    if(sts < 0) return PyErr_SetFromErrno(pySmartError);

    Py_INCREF(Py_None);
    return Py_None;
}

/* instance methods */
static PyMethodDef pySkDisk_methods[] = {
    {"self_test",      (PyCFunction)pySkDisk_self_test,             METH_VARARGS, "Start or stop a self-test"},
    {"power_on",       (PyCFunction)pySkDisk_power_on,              METH_NOARGS, "Get the disks power on time in milliseconds"},
    {"power_cycles",   (PyCFunction)pySkDisk_power_cycles,          METH_NOARGS, "Get the disk's power cycle count"},
    {"temp",           (PyCFunction)pySkDisk_temp,                  METH_NOARGS, "Get the disk's temperature in kelvin"},
    {"refresh",        (PyCFunction)pySkDisk_refresh,               METH_NOARGS, "Read SMART data from the disk"},
    {"status",         (PyCFunction)pySkDisk_status,                METH_NOARGS, "Determine if SMART status is good or bad"},
    {"bad_sectors",    (PyCFunction)pySkDisk_bad_sectors,           METH_NOARGS, "Get the number of bad sectors"},
    {"overall",        (PyCFunction)pySkDisk_overall,               METH_NOARGS, "Get overall SMART status"},
    {"identify",       (PyCFunction)pySkDisk_identify,              METH_NOARGS, "Parse identifying strings from SMART data"},
    {"awake",          (PyCFunction)pySkDisk_awake,                 METH_NOARGS, "Determine if the  device is awake"},
    {"size",           (PyCFunction)pySkDisk_size,                  METH_NOARGS, "Get the device size in bytes"},
    {"close",          (PyCFunction)pySkDisk_close,                 METH_NOARGS, "Release and clear object resources"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

/* no structure members exposed yet */
static PyMemberDef pySkDisk_members[] = {
    {"SELF_TEST_SHORT",      T_INT, offsetof(pySkDisk, sts), 0, "Short offline test"},
    {"SELF_TEST_EXTENDED",   T_INT, offsetof(pySkDisk, ste), 0, "Extended offline test"},
    {"SELF_TEST_CONVEYANCE", T_INT, offsetof(pySkDisk, stc), 0, "Conveyance offline test"},
    {"SELF_TEST_ABORT",      T_INT, offsetof(pySkDisk, sta), 0, "Abort offline test"},
    {NULL}  /* Sentinel */
};

static PyTypeObject pySkDiskType = {
    PyObject_HEAD_INIT(NULL)
    0,                            /*ob_size*/
    "atasmart.SkDisk",            /*tp_name*/
    sizeof(pySkDisk),             /*tp_basicsize*/
    0,                            /*tp_itemsize*/
    (destructor)pySkDisk_dealloc, /*tp_dealloc*/
    0,                            /*tp_print*/
    0,                            /*tp_getattr*/
    0,                            /*tp_setattr*/
    0,                            /*tp_compare*/
    0,                            /*tp_repr*/
    0,                            /*tp_as_number*/
    0,                            /*tp_as_sequence*/
    0,                            /*tp_as_mapping*/
    0,                            /*tp_hash */
    0,                            /*tp_call*/
    0,                            /*tp_str*/
    0,                            /*tp_getattro*/
    0,                            /*tp_setattro*/
    0,                            /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,           /*tp_flags*/
    "atasmart SkDisk",            /*tp_doc*/
    0,		                  /*tp_traverse*/
    0,		                  /*tp_clear*/
    0,		                  /*tp_richcompare*/
    0,		                  /*tp_weaklistoffset*/
    0,		                  /*tp_iter*/
    0,		                  /*tp_iternext*/
    pySkDisk_methods,             /*tp_methods*/
    pySkDisk_members,             /*tp_members*/
    0,                            /*tp_getset*/
    0,                            /*tp_base*/
    0,                            /*tp_dict*/
    0,                            /*tp_descr_get*/
    0,                            /*tp_descr_set*/
    0,                            /*tp_dictoffset*/
    (initproc)pySkDisk_init,      /*tp_init*/
    0,                            /*tp_alloc*/
    pySkDisk_new,                 /*tp_new*/
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC initatasmart(void) {
    PyObject *m;

    if(PyType_Ready(&pySkDiskType) < 0)
        return;

    m = Py_InitModule3("atasmart", pySkDisk_methods,
                       "atasmart module with SkDisk");
    if(!m) return;

    Py_INCREF(&pySkDiskType);
    PyModule_AddObject(m, "SkDisk", (PyObject *)&pySkDiskType);

    pySmartError = PyErr_NewException("atasmart.SmartError", NULL, NULL);
    if(pySmartError) {
        Py_INCREF(pySmartError);
        PyModule_AddObject(m, "SmartError", pySmartError);
    }
}
