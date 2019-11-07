#!/usr/bin/env python3

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "astrolove"))
sys.path.append("/usr/lib/astrolove")

import ASI
try:
    import astrolib as AL
except ImportError:
    print("Cannot import astrolove, SAA won't work")
import focuser
try:
    import focuser
except ImportError:
    print("Cannot import focuser")

import json
import numpy as np
import time
import os
import glob
import datetime
import tempfile
from PIL import Image

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GdkPixbuf, GLib, Gdk, Gio, GObject


class ImageManager(object):

    def __init__(self):
        self.main = Gtk.DrawingArea()
        self.main.connect("draw", self.main_draw)
        self.main.connect("configure-event", self.main_configure)
        self.main.add_events(Gdk.EventMask.BUTTON_PRESS_MASK)
        self.main.connect('button-press-event', self.on_main_button_press)

        self.small = Gtk.DrawingArea()
        self.small.connect("draw", self.small_draw)
        self.small.add_events(Gdk.EventMask.BUTTON_PRESS_MASK)
        self.small.connect('button-press-event', self.on_small_button_press)

        self.histo_box = Gtk.VBox()
        self.histo = Gtk.DrawingArea()
        self.histo.connect("draw", self.histo_draw)
        self.histo.add_events(Gdk.EventMask.BUTTON_PRESS_MASK |
                              Gdk.EventMask.BUTTON_RELEASE_MASK)
        self.histo.connect("button-press-event", self.on_histo_press)
        self.histo.connect("button-release-event", self.on_histo_release)
        self.histo.set_property("height-request", 200)
        self.histo_box.pack_start(self.histo, False, False, 0)
        l = Gtk.Label()
        l.set_markup("Stretch: z-x c-v")
        self.histo_box.pack_start(l, False, False, 0)

        self.info_box = Gtk.Label()
        self.info_box.set_justify(Gtk.Justification.CENTER)
        self.info_box.set_markup("Not started")

        self.zoom = 1.0
        self.small_x = 0
        self.small_y = 0
        self.off_x = 0
        self.off_y = 0
        self.pb = None
        self.im_height = 0
        self.im_width = 0
        self.px = -1
        self.py = -1
        self.cross = False
        self.hist = False
        self.histo_data = None
        self.bins = [2*i - 0.5 for i in range(129)]
        self.stretch_from = 0
        self.stretch_to = 255
        self.stretch_start = -1
        self.box_size = 0
        self.ndark = 0
        self.nlight = 0
        self.add_dark = False
        self.do_saa = False
        self.show_raw = True
        self.show_fst = False
        self.current = None
        self.xshift = 0
        self.yshift = 0
        self.disp_im = None
        self.gamma_stretch = False
        self.hook = None

    def update_info(self):
        s = "<b>%d</b>,<b>%d</b> box(b,n) <b>%d</b>\nstretch: <b>%d-%d</b>" % (
            self.px, self.py,
            self.box_size, self.stretch_from, self.stretch_to)
        if self.disp_im is not None:
            if len(self.disp_im.shape) == 3:
                s = s + "\nV: %d, %d, %d" % (
                    int(self.disp_im[self.py, self.px, 0]),
                    int(self.disp_im[self.py, self.px, 1]),
                    int(self.disp_im[self.py, self.px, 2]))
            else:
                s = s + "\nValue: %d" % int(
                    self.disp_im[self.py, self.px])
        if self.do_saa:
            s += "\nSAA off: %d,%d" % (self.xshift, self.yshift)
        if self.show_fst:
            mode = "Raw"
        elif self.show_raw:
            mode = "Processed"
        else:
            mode = "SAA/Dark"
        s += "\n<b>%s</b> light: %d dark: %d" % (mode, self.nlight, self.ndark)
        self.info_box.set_markup(s)

    def get_box(self):
        x = int(self.px)
        if x < 0 or x >= self.im_width:
            x = self.im_width // 2
        y = (self.py)
        if y < 0 or y >= self.im_height:
            y = self.im_height // 2
        d = self.box_size
        if d < 16:
            d = 16
        x0 = x - d
        x1 = x + d
        if x0 < 0:
            x0 = 0
            x1 = 2 * d
        if x1 >= self.im_width:
            x0 = self.im_width - 2 * d
            x1 = self.im_width
        y0 = y - d
        y1 = y + d
        if y0 < 0:
            y0 = 0
            y1 = 2 * d
        if y1 >= self.im_height:
            y0 = self.im_height - 2 * d
            y1 = self.im_height
        return x0, x1, y0, y1

    def set_saa(self, v):
        self.do_saa = v

    def show_saa_dark(self, v):
        self.show_raw = not v
        self.new_image()

    def show_fast(self, v):
        self.show_fst = v
        self.new_image()

    def do_add_dark(self, v):
        self.add_dark = v

    def do_gamma_stretch(self, v):
        self.gamma_stretch = v

    def reset_dark(self):
        self.ndark = 0
        self.add_dark = False

    def reset_saa(self):
        self.nlight = 0
        self.do_saa = False

    def set_hook(self, hook):
        self.hook = hook

    def process_image(self):
        done = False
        if self.imtype == 1 or (self.imtype == 0 and self.auto_debayer != 0):
            imR = self.im[:, :, 0]
            imG = self.im[:, :, 1]
            imB = self.im[:, :, 2]
            imL = 0.299 * imR + 0.587 * imG + 0.114 * imB
            done = True
        elif self.imtype == 2 and self.auto_debayer != 0:
            # I have no idea why R and B are swapped :-/
            imR = self.im[:, :, 2]
            imG = self.im[:, :, 1]
            imB = self.im[:, :, 0]
            imL = (0.299 / 257.0 * imR +
                   0.587 / 257.0 * imG + 0.114 / 257.0 * imB)
            done = True
        elif self.imtype == 2:
            imL = self.im / 257.0
            done = True
        else:
            imL = self.im
            done = True
        if not done:
            raise ValueError(
                'Unsupported image format %s in numpy array' % self.im.dtype)

        if self.hist:
            if self.box_size > 1:
                x0, x1, y0, y1 = self.get_box()
                imH = imL[y0:y1, x0:x1]
            else:
                imH = imL
            if self.gamma_stretch:
                imH = AL.gamma_stretch([imH], mv=255.0)[0]
            self.histo_data = np.histogram(imH, bins=self.bins)[0]
        else:
            self.histo_data = None

        if self.imtype == 1 or (self.imtype == 0 and self.auto_debayer != 0):
            im = self.im
            imt = 1
            adb = 1
        elif self.imtype == 2 and self.auto_debayer != 0:
            im = self.im[:, :, 2::-1] / 257.0
            imt = 1
            adb = 1
        elif self.imtype == 2:
            im = imL
            imt = 3
            adb = 0
        else:
            im = self.im
            imt = 3
            adb = 0

        if self.add_dark:
            if self.ndark == 0:
                self.dark = im.astype(np.float)
                self.ndark = 1
            else:
                self.dark += im
                self.ndark += 1

        if self.do_saa:
            if self.nlight == 0:
                self.fft_box = self.get_box()
                nim = imL[self.fft_box[2]:self.fft_box[3],
                          self.fft_box[0]:self.fft_box[1]]
                self.fft_ref = np.fft.fft2(nim)
                self.light = im.astype(np.float)
                self.nlight = 1
            else:
                nim = imL[self.fft_box[2]:self.fft_box[3],
                          self.fft_box[0]:self.fft_box[1]]
                fft = np.fft.fft2(nim, s=self.fft_ref.shape)
                self.xshift, self.yshift = AL.registration_dft(
                    self.fft_ref, fft)
                self.light += np.roll(
                    np.roll(im, self.xshift, axis=0), self.yshift, axis=1)
                self.nlight += 1

        self.current = (im, imt, adb)
        return self.redraw_image()

    def redraw_image(self):
        im = self.current[0]
        if not self.show_raw:
            if self.nlight > 0:
                im = self.light / self.nlight
            if self.ndark > 0:
                im = im - self.dark / self.ndark
        if self.gamma_stretch:
            im = AL.gamma_stretch([im], mv=255.0)[0]
        if self.stretch_from > 0 or self.stretch_to < 255:
            scale = 255.0 / (self.stretch_to - self.stretch_from)
            im = (np.clip(im, self.stretch_from, self.stretch_to)
                  - self.stretch_from) * scale
        return im.astype(np.uint8), self.current[1], self.current[2]

    def new_image(self, nim=None, nimtype=None, nauto_debayer=None):
        if nim is not None and (nimtype is not None and
                                nauto_debayer is not None):
            if ((nauto_debayer == 2
                 and nimtype == 0) or (nauto_debayer == 2 and nimtype == 2)):
                (h, w, c) = nim.shape
                self.im = nim.reshape(2*h, w//2, c)[:h//2, :, :]
            else:
                self.im = nim
            self.imtype = nimtype
            self.auto_debayer = nauto_debayer
            if self.show_fst:
                im = self.im
                imtype = self.imtype
                auto_debayer = self.auto_debayer
            else:
                im, imtype, auto_debayer = self.process_image()
        else:
            if self.current is None:
                return
            im, imtype, auto_debayer = self.redraw_image()
        if self.hook:
            im, imtype, auto_debayer = self.hook(im, imtype, auto_debayer)

        l = GdkPixbuf.PixbufLoader.new_with_type('pnm')
        done = False
        self.im_height = im.shape[0]
        self.im_width = im.shape[1]
        self.disp_im = im
        self.norm_cross()
        if imtype == 1 or (imtype == 0 and auto_debayer != 0):
            l.write(b'P6\n%d %d\n255\n' % (im.shape[1], im.shape[0]))
            l.write(im.tobytes())
            done = True
        elif imtype == 2 and auto_debayer != 0:
            l.write(b'P6\n%d %d\n255\n' % (im.shape[1], im.shape[0]))
            # I have no idea why R and B are swapped :-/
            l.write(im.view('B')[:, :, 5::-2].tobytes())
            l.write(im.tobytes())
            done = True
        elif imtype == 2:
            l.write(b'P5\n%d %d\n255\n' % (im.shape[1], im.shape[0]))
            l.write(im.view('B')[:, 1::2].tobytes())
            l.write(im.tobytes())
            done = True
        else:
            l.write(b'P5\n%d %d\n255\n' % (im.shape[1], im.shape[0]))
            l.write(im.tobytes())
            done = True
        if not done:
            raise ValueError(
                'Unsupported image format %s in numpy array' % im.dtype)
        l.close()
        self.pb = l.get_pixbuf()

        new_small_width = self.small.get_allocated_width()
        old_small_height = self.small.get_allocated_height()
        new_small_height = int(float(self.im_height) /
                               self.im_width * new_small_width)
        if new_small_height != old_small_height:
            self.small.set_size_request(new_small_width, new_small_height)
        self.small_width = new_small_width
        self.small_height = new_small_height

        self.calc()

        self.main.queue_draw()
        self.small.queue_draw()
        if self.hist or self.stretch_from > 0 or self.stretch_to < 255:
            self.histo.queue_draw()

    def calc(self):
        if self.pb:
            real_width = self.im_width * self.zoom
            real_height = self.im_height * self.zoom
            main_width = self.main.get_allocated_width()
            main_height = self.main.get_allocated_height()

            self.view_width = float(main_width) / real_width * self.small_width
            if self.view_width > self.small_width - 1:
                self.view_width = self.small_width - 1
            self.view_height = (float(main_height) /
                                real_height * self.small_height)
            if self.view_height > self.small_height - 1:
                self.view_height = self.small_height - 1

            self.view_off_x = int(self.small_x - self.view_width / 2)
            if self.view_off_x < 0:
                self.view_off_x = 0
            if self.view_off_x + self.view_width > self.small_width - 1:
                self.view_off_x = self.small_width - 1 - self.view_width

            self.view_off_y = int(self.small_y - self.view_height / 2)
            if self.view_off_y < 0:
                self.view_off_y = 0
            if self.view_off_y + self.view_height > self.small_height - 1:
                self.view_off_y = self.small_height - 1 - self.view_height

            if self.small_width == 0 or self.small_height == 0:
                return
            self.off_x = int(float(self.view_off_x) /
                             self.small_width * real_width)
            self.off_y = int(float(self.view_off_y) /
                             self.small_height * real_height)

    def centered_text(self, wi, cr, sz, txt):
        w = wi.get_allocated_width()
        h = wi.get_allocated_height()
        cr.set_font_size(sz)
        (x, y, width, height, dx, dy) = cr.text_extents(txt)
        cr.move_to(w/2 - width/2, h/2)
        cr.show_text(txt)

    def main_draw(self, w, cr):
        if not self.pb:
            self.centered_text(w, cr, 40, "Open a Camera!")
            return
        if self.zoom == 1.0:
            Gdk.cairo_set_source_pixbuf(cr, self.pb, -self.off_x, -self.off_y)
        else:
            off_x = self.off_x / self.zoom
            off_y = self.off_y / self.zoom
            width = float(self.main.get_allocated_width()) / self.zoom
            height = float(self.main.get_allocated_height()) / self.zoom
            if off_x + width >= self.im_width:
                width = self.im_width - off_x
            if off_y + height >= self.im_height:
                height = self.im_height - off_y
            Gdk.cairo_set_source_pixbuf(
                cr, self.pb.new_subpixbuf(
                    off_x, off_y, width, height).scale_simple(
                        width * self.zoom, height * self.zoom,
                        GdkPixbuf. InterpType.BILINEAR), 0, 0)
        cr.paint()
        if self.cross:
            cr.set_source_rgb(0.80, 0, 0)
            cr.set_line_width(1)
            (x, y) = self.from_real(self.px, self.py)
            cr.move_to(0, y)
            cr.line_to(self.main.get_allocated_width(), y)
            cr.move_to(x, 0)
            cr.line_to(x, self.main.get_allocated_height())
            cr.stroke()
            if self.box_size > 1:
                (x,y) = self.from_real(self.px - self.box_size,
                                       self.py - self.box_size)
                cr.move_to(x, y)
                (x,y) = self.from_real(self.px + self.box_size,
                                       self.py - self.box_size)
                cr.line_to(x, y)
                (x,y) = self.from_real(self.px + self.box_size,
                                       self.py + self.box_size)
                cr.line_to(x, y)
                (x,y) = self.from_real(self.px - self.box_size,
                                       self.py + self.box_size)
                cr.line_to(x, y)
                (x,y) = self.from_real(self.px - self.box_size,
                                       self.py - self.box_size)
                cr.line_to(x, y)
                cr.stroke()

    def to_real(self, x, y):
        return ((x + self.off_x) / self.zoom,
                (y + self.off_y) / self.zoom)

    def from_real(self, x, y):
        if self.zoom == 1.0:
            off_x = self.off_x
            off_y = self.off_y
        else:
            off_x = self.off_x / self.zoom
            off_y = self.off_y / self.zoom
        return ((x - off_x) * self.zoom,
                (y - off_y) * self.zoom)

    def on_main_button_press(self, w, ev):
        (self.px, self.py) = self.to_real(ev.x, ev.y)
        self.px = int(self.px)
        self.py = int(self.py)
        self.norm_cross()
        self.main.queue_draw()

    def small_draw(self, w, cr):
        if not self.pb:
            self.centered_text(w, cr, 10, "Open a Camera!")
            return
        width = w.get_allocated_width()
        height = w.get_allocated_height()
        Gdk.cairo_set_source_pixbuf(
            cr, self.pb.scale_simple(
                width, height, GdkPixbuf.InterpType.BILINEAR), 0, 0)
        cr.paint()
        cr.set_source_rgb(0.42, 0.65, 0.80)
        cr.set_line_width(2)
        cr.rectangle(self.view_off_x, self.view_off_y,
                     self.view_width, self.view_height)
        cr.stroke()
        if self.cross:
            cr.set_source_rgb(0.80, 0, 0)
            cr.set_line_width(1)
            x = self.px * width / self.im_width
            y = self.py * height / self.im_height
            cr.move_to(0, y)
            cr.line_to(width, y)
            cr.move_to(x, 0)
            cr.line_to(x, height)
            cr.stroke()

    def on_small_button_press(self, w, ev):
        self.small_x = ev.x
        self.small_y = ev.y
        self.calc()
        self.main.queue_draw()
        self.small.queue_draw()

    def main_configure(self, w, ev):
        if self.pb:
            self.calc()
            self.main.queue_draw()
            self.small.queue_draw()

    def set_zoom(self, zoom):
        self.zoom = zoom
        self.calc()
        self.main.queue_draw()
        self.small.queue_draw()

    def norm_cross(self):
        if self.px < 0 or self.px >= self.im_width:
            self.px = self.im_width // 2
        if self.py < 0 or self.py >= self.im_height:
            self.py = self.im_height // 2
        self.update_info()

    def set_cross(self, en):
        self.cross = en
        self.norm_cross()

    def set_histo(self, en):
        self.hist = en
        self.histo_data = None
        self.histo.queue_draw()

    def on_histo_press(self, w, ev):
        self.stretch_start = ev.x

    def on_histo_release(self, w, ev):
        if abs(ev.x - self.stretch_start) > 10:
            if ev.x > self.stretch_start:
                self.stretch_from = self.stretch_start
                self.stretch_to = ev.x
            else:
                self.stretch_from = ev.x
                self.stretch_to = self.stretch_start
        else:
            self.stretch_from = 0
            self.stretch_to = 255
        self.histo.queue_draw()
        self.main.queue_draw()

    def histo_draw(self, w, cr):
        width = w.get_allocated_width()
        height = w.get_allocated_height()
        cr.set_source_rgb(0.7, 0.1, 0.1)
        cr.move_to(0, 0)
        cr.line_to(width, 0)
        cr.line_to(width, height)
        cr.line_to(0, height)
        cr.line_to(0, 0)
        cr.stroke()
        if self.stretch_from >= 0:
            cr.set_source_rgb(0.9, 0.6, 0.6)
            cr.rectangle(self.stretch_from, 0,
                         self.stretch_to - self.stretch_from, height)
            cr.fill()
        cr.set_source_rgb(0.1, 0.1, 0.1)
        if self.histo_data is not None:
            xscale = width / 127.0
            yscale = float(height) / np.max(self.histo_data)
            cr.new_path()
            cr.move_to(0, height - 0)
            cr.line_to(0, height - self.histo_data[0] * yscale)
            for i in range(1, 128):
                cr.line_to(i * xscale, height - self.histo_data[i] * yscale)
            cr.line_to(width, height - 0)
            cr.close_path()
            cr.fill()
        else:
            self.centered_text(w, cr, 10, "No histogram data.")

    def add_stretch_from(self, diff):
        self.stretch_from += diff
        if self.stretch_from < 0:
            self.stretch_from = 0
        if self.stretch_from >= self.stretch_to:
            self.stretch_from = self.stretch_to - 1
        self.new_image()

    def add_stretch_to(self, diff):
        self.stretch_to += diff
        if self.stretch_to <= self.stretch_from:
            self.stretch_to = self.stretch_from + 1
        if self.stretch_to > 255:
            self.stretch_to = 255
        self.new_image()

    def mul_box_size(self, inc):
        if inc:
            self.box_size *= 2
        else:
            self.box_size /= 2
        if self.box_size < 2:
            self.box_size = 1
        if self.box_size > 256:
            self.box_size = 256
        self.norm_cross()
        self.main.queue_draw()

    def center_cross(self):
        self.px = self.im_width // 2
        self.py = self.im_height // 2
        self.norm_cross()
        self.main.queue_draw()

    def center_fov(self):
        self.small_x = self.small_width / 2
        self.small_y = self.small_height / 2
        self.calc()
        self.main.queue_draw()
        self.small.queue_draw()


class CamManager(object):

    def __init__(self, consumer):
        self.camera = None
        self.running = False
        self.periodic = None
        self.consumer = consumer
        self.error = None
        self.idx = None
        self.sbox = Gtk.VBox()
        self.ltemp = self.label(self.sbox, "Temp: <b>NA</b>")
        self.lexposure = self.label(self.sbox, "Exp (o-p,k-l): <b>NA</b>")
        self.lgain = self.label(self.sbox, "Gain (q-a,w-s): <b>NA</b>")
        self.lrecording = self.label(self.sbox, "Recording(r): <b>NO</b>")
        self.ldropped = self.label(self.sbox, "Dropped: <b>NA</b>")
        self.lcapfail = self.label(self.sbox, "Cap/Fail: <b>NA</b>")
        self.lFPS = self.label(self.sbox, "FPS: <b>NA</b>")
        self.last_count = -1
        self.last_tick = 0
        self.fps = 0.0
        self.exp = 1000
        self.s = None

    def label(self, box, txt):
        l = Gtk.Label()
        l.set_markup(txt)
        box.pack_start(l, False, False, 0)
        return l

    def open(self, idx):
        try:
            self.camera = ASI.Camera(idx)
            self.idx = idx
            for i, v in enumerate(self.prop()["controls"]):
                if "ensor temperature" in v["Description"]:
                    self.temp_idx = i
                if "Exposure Time" in v["Description"]:
                    self.exp_idx = i
        except IOError as e:
            self.error = str(e)

    def close(self):
        self.error = None
        if self.camera:
            self.camera.close()
            self.camera = None

    def start(self):
        if not self.running and self.camera and not self.error:
            self.running = True
            self.ucaptured = -1
            self.s = self.get()
            self.camera.start()
            self.last_count = -1
            self.hook(self.s["vals"][self.exp_idx])

    def stop(self):
        if self.running:
            self.unhook()
            self.camera.stop()
            self.running = False
            self.last_count = -1

    def get(self):
        return self.camera.stat()

    def prop(self):
        return self.camera.prop()

    def set(self, s):
        try:
            self.camera.set(s)
        except IOError as e:
            return str(e)
        return None

    def auto_exposure(self):
        s = self.camera.stat()
        a = s['auto']
        v = s['vals']
        t = 2
        a[0] = False
        a[1] = False
        v[1] = 1000000
        if "ASI1600" in self.parameters()['Name']:
            print("Fixing USB bandwidth to 95%")
            a[6] = False
            v[6] = 95
        if "ASI120M" in self.parameters()['Name']:
            t = 0
        self.camera.set({'auto': a, 'vals': v, 'type': t})

    def us2s(self, us):
        if us < 1000:
            return "%d us" % us
        elif us < 1000*1000:
            return "%.3f ms" % (us / 1000.0)
        else:
            return "%.3f s" % (us / (1000.0 * 1000.0))

    def calc_fps(self, s):
        now = time.time()
        count = s["captured"]
        if self.last_count < 0:
            self.last_count = count
            self.last_tick = now
        else:
            if ((now - self.last_tick > 1.0) and
                    (count > self.last_count)):
                self.fps = (count - self.last_count) / (now - self.last_tick)
                self.last_count = count
                self.last_tick = now
        return "%.2f" % self.fps

    def update_sbox(self, s):
        self.ltemp.set_markup("Temp: <b>%.1f</b>" % (
            s["vals"][self.temp_idx] / 10.0))
        self.lexposure.set_markup(
            "Exp (o-p,k-l): <b>%s</b>" % self.us2s(s["vals"][1]))
        self.lgain.set_markup(
            "Gain (q-a,w-s): <b>%d</b>" % s["vals"][0])
        if s["recording"]:
            self.lrecording.set_markup("Recording (r): <b>YES</b>")
        else:
            self.lrecording.set_markup("Recording (r): <b>NO</b>")
        self.ldropped.set_markup("Dropped: <b>%d / %d</b>" % (
            s["cam_dropped"], s["dropped"]))
        self.lcapfail.set_markup("Cap/Fail: <b>%d / %d</b>" % (
            s["captured"], s["failed"]))
        self.lFPS.set_markup("FPS: %s" % self.calc_fps(s))

    def get_image(self):
        if not self.periodic:
            return False
        self.s = self.get()
        self.update_sbox(self.s)
        if self.s["ucaptured"] != self.ucaptured:
            start = time.time()
            im = self.camera.get_image()
            self.consumer.new_image(im, self.s["type"], self.s["auto_debayer"])
            self.ucaptured = self.s["ucaptured"]
            duration = (time.time() - start) * 1000
            exp = self.exp
            if duration > exp / 1.2:
                exp = 1.2 * duration
            self.periodic = GLib.timeout_add(exp, self.get_image)
            return False
        return True

    def list(self):
        self.asi_list = ASI.list()
        return self.asi_list

    def parameters(self):
        return self.asi_list[self.idx]

    def hook(self, exp):
        if not self.periodic and self.running and not self.error:
            # To ms, exp is in us
            exp /= 1000
            if exp > 1000:
                exp = 1000
            if exp < 100:
                exp = 100
            self.exp = exp
            self.periodic = GLib.timeout_add(exp, self.get_image)

    def unhook(self):
        if self.periodic:
            GLib.source_remove(self.periodic)
            self.periodic = None

    def run(self, idx):
        self.stop()
        self.close()
        self.open(idx)
        self.auto_exposure()
        self.start()

    def modify(self, what, absolute, delta):
        if self.s is None:
            return
        val = self.s["vals"][what]
        if absolute:
            val += delta
        else:
            val *= delta
        p = self.prop()
        maxv = p["controls"][what]["MaxValue"]
        minv = p["controls"][what]["MinValue"]
        if val > maxv:
            val = maxv
        if val < minv:
            val = minv
        self.s["vals"][what] = int(val)
        self.camera.set({'vals': self.s["vals"]})

    def error_dialog(self, w, e):
        dialog = Gtk.MessageDialog(w, 0, Gtk.MessageType.ERROR,
                                   Gtk.ButtonsType.OK, e)
        dialog.run()
        dialog.destroy()

    def save_parameters(self, w):
        dialog = Gtk.FileChooserDialog(
            "Save parameters", w, Gtk.FileChooserAction.SAVE,
            (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
             Gtk.STOCK_SAVE, Gtk.ResponseType.ACCEPT))
        dialog.set_current_name("default.yaaca")
        response = dialog.run()
        if response == Gtk.ResponseType.ACCEPT:
            with open(dialog.get_filename(), "w") as f:
                json.dump(self.get(), f, sort_keys=True, indent=4,
                          separators=(',', ': '))
        dialog.destroy()

    def load_parameters(self, w):
        dialog = Gtk.FileChooserDialog(
            "Load parameters", w, Gtk.FileChooserAction.OPEN,
            (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
             Gtk.STOCK_OPEN, Gtk.ResponseType.ACCEPT))
        err = None
        response = dialog.run()
        try:
            if response == Gtk.ResponseType.ACCEPT:
                with open(dialog.get_filename(), "r") as f:
                    p = json.load(f)
                    self.stop()
                    e = self.set(p)
                    if e:
                        raise ValueError(e)
                    self.start()
        except ValueError as e:
            err = e
        dialog.destroy()
        if err:
            self.error_dialog(w, err)


class Choicer(object):

    def __init__(self, labels, actuator):
        master = None
        self.value = 0
        self.actuator = actuator
        self.b = []
        self.box = Gtk.HBox()
        for i, e in enumerate(labels):
            item = Gtk.RadioButton(label=e)
            if master is None:
                master = item
            else:
                item.join_group(master)
            item.connect("toggled", lambda w, i=i: self.actuate(w, i))
            self.b.append(item)
            self.box.pack_start(item, False, False, 0)

    def actuate(self, w, val):
        if w.get_active():
            self.value = val
            self.actuator(val)

    def set_text(self, t):
        self.value = int(t)
        self.b[self.value].set_active(True)

    def get_text(self):
        return self.value


class CamSim(object):

    # fname is a glob with images to load.
    def __init__(self, consumer, fname):
        self.sbox = Gtk.VBox()
        self.fname = fname
        self.i = 0
        self.images = glob.glob(fname)
        self.n = len(self.images)
        self.running = False
        self.exp = 1000
        self.consumer = consumer

    def start(self):
        if not self.running:
            self.running = True
            self.periodic = GLib.timeout_add(self.exp, self.get_image)

    def stop(self):
        self.running = False

    def get_image(self):
        if not self.running:
            return False
        print("%d/%d: %s" % (self.i, self.n, self.images[self.i]))
        image = Image.open(self.images[self.i])
        im = np.array(image)
        print(im.shape)
        self.consumer.new_image(im, 1, 0)
        self.i += 1
        if self.i >= self.n:
            self.i = 0
        return True

    def open(self, idx):
        pass

    def close(self):
        pass

    def get(self):
        return {}

    def prop(self):
        return {}

    def set(self, s):
        return None

    def auto_exposure(self):
        pass

    def list(self):
        return [{'Name':'Simulator'}]

    def run(self, idx):
        self.start()

    def modify(self, what, absolute, delta):
        pass

    def save_parameters(self, w):
        pass

    def load_parameters(self, w):
        pass


class DialogMixin(object):

    def _assure_mul(self, w, par, mul):
        if par in w:
            w[par] = w[par] // mul * mul

    def _change_values(self, w=None):
        if w is None:
            w = self._default_change()
        self._assure_mul(w, "width", 8)
        self._assure_mul(w, "height", 2)
        self.camera_.stop()
        try:
            self.camera_.set(w)
        except IOError as e:
                Gtk.MessageDialog(self, 0, Gtk.MessageType.ERROR,
                                  Gtk.ButtonsType.OK, "Cannot set: %s" % e)
                self.update()
        self.camera_.start()
        self.update()

    def _add(self, i, t, e=None, s=None):
        l = Gtk.Label()
        l.set_markup(t)
        l.set_justify(Gtk.Justification.RIGHT)
        l.set_hexpand(True)
        self._grid.attach(l, 0, i, 1, 1)
        if e:
            r = Choicer(e, lambda v: self._setter(s, v))
            self._grid.attach(r.box, 1, i, 1, 1)
        else:
            r = Gtk.Entry()
            self._grid.attach(r, 1, i, 1, 1)
        return r

    def on_destroy(self, w, d):
        self.hide()
        return True


class ROIDialog(Gtk.Dialog, DialogMixin):

    def _setter(self, s, v):
        self._change_values({s: v})

    def _default_change(self):
        return {
            "type": int(self._mode.get_text()),
            "bin": int(self._bin.get_text()),
            "start_x": int(self._start_x.get_text()),
            "start_y": int(self._start_y.get_text()),
            "width": int(self._width.get_text()),
            "height": int(self._height.get_text()),
            "dest": self._path.get_text(),
            }

    def __init__(self, parent, camera):
        Gtk.Dialog.__init__(self,
                            title="ROI / Mode",
                            parent=parent,
                            flags=0)
        self.add_button("R_ecenter ROI", 4)
        self.add_button("_Apply", 1)
        self.add_button("_Reload", 2)
        self.add_button("_Close", 3)

        self.camera_ = camera
        self._par = camera.parameters()
        self._grid = Gtk.Grid()
        self._grid.set_row_homogeneous(True)
        self.prop_ = camera.prop()
        self._mode = self._add(0, "<b>Mode</b>(0-3:RAW8,RGB24,RAW16,Y8)",
                               ["RAW8", "RGB24", "RAW16", "Y8"], "type")
        bint = ["%d" % i for i in self._par["SupportedBins"]]
        self._bin = self._add(1, "<b>Binning</b>(%s)" % ",".join(bint))
        self._start_x = self._add(
            2, "<b>Start X</b>(0-%d)" % self._par["MaxWidth"])
        self._start_y = self._add(
            3, "<b>Start Y</b>(0-%d)" % self._par["MaxHeight"])
        self._width = self._add(
            4, "<b>Width</b>(0-%d)" % self._par["MaxWidth"])
        self._height = self._add(
            5, "<b>Height</b>(0-%d)" % self._par["MaxHeight"])
        self._path = self._add(6, "<b>Save Prefix</b>")
        box = self.get_content_area()
        box.add(self._grid)
        self.connect("response", self.on_response)
        self.connect('delete-event', self.on_destroy)

        button_grid = Gtk.Grid()
        button_grid.set_row_homogeneous(True)
        button_grid.set_column_homogeneous(True)
        self._grid.attach(button_grid, 0, 7, 2, 2)
        for i in range(8):
            bi = (i // 4) + 1
            pa = 4 - (i % 4)
            bwidth = self._par["MaxWidth"] / bi
            width = bwidth * pa / 4
            x = (bwidth - width) / 2
            bheight = self._par["MaxHeight"] / bi
            height = bheight * pa / 4
            y = (bheight - height) / 2
            b = Gtk.Button.new_with_mnemonic(
                "_%d:%dx%dx%d" % (i + 1, width, height, bi))
            s = {
                "bin": bi,
                "start_x": x,
                "start_y": y,
                "width": width,
                "height": height,
                }
            b.connect("clicked", lambda w, s=s: self._change_values(s))
            button_grid.attach(b, i % 4, i // 4, 1, 1)

        self.show_all()

    def update(self):
        s = self.camera_.get()
        self._mode.set_text("%d" % s["type"])
        self._bin.set_text("%d" % s["bin"])
        self._start_x.set_text("%d" % s["start_x"])
        self._start_y.set_text("%d" % s["start_y"])
        self._width.set_text("%d" % s["width"])
        self._height.set_text("%d" % s["height"])
        self._path.set_text(s["dest"])

    def on_response(self, w, e):
        if e == 2:
            self.update()
        elif e == 1:
            self._change_values()
        elif e == 3 or e == -4:
            self.hide()
        elif e == 4:
            self._start_x.set_text("%d" % (
                (self._par["MaxWidth"] - int(self._width.get_text())) / 2))
            self._start_y.set_text("%d" % (
                (self._par["MaxHeight"] - int(self._height.get_text())) / 2))


class SettingsDialog(Gtk.Dialog, DialogMixin):

    def __init__(self, parent, camera):
        Gtk.Dialog.__init__(self,
                            title="Camera Settings",
                            parent=parent,
                            flags=0)
        self.add_button("_Apply", 1)
        self.add_button("_Reload", 2)
        self.add_button("_Close", 3)

        self.camera_ = camera
        self._grid = Gtk.Grid()
        self._grid.set_row_homogeneous(True)
        self.prop_ = camera.prop()
        self.parameters_ = camera.parameters()
        self.ctrl_ = []
        self.auto_ = []
        self.handlers_ = []
        i = 0
        for c in self.prop_["controls"]:
            if c["MinValue"] == 0 and c["MaxValue"] == 1:
                e = self._add(i, "<b>%s</b>" % c["Description"],
                              ["Off", "On"], i)
                self.ctrl_.append(e)
                self.auto_.append(None)
                i = i + 1
                continue
            if "Flip:" in c["Description"]:
                e = self._add(i, "<b>%s</b>" % c["Description"],
                              ["Off", "Horz", "Vert", "Both"], i)
                self.ctrl_.append(e)
                self.auto_.append(None)
                i = i + 1
                continue
            l = Gtk.Label()
            if c["IsWritable"]:
                l.set_markup("<b>%s</b>(%d-%d)" % (
                    c["Description"], c["MinValue"], c["MaxValue"]))
            else:
                l.set_markup("<b>%s</b>" % c["Description"])
            l.set_justify(Gtk.Justification.RIGHT)
            l.set_hexpand(True)
            self._grid.attach(l, 0, i, 1, 1)
            e = Gtk.Entry()
            if c["IsWritable"]:
                self.handlers_.append((
                    e, e.connect("activate", lambda w: self._change_values())))
            else:
                e.set_property("editable", False)
            self.ctrl_.append(e)
            if c["IsAutoSupported"]:
                self._grid.attach(e, 1, i, 1, 1)
                ch = Gtk.CheckButton()
                self.handlers_.append((
                    ch, ch.connect("toggled", lambda w: self._change_values())))
                self._grid.attach(ch, 2, i, 1, 1)
                self.auto_.append(ch)
            else:
                self.auto_.append(None)
                self._grid.attach(e, 1, i, 2, 1)
            i = i + 1
        l = Gtk.Label()
        l.set_markup("Offsets HighestDR: %d, UnityGain: %d, LowestRN: "
                     "%d. Gain LowestRN: %d. ElecPerADU: %f" % (
                         self.prop_["Offset_HighestDR"],
                         self.prop_["Offset_UnityGain"],
                         self.prop_["Offset_LowestRN"],
                         self.prop_["Gain_LowestRN"],
                         self.parameters_["ElecPerADU"]))
        self._grid.attach(l, 0, i, 3, 1)
        box = self.get_content_area()
        box.add(self._grid)
        self.connect("response", self.on_response)
        self.connect('delete-event', self.on_destroy)
        self.show_all()

    def update(self):
        for i, h in self.handlers_:
            GObject.signal_handler_block(i, h)
        s = self.camera_.get()
        for e, a, ie, ia in zip(self.ctrl_, self.auto_, s["vals"], s["auto"]):
            e.set_text("%d" % ie)
            if a:
                a.set_active(ia)
        for i, h in self.handlers_:
            GObject.signal_handler_unblock(i, h)

    def _default_change(self):
        r = {
            "vals": [int(x.get_text()) for x in self.ctrl_],
            "auto": [x and x.get_active() for x in self.auto_]
            }
        return r

    def _setter(self, s, v):
        st = self.camera_.get()
        sc = st["vals"]
        sc[s] = v
        self._change_values({"vals": sc})

    def on_response(self, w, e):
        if e == 2:
            self.update()
        elif e == 1:
            self._change_values()
        elif e == 3 or e == -4:
            self.hide()


class MenuManager(Gtk.MenuBar):

    def _save_snapshot(self, w):
        if self._im.pb is None:
            return
        fname = "yaaca_snap_{:%Y-%m-%d_%H:%M:%S}.jpg".format(
            datetime.datetime.now())
        self._im.pb.savev(fname, "jpeg", (), ())

    def _run_ext(self, cmd):
        if self._im.pb is None:
            return
        fname = tempfile.mktemp()
        self._im.pb.savev(fname, "jpeg", (), ())
        os.system('%s %s &' % (cmd, fname))

    def __init__(self, parent, camera, im, tools):
        self._parent = parent
        self._camera = camera
        self._current = None
        self._im = im
        self._groups = {}
        self._settings = None
        self._roi = None
        Gtk.MenuBar.__init__(self)

        _file_menu = self._add_sub_menu("_File")
        for i, c in enumerate(self._camera.list()):
            self._add_radio(_file_menu, 'cameras',
                            "%d: %s" % (i, c['Name']),
                            lambda w, i=i: self._change_camera(i),
                            False)
        self._add_separator(_file_menu)
        self._add_entry(
            _file_menu, "Save parameters",
            lambda w: self._camera.save_parameters(self._parent))
        self._add_entry(
            _file_menu, "Load parameters",
            lambda w: self._camera.load_parameters(self._parent))
        self._add_separator(_file_menu)
        self._add_entry(_file_menu, "Save Snapshot", self._save_snapshot)
        solver = "/usr/lib/astrolove/solver.sh"
        if os.path.isfile("./solver.sh"):
            solver = "./solver.sh"
        self._add_entry(_file_menu, "Solve", lambda w: self._run_ext(solver))
        self._add_separator(_file_menu)
        self._add_entry(_file_menu, "Quit", Gtk.main_quit)

        _camera_menu = self._add_sub_menu("_Camera")
        self._add_entry(_camera_menu,
                        "Start", lambda _: self._camera.start())
        self._add_entry(_camera_menu,
                        "Stop", lambda _: self._camera.stop())
        self._rec_toggle = self._add_check(
            _camera_menu, "Record", lambda w: self._toggle_int(w, "recording"))
        self._add_check(_camera_menu, "Long Exposure mode",
                        lambda w: self._toggle_int(w, "mode"))
        self._add_entry(_camera_menu,
                        "Settings", self._show_settings)
        self._add_entry(_camera_menu,
                        "ROI/Mode", self._show_roi)

        _view_menu = self._add_sub_menu("_View")
        self._zoom_radio = []
        for c in [4, 2, 1, 0.5, 0.25]:
            active = False
            if c == 1:
                active = True
                self._im.set_zoom(c)
            self._add_radio(_view_menu, 'zooms',
                            "Zoom %sx" % c,
                            lambda w, c=c: self._im.set_zoom(c),
                            active)

        self._add_separator(_view_menu)
        self._add_radio(_view_menu, 'debayer', "Raw",
                        lambda w: self._set_int("auto_debayer", 0), True)
        self._add_radio(_view_menu, 'debayer', "Full debayer",
                        lambda w: self._set_int("auto_debayer", 1), False)
        self._add_radio(_view_menu, 'debayer', "Fast debayer",
                        lambda w: self._set_int("auto_debayer", 2), False)

        self._add_separator(_view_menu)
        self._add_check(_view_menu, "Cross",
                        lambda w: self._im.set_cross(w.get_active()))
        self._add_entry(_view_menu, "Center Cross",
                        lambda w: self._im.center_cross())
        self._add_entry(_view_menu, "Center FoV",
                        lambda w: self._im.center_fov())
        self._add_check(_view_menu, "Histogram",
                        lambda w: self._im.set_histo(w.get_active()))

        self._add_separator(_view_menu)
        self._do_saa = self._add_check(
            _view_menu, "SAA",
            lambda w: self._im.set_saa(w.get_active()))
        self._add_entry(_view_menu, "Reset SAA",
                        lambda w: self._im.reset_saa())
        self._sub_dark = self._add_check(
            _view_menu, "Add Dark", lambda w: self._im.do_add_dark(
                w.get_active()))
        self._add_entry(_view_menu, "Reset Dark",
                        lambda w: self._im.reset_dark())
        self._gamma_stretch = self._add_check(
            _view_menu, "Gamma Stretch", lambda w: self._im.do_gamma_stretch(
                w.get_active()))

        self._add_separator(_view_menu)
        self._add_radio(
            _view_menu, 'disp_mode', "Show Processed",
            lambda w: self._set_disp_mode("Show Processed"), True)
        self._add_radio(
            _view_menu, 'disp_mode', "Show SAA/Dark",
            lambda w: self._set_disp_mode("Show SAA/Dark"), False)
        self._add_radio(
            _view_menu, 'disp_mode', "Show Raw",
            lambda w: self._set_disp_mode("Show Raw"), False)

        _tools_menu = self._add_sub_menu("_Tools")
        self._tools_active = -1
        self._tools = tools
        self._add_radio(_tools_menu, 'tools_menu', 'None',
                        lambda w: self._tool_activate(-1), True)
        for i, c in enumerate(tools):
            self._add_radio(_tools_menu, 'tools_menu', c.menu_name(),
                            lambda w: self._tool_activate(i), False)

        self.show_all

    def _set_disp_mode(self, disp_mode):
        if disp_mode == "Show SAA/Dark":
            self._im.show_saa_dark(True)
            self._im.show_fast(False)
        elif disp_mode == "Show Raw":
            self._im.show_saa_dark(False)
            self._im.show_fast(True)
        else:
            self._im.show_saa_dark(False)
            self._im.show_fast(False)

    def _do_toggle(self, w):
        w.set_active(not w.get_active())

    def _reset_all(self, w):
        self._do_saa.set_active(False)
        self._add_dark.set_active(False)
        self._sub_dark.set_active(False)
        self._im.reset_all()

    def _set_int(self, s, val):
        self._camera.stop()
        self._camera.set({s: val})
        self._camera.start()

    def _toggle_int(self, w, s):
        if w.get_active():
            val = 1
        else:
            val = 0
        self._camera.stop()
        self._camera.set({s: val})
        self._camera.start()

    def _change_camera(self, i):
        if self._current != i:
            if self._settings:
                self._settings.destroy()
                self._settings = None
            if self._roi:
                self._roi.destroy()
                self._roit = None
            self._camera.run(i)
            self._current = i

    def _add_sub_menu(self, name):
        sub = Gtk.MenuItem.new_with_mnemonic(name)
        self.append(sub)
        menu = Gtk.Menu()
        sub.set_submenu(menu)
        return menu

    def _add_entry(self, menu, name, command):
        item = Gtk.MenuItem(label=name)
        item.connect("activate", command)
        menu.append(item)

    def _add_check(self, menu, name, command, on=False):
        item = Gtk.CheckMenuItem(label=name)
        if on:
            item.set_active(True)
        item.connect("activate", command)
        menu.append(item)
        return item

    def _add_radio(self, menu, group, name, command, active):
        item = Gtk.RadioMenuItem(
            label=name, group=self._groups.get(group, None))
        self._groups[group] = item
        item.connect("activate", command)
        if active:
            item.set_active(active)
        menu.append(item)

    def _add_separator(self, menu):
        menu.append(Gtk.SeparatorMenuItem())

    def _show_settings(self, w):
        if not self._settings:
            self._settings = SettingsDialog(self._parent, self._camera)
        self._settings.update()
        self._settings.show()

    def _show_roi(self, i):
        if not self._roi:
            self._roi = ROIDialog(self._parent, self._camera)
        self._roi.update()
        self._roi.show()

    def _tool_activate(self, i):
        if self._tools_active != -1:
            self._tools[i].deactivate()
            self._im.set_hook(None)
        if i != -1:
            self._tools[i].activate()
            self._im.set_hook(self._tools[i].process_image)
        self._tools_active = i


class ToolDialog(Gtk.Dialog, DialogMixin):

    def __init__(self, parent, name):
        super().__init__(self,
                         title=name,
                         parent=parent,
                         flags=0)
        self.add_button("_Apply", 1)
        self.add_button("_Close", 2)
        self._grid = Gtk.Grid()
        self._grid.set_row_homogeneous(True)
        box = self.get_content_area()
        box.add(self._grid)
        self.connect("response", self.on_response)
        self.connect('delete-event', self.on_destroy)

    def ready(self):
        self.update()
        self.show_all()

    def on_response(self, w, e):
        if e == 1:
            self.commit()
        self.update()
        if e == 2:
            self.hide()

    def update(self):
        pass

    def commit(self):
        pass


class HiContrastDialog(ToolDialog):

    def __init__(self, parent, hc):
        self._hc = hc
        super().__init__(parent, "Hi Contrast")
        self._rep = self._add(
            0, "No of BANDS (2-256)")
        self.ready()

    def update(self):
        self._rep.set_text("%d" % self._hc.get())

    def commit(self):
        try:
            self._hc.update(int(self._rep.get_text()))
        except ValueError:
            pass


class ToolBox:

    def __init__(self, parent, container_box):
        self._parent = parent
        self._dialog = None
        self._container_box = container_box
        self._box = Gtk.VBox()
        self.label = Gtk.Label()
        self._box.pack_start(Gtk.VSeparator(), False, False, 0)
        self._box.pack_start(self.label, False, False, 0)
        b = Gtk.Button.new_with_mnemonic("Configure")
        b.connect("clicked", self.dialog)
        self._box.pack_start(b, False, False, 0)

    def dialog(self, w):
        if not self._dialog:
            self._dialog = HiContrastDialog(self._parent, self)
        self._dialog.update()
        self._dialog.show()

    def activate(self):
        self._container_box.pack_start(self._box, False, False, 0)
        self._container_box.show_all()

    def deactivate(self):
        self._container_box.remove(self._box)


class HiContrast(ToolBox):

    def __init__(self, parent, container_box):
        super().__init__(parent, container_box)
        self.update(2)

    def menu_name(self):
        return "Hi Contrast"

    def get(self):
        return self._v

    def update(self, val):
        self._v = int(val)
        if self._v < 2:
            self._v = 2
        if self._v > 256:
            self._v = 256
        step = 255.0 / (self._v - 1)
        x = 0.0
        self._m = np.arange(0, 256, dtype=np.uint8)
        self._m[0] = 0
        for i in range(1, 256):
            x += step
            if x > 255.0:
                x = 0.0
            self._m[i] = int(round(x))
        self.label.set_markup("Hi Contrast: %d" % self._v)

    def process_image(self, im, imtype, auto_debayer):
        im = self._m[im]
        return im, imtype, auto_debayer


class Mainwindow(Gtk.Window):

    def _handle_key(self, w, ev):
        if ev.string == 'q':
            self.camera.modify(0, True, 1)
        elif ev.string == 'a':
            self.camera.modify(0, True, -1)
        elif ev.string == 'w':
            self.camera.modify(0, True, 5)
        elif ev.string == 's':
            self.camera.modify(0, True, -5)
        elif ev.string == 'o':
            self.camera.modify(1, False, 0.9)
        elif ev.string == 'p':
            self.camera.modify(1, False, 1.1)
        elif ev.string == 'k':
            self.camera.modify(1, False, 0.5)
        elif ev.string == 'l':
            self.camera.modify(1, False, 2.0)
        elif ev.string == 'z':
            self.imman.add_stretch_from(-5)
        elif ev.string == 'x':
            self.imman.add_stretch_from(5)
        elif ev.string == 'c':
            self.imman.add_stretch_to(-5)
        elif ev.string == 'v':
            self.imman.add_stretch_to(5)
        elif ev.string == 'b':
            self.imman.mul_box_size(False)
        elif ev.string == 'n':
            self.imman.mul_box_size(True)
        elif ev.string == 'r':
            self.menu._do_toggle(self.menu._rec_toggle)
        elif ev.keyval in self._arrows:
            self.camera.camera.pulse(self._arrows[ev.keyval], True)

    def _handle_key_release(self, w, ev):
        if ev.keyval in self._arrows:
            self.camera.camera.pulse(self._arrows[ev.keyval], False)

    def __init__(self, *args, **kwargs):
        Gtk.Window.__init__(
            self, title="YAACA", default_width=800,
            default_height=600, *args, **kwargs)
        self.imman = ImageManager()

        if os.getenv("YAACA_SIM"):
            self.camera = CamSim(self.imman, os.getenv("YAACA_SIM"))
        else:
            self.camera = CamManager(self.imman)

        self.tool_box = Gtk.VBox()
        self.tools = (
            HiContrast(self, self.tool_box),
        )
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.add(box)
        self.menu = MenuManager(self, self.camera, self.imman, self.tools)
        box.pack_start(self.menu, False, False, 0)

        main_box = Gtk.HBox()
        box.pack_start(main_box, True, True, 0)
        main_box.pack_start(self.imman.main, True, True, 0)
        main_box.pack_start(Gtk.VSeparator(), False, False, 1)

        rvbox = Gtk.VBox(width_request=256)
        main_box.pack_start(rvbox, False, False, 0)
        rvbox.pack_start(self.imman.small, False, False, 0)
        rvbox.pack_start(self.camera.sbox, False, False, 0)
        rvbox.pack_start(self.imman.histo_box, False, False, 0)
        rvbox.pack_start(self.imman.info_box, False, False, 0)
        rvbox.pack_start(self.tool_box, False, False, 0)

        self.connect("key_press_event", self._handle_key)
        self.connect("key_release_event", self._handle_key_release)
        self._arrows = {
            Gdk.KEY_Up: ASI.N,
            Gdk.KEY_Down: ASI.S,
            Gdk.KEY_Left: ASI.E,
            Gdk.KEY_Right: ASI.W}

        self.connect("delete-event", Gtk.main_quit)
        self.show_all()


window = Mainwindow()
Gtk.main()
