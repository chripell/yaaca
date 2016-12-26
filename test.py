#!/usr/bin/python2

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import ASI
import time
import scipy.misc

print(ASI.list())

c = ASI.Camera(0)
print(c.prop())
c.set({'width': 640, 'height': 480, 'start_x': 320, 'start_y': 240})
s = c.stat()
assert s['width'] == 640
assert s['height'] == 480
assert s['start_x'] == 320
assert s['start_y'] == 240
# Only for color ones 1:
c.set({'type': 1})
c.start()
print(c.stat())
for i in xrange(5):
    time.sleep(0.5)
    s = c.stat()
    im = c.get_image()
    print s['captured'], s['vals'][7]/10.0, s['width'], s['height'], s['type'], im.shape
    c.set({'start_x': 320 - 20 * (i + 1)})
    scipy.misc.imsave('/tmp/yaaca_test_%d.jpg' % i, im)
s = c.stat()
v = s['vals']
a = s['auto']
v[0] = 33
a[0] = False
v[1] = 111111
a[1] = False
c.set({'vals': v, 'auto': a, 'start_x': 0, 'start_y': 0})
time.sleep(1)
s = c.stat()
print(s)
assert s['start_x'] == 0
assert s['start_y'] == 0
assert s['vals'][0] == 33
assert not s['auto'][0]
assert s['vals'][1] == 111111
assert not s['auto'][1]
c.stop()
c.close()
