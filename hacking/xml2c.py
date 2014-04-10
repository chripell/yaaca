#!/usr/bin/env python

import sys
import xml.etree.ElementTree as ET

tree = ET.parse(sys.argv[1])
root = tree.getroot()

def get_field(p, n):
    for f in p.iter("field"):
        if f.attrib["name"]  == n:
            return f.attrib["show"]

regs = {}
with open("registers.h", 'r') as fp:
    for line in fp:
        a = line.split()
        if len(a) >= 2:
            regs[int(a[2],16)] = a[1]

first = True
for packet in root:
    if get_field(packet, "usb.bmRequestType") == "0x40":
        ds = get_field(packet, "usb.data")
        dts = get_field(packet, "frame.time_delta")
        dt = float(dts)
        if dt >= 0.01 and not first:
            print "sleep_ms(%d);"%(dt * 1000 + 1)
        d = [int(x, 16) for x in ds.split(":")]
        if d[0] == 0xa6:
            rv = d[1] + d[2] * 256
            rr = regs.get(rv, hex(rv))
            print "set_reg(%s, 0x%04x);"%(rr, d[3] + d[4] * 256)
        else:
            print "send_ctrl(0x%02x);"%(d[0])
        first = False
