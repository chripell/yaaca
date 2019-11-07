#!/usr/bin/env python

import sys
import focuser
from PIL import Image
import numpy as np
import cairo
import os


def out1(fname, par, focuser, im8):
    im32 = np.dstack((im8, im8, im8, im8))
    surface = cairo.ImageSurface.create_for_data(
        im32, cairo.FORMAT_RGB24, im.shape[1], im.shape[0])
    cr = cairo.Context(surface)
    focuser.draw(cr, par)
    v = focuser.get(par)
    cr.set_source_rgb(1.0, 1.0, 1.0)
    cr.set_font_size(30)
    cr.move_to(10, 50)
    cr.show_text("%s n: %d %.2f/%.2f:%.2f:%.2f/%.2f(%.2f)" % (
        par, focuser.num(),
        v.bot, v.p10, v.mean, v.p90, v.top, v.std))
    surface.write_to_png(fname + ("_%s.png" % par))


fname = sys.argv[1]
image = Image.open(fname)
im = np.array(image)
focuser = focuser.Focuser()
focuser.evaluate(im)
if focuser.num() == 0:
    print("No stars found")
    os.exit(1)
while im.max() >= 256:
    im = im / 256
im = im.astype(np.uint8)
for p in focuser.odata:
    out1(os.path.splitext(fname)[0],
         p, focuser, im)
