#!/usr/bin/env python3

# register.py
# outputs: [x] [y] offset for every image

import sys
import os
import multiprocessing
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import numpy as np
import astrolib as AL
import scipy.signal
import scipy.ndimage.interpolation
import imreg

from optparse import OptionParser

parser = OptionParser(usage = "usage: %prog [opts] [files or @file_list ...]")
parser.add_option("--method", type = "int", default = 0, help = "0 FFT, 1 FFT Canny, 2 geometric, 3 imreg, 100 none")
parser.add_option("--filter", type = "int", default = 0, help = "0 none 1 median 2 wavelet only ROI: 101 median  102 wavelet")
parser.add_option("--filter-par", type = "int", default = 0, help = "parameter for filter")
parser.add_option("--zoom", type = "int", default = 0, help = "zoom if > 0")
parser.add_option("--dark", type = "string", default = "N", help = "dark frame to subtract")
parser.add_option("--imtype", type = "int", default = 0, help = AL.imtype_help)
parser.add_option("--flat", type = "string", default = "N", help = "npy file with flat to divide with")
parser.add_option("--crop-x", type = "int", default = 0, help = "crop x")
parser.add_option("--crop-y", type = "int", default = 0, help = "crop y")
parser.add_option("--crop-w", type = "int", default = -1, help = "crop w")
parser.add_option("--crop-h", type = "int", default = -1, help = "crop h")
parser.add_option("--roi-x", type = "int", default = -1, help = "roi x")
parser.add_option("--roi-y", type = "int", default = -1, help = "roi y")
parser.add_option("--roi-w", type = "int", default = -1, help = "roi w")
parser.add_option("--roi-h", type = "int", default = -1, help = "roi h")
parser.add_option("--out-dir", type = "string", default = "./", help = "output dir, default to current")
parser.add_option("--defect", type = "string", default = "", help = "defect list: x,y one per line")
parser.add_option("--defect-col", type = "string", default = "", help = "defect column list: one per line")
parser.add_option("--debayer-pattern", type = "int", default = 2, help = "0:none 1:bggr 2:grbg")
parser.add_option("--debayer-method", type = "int", default = 3, help = "0:median 1:mean 2:super_pixel 3:opencv")
parser.add_option("--save-npy", type = "int", default = 0, help = "if non zero save npy of image as well")
parser.add_option("--cores", type = "int", default = 1, help = "number of workers to use")
(options, args) = parser.parse_args()

debayer_method = options.debayer_method
debayer_pattern = options.debayer_pattern
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
save_npy = options.save_npy
cores = options.cores

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

def prev_pow(x):
    p = 1
    while p < x:
        p *= 2
    return p / 2

def prepare_image(n):
    global crop_w, crop_h, roi_w, roi_h, roi_x, roi_y
    imSP = None
    imRGB = AL.load_pic(n, im_mode)
    if crop_w == -1 :
        crop_w = imRGB[0].shape[0]
    if crop_h == -1 :
        crop_h = imRGB[0].shape[1]
    if roi_w == -1 :
        roi_w = prev_pow(crop_w / 2)
    if roi_h == -1 :
        roi_h = prev_pow(crop_h / 2)
    if roi_x == -1 :
        roi_x = (imRGB[0].shape[0] - roi_w) / 2
    if roi_y == -1 :
        roi_y = (imRGB[0].shape[1] - roi_h) / 2
    imRGB = [x.astype(AL.myfloat) for x in imRGB]
    if dark != "N":
        imRGB = [x - y for x,y in zip(imRGB, darkf)]
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
    imRAW = imRGB[0].astype(np.uint16)
    if im_mode == 7 or im_mode == 8 or im_mode == 16:
        imRGB = AL.demosaic(imRAW, debayer_pattern, debayer_method)
        imRGB = [x.astype(AL.myfloat) for x in imRGB]
    if not flat is None :
        imRGB = [x / flat for x in imRGB]
    imRGB = [x[crop_x : (crop_x + crop_w), crop_y : (crop_y + crop_h)] for x in imRGB]
    if im_mode == 16:
        imSP1 = AL.demosaic(imRAW, debayer_pattern, AL.DEBAYER_SUPER_PIXEL)
        imSP = [np.repeat(np.repeat(x, 2, axis=0), 2, axis=1) for x in imSP1]
        imSP = [x.astype(AL.myfloat) for x in imSP]
        if not flat is None :
            imSP = [x / flat for x in imSP]
        imSP = [x[crop_x : (crop_x + crop_w), crop_y : (crop_y + crop_h)] for x in imSP]

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
        AL.save_pic(dir + "roi_area_%s"%(os.path.basename(n)), 1, [nim])
    if method == 1 :
        nim = AL.canny(nim, sigma = 3)
    return imRGB, nim, imL, imSP

def get_ref(nim):
    if method == 0 or method == 1 :
        ref = np.fft.fft2(nim)
    elif method == 2:
        yref,xref = AL.geometric_median(nim,threshold=0.8*np.max(nim))
        ref = (yref,xref)
    elif method == 3:
        ref = nim
    else:
        ref = None
    return ref

def save_image(idx, imout, mode, prefix = ""):
    fname = dir + prefix + "registered_%05d"%(idx)
    AL.save_pic(fname, mode, imout)
    if save_npy != 0:
        if len(imout) == 3:
            nim = np.dstack(imout)
            np.save(fname, nim)
        else:
            np.save(fname, imout[0])

def process_image(ii):
    (idx, n, ref) = ii
    (imRGB, nim, imL, imSP) = prepare_image(n)
    angle = 0
    success = 1
    if method == 0 or method == 1 :
        fft = np.fft.fft2(nim,s=ref.shape)
        xshift,yshift = AL.registration_dft(ref, fft)
    elif method == 2:
        (yref,xref) = ref
        my,mx = AL.geometric_median(nim,threshold=0.8*np.max(nim))
        yshift,xshift = int(yref-my), int(xref-mx)
    elif method == 3:
        tran = imreg.translation(ref, nim)
        xshift = int(round(tran['tvec'][0]))
        yshift = int(round(tran['tvec'][1]))
        angle = tran['angle']
        success = tran['success']
    else:
        xshift = 0
        yshift = 0
    print(("%s: %d,%d %f %f" % (n, xshift, yshift, angle, success)))
    imout = [np.roll(np.roll(x, xshift, axis=0), yshift, axis=1) for x in imRGB]
    save_image(idx + 1, imout, im_mode)
    if im_mode == 16:
        imout = np.roll(np.roll(imL, xshift, axis=0), yshift, axis=1)
        save_image(idx + 1, [imout], 1, "bw_")
        imout = [np.roll(np.roll(x, xshift, axis=0), yshift, axis=1) for x in imSP]
        save_image(idx + 1, imout, 0, "sp_")
    

all = AL.expand_args(args)
base, nim, baseL, baseSP = prepare_image(all[0])
ref = get_ref(nim)
save_image(0, base, im_mode)
if im_mode == 16:
    save_image(0, [baseL], 1, "bw_")
    save_image(0, baseSP, 0, "sp_")
todo = [(idx, im, ref) for idx, im in enumerate(all[1:])]

pool = multiprocessing.Pool(cores)
pool.map(process_image, todo)
