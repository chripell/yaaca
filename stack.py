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
import astrolib as ALi
try:
    import pywt
except ImportError:
    print "Cannot import wavelet libraries"
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] [files or @file_list ...]")
parser.add_option("--imtype", type = "int", default = 0, help = AL.imtype_help)
parser.add_option("--method", type = "int", default = 0, help = "method: 0 mean 1 median 2 rank 3 fftrank 4 kappa-sigma 5 kappa-sigma-median 6 svd 7 kalman 666->all the methods!, 1000+method just one with list pars")
parser.add_option("--space", type = "int", default = 0, help = "data space: 0 plain, 1 fft, 2 wavelet")
parser.add_option("--group", type = "int", default = 0, help = "images x group, 0 is all")
parser.add_option("--param", type = "string", default = "'[{}]'", help = "list of dict with method prarameters")
parser.add_option("--out", type = "string", default = "./out", help = "out img without ppm without suffix")
(options, args) = parser.parse_args()

im_mode = options.imtype
method = options.method
dataMode = options.space
g = options.group
spara = options.param
diro = options.out

all_frames = AL.expand_args(args)
noOfFrames = len(all_frames)
if g==0: g=noOfFrames
noOfGroups = noOfFrames // g
if noOfFrames % g != 0:
    noOfGroups = noOfGroups + 1
for group in xrange(noOfGroups):
    frame0 = group*g
    frame1 = min(noOfFrames,frame0+g)
    (stack, width, height, imw, nim_width, nim_height) = AL.load_stack(all_frames[frame0:], frame1 - frame0, dataMode, im_mode, group)
    if method == 666:
        for mmm in xrange(0, 8):
            print "multi_method_par for method ", mmm
            pp = 0
            for ppp in eval(spara):
                print "multi_method_par for par ", ppp
                try:
                    s = AL.do_stack([x.copy() for x in stack], mmm, ppp, nim_width, nim_height, dataMode)
                    AL.save_stack(s, width, height, dataMode, im_mode, diro + "_%02d_%02d_%04d"%(mmm, pp, group), imw)
                except Exception as e:
                    print(e)
                    pass
                pp += 1
    elif method >= 1000:
        mmm = method - 1000
        print "multi_par for method ", mmm
        pp = 0
        for ppp in eval(spara):
            try:
                s = AL.do_stack([x.copy() for x in stack], mmm, ppp, width, height, dataMode)
                AL.save_stack(s, width, height, dataMode, im_mode, diro + "_%02d_%02d_%04d"%(mmm, pp, group), imw)
            except Exception as e:
                print(e)
                pass
            pp += 1
    else:
        stack = AL.do_stack(stack, method, eval(spara), width, height, dataMode)
        AL.save_stack(stack, width, height, dataMode, im_mode, diro + "_%04d"%(group), imw)
