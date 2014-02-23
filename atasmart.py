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
    sk.refresh()
    #print "Device temperature: " + str(sk.temp())
    print 'SMART status is good: ' + str(sk.status())
    print 'Power cycle count: ' + str(sk.power_cycles())
    print 'Power on time: ' + str(sk.power_on())
    print 'Bad sectors: ' + str(sk.bad_sectors())
    print 'Overall: ' + str(sk.overall())
    sk.close()
except SmartError, smart_re:
    print '%s: ' % diskdev + str(smart_re)
