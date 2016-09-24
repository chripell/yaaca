#!/usr/bin/env python2

# register.py
# outputs: [x] [y] offset for every image

import numpy as np
import astrolove as AL
import scipy.signal
import scipy.ndimage.interpolation
import sys
from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] [files or @file_list ...]")
parser.add_option("--method", type = "int", default = 0, help = "0 FFT, 1 FFT Canny, 2 geometric, 100 none")
parser.add_option("--filter", type = "int", default = 0, help = "0 none 1 median 2 wavelet only ROI: 101 median  102 wavelet")
parser.add_option("--filter-par", type = "int", default = 0, help = "parameter for filter")
parser.add_option("--zoom", type = "int", default = 0, help = "zoom if > 0")
parser.add_option("--dark", type = "string", default = "N", help = "dark frame to subtract")
parser.add_option("--imtype", type = "int", default = 0, help = AL.imtype_help)
parser.add_option("--flat", type = "string", default = "N", help = "npy file wit flat to multiply")
parser.add_option("--crop-x", type = "int", default = 0, help = "crop x")
parser.add_option("--crop-y", type = "int", default = 0, help = "crop y")
parser.add_option("--crop-w", type = "int", default = -1, help = "crop w")
parser.add_option("--crop-h", type = "int", default = -1, help = "crop h")
parser.add_option("--roi-x", type = "int", default = 0, help = "roi x")
parser.add_option("--roi-y", type = "int", default = 0, help = "roi y")
parser.add_option("--roi-w", type = "int", default = -1, help = "roi w")
parser.add_option("--roi-h", type = "int", default = -1, help = "roi h")
parser.add_option("--out-dir", type = "string", default = ".", help = "output dir, default to current")
parser.add_option("--defect", type = "string", default = "", help = "defect list: x,y one per line")
parser.add_option("--defect-col", type = "string", default = "", help = "defect column list: one per line")
parser.add_option("--debayer-pattern", type = "int", default = 1, help = "0:none 1:bggr 2:grbg")
parser.add_option("--debayer-method", type = "int", default = 0, help = "0:median 1:mean")
(options, args) = parser.parse_args()

method = options.method
filter = options.filter
filter_par = options.filter_par
zoom = options.zoom
dark = options.dark
im_mode = options.imtype
if options.flat != 'N' :
    flat = np.load(options.flat)
else:
    flat = None
crop_x = options.crop_x
crop_y = options.crop_y
crop_w = options.crop_w
crop_h = options.crop_h
roi_x = options.roi_x
roi_y = options.roi_y
roi_w = options.roi_w
roi_h = options.roi_h
dir = options.out_dir

defects = []
if options.defect != "" :
    with open(options.defect) as fp:
        for line in fp:
            defects.append([int(x) for x in line.split(",")])

defect_cols = []
if options.defect_col != "" :
    with open(options.defect_col) as fp:
        for line in fp:
            defect_cols.append(int(line))

if dark != "N" :
    darkf = AL.load_pic(dark, im_mode)

ref = None
debug = False
no = 0

for n in AL.expand_args(args) :
    imRGB = AL.load_pic(n, im_mode)
    if crop_w == -1 :
        crop_w = imRGB[0].shape[0]
    if crop_h == -1 :
        crop_h = imRGB[0].shape[1]
    if roi_w == -1 :
        roi_w = crop_w
    if roi_h == -1 :
        roi_h = crop_h
    imRGB = [x.astype(AL.myfloat) for x in imRGB]
    if dark != "N" :
        imRGB = [x - y for x,y in zip(imRGB, darkf)]
        #imRGB = [x - x.min() for x in imRGB]
        imRGB = [x.clip(0, 65535) for x in imRGB]
    for defect in defects:
        x = defect[0]
        y = defect[1]
        if  x > 0 and x < (crop_w - 1) and y > 0 and y < (crop_h - 1):
            for im in imRGB:
                im[x,y] = (im[x-1,y] + im[x+1,y] + im[x,y-1] + im[x,y+1]) / 4
    for x in defect_cols:
        if x > 0 and x < (crop_w - 1) :
            for im in imRGB:
                im[x,:] = (im[x-1,:] + im[x+1,:]) / 2.0
    if flat != None :
        imRGB = [x / flat for x in imRGB]
    if im_mode == 7 or im_mode == 8:
        imRGB = AL.demosaic(imRGB[0].astype(np.uint16), options.debayer_pattern, options.debayer_method)
        imRGB = [x.astype(AL.myfloat) for x in imRGB]
    imRGB = [x[crop_x : (crop_x + crop_w), crop_y : (crop_y + crop_h)] for x in imRGB]
    if filter == 1 :
        imRGB = [scipy.signal.medfilt2d(x, kernel_size = int(filter_par)) for x in imRGB]
    elif filter == 2 :
        imRGB = [AL.waveletDenoise(x, filter_par) for x in imRGB]
    if zoom > 0 :
        imRGB = [scipy.ndimage.interpolation.zoom(x, zoom) for x in imRGB]
    if len(imRGB) == 1 :
        imL = imRGB[0]
    else:
        imL = 0.299 * imRGB[0] + 0.587 * imRGB[1] + 0.114 * imRGB[2]
    if zoom > 1 :
        nim = imL[(roi_x - crop_x)*zoom : (roi_x - crop_x + roi_w)*zoom, (roi_y - crop_y)*zoom : (roi_y - crop_y + roi_h)*zoom]
    else:
        nim = imL[(roi_x - crop_x) : (roi_x - crop_x + roi_w), (roi_y - crop_y) : (roi_y - crop_y + roi_h)]
    if filter == 101 :
        nim = scipy.signal.medfilt2d(nim, kernel_size = int(filter_par))
    elif filter == 102 :
        nim = AL.waveletDenoise(nim, filter_par)
    if debug :
        AL.save_pic(dir + "roi_area_%04d"%(no), 1, [nim])
    if method == 1 :
        nim = AL.canny(nim, sigma = 3)
    if method == 0 or method == 1 :
        if ref == None :
            imout = imRGB
            ref = np.fft.fft2(nim)
            print "0 0"
        else:
            fft = np.fft.fft2(nim,s=ref.shape)
            xshift,yshift = AL.registration_dft(ref, fft)
            print "%d %d"%(xshift, yshift)
            imout = [np.roll(np.roll(x, xshift, axis=0), yshift, axis=1) for x in imRGB]
    elif method == 2:
        if ref == None :
            imout = imRGB
            ref = nim
            yref,xref = AL.geometric_median(ref,threshold=0.8*np.max(ref))
            print "0 0"
        else:
            my,mx = AL.geometric_median(nim,threshold=0.8*np.max(nim))
            yshift,xshift = int(yref-my), int(xref-mx)
            imout = [np.roll(np.roll(x, xshift, axis=0), yshift, axis=1) for x in imRGB]
            print "%d %d"%(xshift, yshift)
    else:
        imout = imRGB
    AL.save_pic(dir + "registered_%05d"%(no), im_mode, imout)
    no = no + 1
