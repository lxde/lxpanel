#!/usr/bin/env python
# -*- coding: UTF-8 -*-

import os, re, argparse, glob, gtk


parser = argparse.ArgumentParser()
parser.add_argument("width", help="destination width", type=int)
parser.add_argument("height", help="destination height", type=int)
args = parser.parse_args()
svg_files = glob.glob("*.svg")
for svg_file in svg_files:
    pixbuf = gtk.gdk.pixbuf_new_from_file(svg_file)
    new_pixbuf = pixbuf.scale_simple(args.width, args.height, gtk.gdk.INTERP_HYPER)
    new_pixbuf.save(svg_file[:-3] + "png" , "png")
