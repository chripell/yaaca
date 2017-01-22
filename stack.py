#!/usr/bin/env python2

# stack.py
#
# parameters:
# rank -> percentile -> {"percentile":0.5}
# fftrank -> spectralMidFrequency, spectralMidRank, spectralHighRank -> {"spectralMidFrequency":0.5,"spectralMidRank":0.5,"spectralHighRank":0.5}
# kappa-sigma -> kappa -> {"kappa":0.5}
# kappa-sigma-median -> kappa -> {"kappa":0.5}
# svd -> noOfSingularValues -> {"noOfSingularValues":3}

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import numpy as np
import astrolib as AL
try:
    import pywt
except ImportError:
    print "Cannot import wavelet libraries"
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] [files or @file_list ...]")
parser.add_option("--imtype", type = "int", default = 0, help = AL.imtype_help)
parser.add_option("--method", type = "int", default = 0, help = "method: 0 mean 1 median 2 rank 3 fftrank 4 kappa-sigma 5 kappa-sigma-median 6 svd 7 kalman")
parser.add_option("--space", type = "int", default = 0, help = "data space: 0 plain, 1 fft, 2 wavelet")
parser.add_option("--group", type = "int", default = 0, help = "images x group, 0 is all")
parser.add_option("--param", type = "string", default = "'[{}]'", help = "list of dict with method prarameters")
parser.add_option("--out", type = "string", default = "./out", help = "out img without ppm without suffix")
parser.add_option("--prebin", type = "int", default = 1, help = "bin this way before processing if > 1")
(options, args) = parser.parse_args()

im_mode = options.imtype
method = options.method
dataMode = options.space
g = options.group
spara = options.param
diro = options.out
pre_bin = options.prebin

all_frames = AL.expand_args(args)
noOfFrames = len(all_frames)
if g==0: g=noOfFrames
noOfGroups = noOfFrames // g
if noOfFrames % g != 0:
    noOfGroups = noOfGroups + 1

imf = AL.load_pic(all_frames[0], im_mode)
channels = len(imf)
    
for group in xrange(noOfGroups):
    frame0 = group*g
    frame1 = min(noOfFrames,frame0+g)
    imf = []
    for ch in range(channels):
        (stack, width, height, imw, nim_width, nim_height) = AL.load_stack(all_frames[frame0:], frame1 - frame0, dataMode, im_mode, group, pre_bin, ch)
        stack = AL.do_stack(stack, method, eval(spara), width, height, dataMode)
        imf.append(stack)
        if ch == (channels -1):
            AL.save_stack(imf, width, height, dataMode, im_mode, diro + "_%04d"%(group), imw)
