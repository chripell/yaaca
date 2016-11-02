#!/usr/bin/env python2

import sys

import numpy as np
import astrolove as AL

import numpy as np
import astrolove as AL
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] " + AL.expand_file_list_help)

parser.add_option("--out", type = "string", default = "image%d",
                      help = "output file template, default image%d")
parser.add_option("--raw", action="store_true", dest="is_raw")
(options, args) = parser.parse_args()

ser = AL.SerReader(args[0], options.is_raw)

for i in xrange(ser.count):
    im = ser.get()
    if ser.depth == 16:
        if len(im) == 3:
            AL.write_ppm_65535((options.out % i) + ".ppm", im )
        else:
            AL.write_pgm_65535((options.out % i) + ".pgm", im )
    else:
        if len(im) == 3:
            AL.write_ppm_255((options.out % i) + ".ppm", im )
        else:
            AL.write_pgm_255((options.out % i) + ".pgm", im )
