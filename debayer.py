#!/usr/bin/env python2

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import numpy as np
import astrolove as AL
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] in_file out_file")

parser.add_option("--out", type = "string", default = "", help = "output file")
parser.add_option("--infile", type = "string", default = "", help = "input file")
parser.add_option("--mode", type = "int", default = 0,
                      help = "debayer mode: 0 vector median, 1 vector mean, 2 super-pixel")
(options, args) = parser.parse_args()

in_file = args[0]
out_file = args[1]
debayer_mode = options.mode

oser = None
iser = AL.SerReader(in_file, mode=debayer_mode)

for i in xrange(iser.count):
    im = iser.get()
    if not oser:
        oser = AL.SerWriter(out_file, im[0].shape, len(im))
    oser.write_frame(im)

oser.close()
