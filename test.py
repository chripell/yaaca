#!/usr/bin/env python

import pyasill as A
import numpy as np
import Image

cam = A.Cam(A.ASI120MC, 0)
cam.set_int_par(A.PAR_ANALOG_GAIN, 10)
print cam.get_maxw(), "x", cam.get_maxh()
#cam.set_wh(cam.get_maxw(), cam.get_maxh(), 1, A.FMT_RAW8)
cam.set_save("/tmp/")

i = 0
while True:
    x = cam.get_frame()
    #x = cam.get_frame(True)
    if x != None:
        if i < 5:
            i = i + 1
            continue
        print x.shape, " ",x.dtype, " ", x.min(), "-", x.max()
        x = x * 255.0 / x.max()
        IM = Image.fromarray(x.astype(np.uint8))
        IM.save("test.png");
        break
