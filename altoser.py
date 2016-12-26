#!/usr/bin/env python2

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import numpy as np
import astrolib as AL
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] " + AL.expand_file_list_help)

parser.add_option("--out", type = "string", default = "-", help = "output file, default stdout")
parser.add_option("--imtype", type = "int", default = 0, help = AL.imtype_help)
parser.add_option("--ser_observer", type = "string", default = "", help = "SER observer field")
parser.add_option("--ser_instrument", type = "string", default = "", help = "SER instrument field")
parser.add_option("--ser_telescope", type = "string", default = "", help = "SER telescope field")
(options, args) = parser.parse_args()

im_mode = options.imtype
out_file = options.out
ser_observer = options.ser_observer
ser_instrument = options.ser_instrument
ser_telescope = options.ser_telescope

ser = None

for n in AL.expand_args(args):
    imRGB = AL.load_pic(n, im_mode)
    if not ser:
        ser = AL.SerWriter(out_file, imRGB[0].shape, len(imRGB))
    ser.write_frame(imRGB)

ser.close()

