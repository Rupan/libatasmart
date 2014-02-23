#!/usr/bin/python

import sys
from atasmart import SkDisk, SmartError

diskdev = '/dev/sda'
try:
    sk = SkDisk(diskdev)
    print "Device awake: " + str(sk.awake())
    print "Device size: " + str(sk.size())
    id = sk.identify()
    for key in id.keys():
        print key + ' -> ' + id[key]
    print "Device temperature: " + str(sk.temp())
    print 'Checking S.M.A.R.T. status: ' + str(sk.smart_status())
    sk.close()
except SmartError, smart_re:
    print '%s: ' % diskdev + str(smart_re)
