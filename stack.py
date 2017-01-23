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
import multiprocessing
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
parser.add_option("--chunks", type = "int", default = 1, help = "how many chunks x axis")
parser.add_option("--cores", type = "int", default = 1, help = "number of workers to use")

(options, args) = parser.parse_args()

im_mode = options.imtype
method = options.method
dataMode = options.space
g = options.group
spara = options.param
diro = options.out
chunks = options.chunks
cores = options.cores

all_frames = AL.expand_args(args)
noOfFrames = len(all_frames)
if g==0: g=noOfFrames
noOfGroups = noOfFrames // g
if noOfFrames % g != 0:
    noOfGroups = noOfGroups + 1

imf = AL.load_pic(all_frames[0], im_mode)
channels = len(imf)

w, h = imf[0].shape
(cw, ch) = [i/chunks for i in (w, h)]
xs = [i*cw for i in range(chunks)]
ys = [i*ch for i in range(chunks)]

def do_chunk(chunk):
    (frame0, frame1, x, y, cw, cy, channel, group) = chunk
    print "chunk %d-%d,%d-%d,%d"%(x,x+cw,y,y+ch,channel)
    (stack, width, height, imw) = AL.load_stack(
        all_frames[frame0:], frame1 - frame0, dataMode, im_mode, group, channel,
        x, y, cw, ch)
    stack = AL.do_stack(stack, method, eval(spara), width, height, dataMode)
    return AL.unpack_stack(stack, width, height, dataMode, imw)

def get_empty():
    return np.empty((w, h), dtype=AL.myfloat)

pool = multiprocessing.Pool(cores)

for group in xrange(noOfGroups):
    frame0 = group*g
    frame1 = min(noOfFrames,frame0+g)
    todo = []
    for channel in range(channels):
        im = np.empty((w,h), dtype=AL.myfloat)
        for x in xs:
            for y in ys:
                 todo.append((frame0, frame1, x, y, cw, ch, channel, group))
    pieces = pool.map(do_chunk, todo)
    if channels == 3:
        imf = [get_empty(), get_empty(), get_empty()]
    else:
        imf = [get_empty()]
    for i, p in enumerate(todo):
        (_, _, x, y, cw, cy, channel, _) = p
        imf[channel][x:(x+cw),y:(y+ch)] = pieces[i]
    fname = diro + "_%04d"%(group)
    AL.save_pic(fname + "_linear", im_mode, imf)
    AL.save_pic(fname + "_gamma", im_mode, AL.gamma_stretch(imf))
