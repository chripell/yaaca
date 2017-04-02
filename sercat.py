#!/usr/bin/env python2

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import numpy as np
import astrolib as AL
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] " + AL.expand_file_list_help)

parser.add_option("--out", type = "string", default = "out.ser",
                      help = "output file template, default out.ser")
parser.add_option("--crop-x", type = "int", default = 0, help = "crop x")
parser.add_option("--crop-y", type = "int", default = 0, help = "crop y")
parser.add_option("--crop-w", type = "int", default = -1, help = "crop w")
parser.add_option("--crop-h", type = "int", default = -1, help = "crop h")
parser.add_option("--mode", type = "int", default = 3, help = "debayer mode")
parser.add_option("--raw", action="store_true", dest="is_raw")
parser.add_option("--register", action="store_true", dest="is_register")

(options, args) = parser.parse_args()

crop_x = options.crop_x
crop_y = options.crop_y
crop_w = options.crop_w
crop_h = options.crop_h
out_file = options.out

ser_out = None
ref = None

for fname in args:
    ser = AL.SerReader(fname, options.is_raw, options.mode)
    for i in xrange(ser.count):
        im = ser.get()
        if crop_w < 0:
            crop_w = im[0].shape[0]
        if crop_h < 0:
            crop_h = im[0].shape[1]
        if ser_out is None:
            if options.is_raw:
                color_id = ser.color_id
            else:
                color_id = None
            ser_out = AL.SerWriter(out_file, (crop_w, crop_h) , len(im), color_id=color_id)
        if options.is_register:
            if len(im) == 1 :
                imL = im[0]
            else:
                imL = 0.299 * im[0] + 0.587 * im[1] + 0.114 * im[2]
            if ref is None:
                ref = np.fft.fft2(imL)
            else:
                fft = np.fft.fft2(imL,s=ref.shape)
                xshift,yshift = AL.registration_dft(ref, fft)
                im = [np.roll(np.roll(i, xshift, axis=0), yshift, axis=1) for i in im]
        im =  [i[crop_x:(crop_x+crop_w),crop_y:(crop_y+crop_h)] for i in im]
        ser_out.write_frame(im)

ser_out.close()
