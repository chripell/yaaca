#!/usr/bin/env python3

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import numpy as np
import astrolib as AL
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] " + AL.expand_file_list_help)

parser.add_option("--out", type = "string", default = "image%04d",
                  help = "output file template, default image%04d")
parser.add_option("--raw", action="store_true", dest="is_raw")
parser.add_option("--mode", type = "int", default = 3,
                  help = "debayer mode: 0,1=built-in, 2=superpixel, 3=opencv, 4=dcraw")
parser.add_option("--mono", action="store_true", dest="is_mono")
parser.add_option("--opt", type = "string", default = "-v -H 0 -o 0 -q 3 -4 -t 0",
                  help = "options for dcraw debayering")
(options, args) = parser.parse_args()

ser = AL.SerReader(args[0], options.is_raw, options.mode, options.opt)

for i in range(ser.count):
    im = ser.get()
    if len(im) == 3 and options.is_mono:
        imRGB = [x.astype(float) for x in im]
        im = [0.299 * imRGB[0] + 0.587 * imRGB[1] + 0.114 * imRGB[2],]
    if ser.depth == 16:
        if len(im) == 3:
            AL.write_ppm_65535((options.out % i) + ".ppm", im )
        else:
            AL.write_pgm_65535((options.out % i) + ".pgm", im[0] )
    else:
        if len(im) == 3:
            AL.write_ppm_255((options.out % i) + ".ppm", im )
        else:
            AL.write_pgm_255((options.out % i) + ".pgm", im[0] )
