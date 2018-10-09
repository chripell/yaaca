#!/usr/bin/env python3

import os
import sys
import multiprocessing
import numpy as np
from astropy.io import fits
from optparse import OptionParser
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")
import astrolib as AL


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
    idx = x[0]
    img = x[1]
    with fits.open(img) as hdul:
        data = np.flip(hdul['PRIMARY'].data.transpose(), 1)
        if options.is_raw:
            AL.write_pgm_65535((options.out % idx)+".pgm", data)


pool = multiprocessing.Pool(options.cores)
pool.map(process_image,
         [(i, x) for i, x in enumerate(args)])
