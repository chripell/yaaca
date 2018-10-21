#!/usr/bin/env python3

import os
import sys
import multiprocessing
import numpy as np
from astropy.io import fits
from optparse import OptionParser
from collections import namedtuple
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")
import astrolib as AL


ImageWork = namedtuple("ImageWork", "index filename")

parser = OptionParser(usage="usage: %prog [opts] " + AL.expand_file_list_help)
parser.add_option("--out", type="string", default="image%04d",
                  help="output file template, default image%04d")
parser.add_option("--raw", action="store_true", dest="is_raw")
parser.add_option(
    "--mode", type="int", default=3,
    help="debayer mode: 0,1=built-in, 2=superpixel, 3=opencv, 4=dcraw")
parser.add_option("--mono", action="store_true", dest="is_mono")
parser.add_option("--opt", type="string", default="-v -H 0 -o 0 -q 3 -4 -t 0",
                  help="options for dcraw debayering")
parser.add_option("--cores", type="int", default=1,
                  help="number of workers to use")
(options, args) = parser.parse_args()


def process_image(x):
    with fits.open(x.filename) as hdul:
        data = hdul['PRIMARY'].data
        if options.is_raw:
            AL.write_pgm_65535((options.out % x.index)+".pgm", data)
            return
        color = AL.demosaic(data, 2, options.mode, options.opt)
        if options.is_mono:
            AL.write_pgm_65535(
                (options.out % x.index)+".pgm",
                0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2])
            return
        AL.write_ppm_65535((options.out % x.index) + ".ppm",
                           color)


pool = multiprocessing.Pool(options.cores)
pool.map(process_image,
         [ImageWork(i, x) for i, x in enumerate(args)])
