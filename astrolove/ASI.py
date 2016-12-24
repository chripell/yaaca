
import ctypes
import os
import json
import numpy as np

MAXB = 10000
BPP = (1, 3, 2, 1)
BPPA = (3, 3, 6, 1)
DS = (np.uint8, np.uint8, np.uint16, np.uint8)
N = 0
S = 1
E = 2
W = 3

c_char_p = ctypes.POINTER(ctypes.c_char)

asi = ctypes.CDLL(os.path.join(os.path.dirname(__file__), "libyaaca.so.1"))

def check(n):
    # TODO: textual error
    if n != 0:
        raise IOError("ASI error: %d" % n)


def trim(s):
    l = s.raw.find("\000")
    return s.raw[:l]

    
def list():
    o = ctypes.create_string_buffer(MAXB)
    r = asi.yaaca_cmd('{"cmd":"list"}', o, MAXB)
    check(r)
    return json.loads(trim(o))

    
class Camera(object):

    def __init__(self, idx):
        self.idx = idx
        self.pulsed = {}
        check(asi.yaaca_cmd('{"cmd":"open","idx":%d}' % idx, None, 0))

    def close(self):
        check(asi.yaaca_cmd('{"cmd":"close","idx":%d}' % self.idx, None, 0))

    def prop(self):
        o = ctypes.create_string_buffer(MAXB)
        r = asi.yaaca_cmd('{"cmd":"prop","idx":%d}' % self.idx, o, MAXB)
        check(r)
        return json.loads(trim(o))

    def start(self):
        check(asi.yaaca_cmd('{"cmd":"start","idx":%d}' % self.idx, None, 0))

    def stop(self):
        check(asi.yaaca_cmd('{"cmd":"stop","idx":%d}' % self.idx, None, 0))

    def stat(self):
        o = ctypes.create_string_buffer(MAXB)
        r = asi.yaaca_cmd('{"cmd":"stat","idx":%d}' % self.idx, o, MAXB)
        check(r)
        s = json.loads(trim(o))
        self.height = s['height']
        self.width = s['width']
        self.type = s['type']
        self.auto_debayer = s['auto_debayer']
        return s

    def set(self, vals):
        vals['cmd'] = 'set'
        vals['idx'] = self.idx
        json_vals = json.dumps(vals)
        r = asi.yaaca_cmd(json_vals, None, 0)
        check(r)

    def set_data_type(self):
        s = self.stat()
        
    def get_image(self):
        if self.auto_debayer:
            t = self.width * self.height * BPPA[self.type]
        else:
            t = self.width * self.height * BPP[self.type]
        x = np.require(np.zeros(t), np.uint8, ('C', 'W', 'A'))
        r = asi.yaaca_cmd('{"cmd":"data","idx":%d}' % self.idx, x.ctypes.data_as(c_char_p), t)
        check(r)
        if self.auto_debayer:
            if self.type == 0 or self.type == 1:
                imr = x.reshape(self.height, self.width, 3)
                return imr[:,:,::-1]
            elif self.type == 2:
                return x.view(dtype=DS[self.type]).reshape(self.height, self.width, 3)
            else:
                return x.view(dtype=DS[self.type]).reshape(self.height, self.width)
        else:
            if self.type == 1:
                imr = x.reshape(self.height, self.width, 3)
                return imr[:,:,::-1]
            else:
                return x.view(dtype=DS[self.type]).reshape(self.height, self.width)

    def pulse(self, dir, on):
        if on and self.pulsed.get(dir, False):
            return
        self.pulsed[dir] = on
        check(asi.yaaca_cmd('{"cmd":"pulse","idx":%d,"dir":%d,"on":%d,}' %
                            (self.idx, dir, int(on)), None, 0))
   
