#!/usr/bin/env python
# -*- coding: UTF-8 -*-

import os, re, argparse, ConfigParser


parser = argparse.ArgumentParser()
parser.add_argument("file_cfg", help="file .cfg to sort")
args = parser.parse_args()
if not os.path.isfile(args.file_cfg):
    print "ERROR: The path %s is not valid" % args.file_cfg
    exit(1)
config_in = ConfigParser.RawConfigParser()
config_in.read(args.file_cfg)
config_out = ConfigParser.RawConfigParser()
for section in config_in.sections():
    config_out.add_section(section)
    keylist_sorted = sorted(config_in.items(section), key=lambda t: t[0].lower())
    for keyval in keylist_sorted:
        config_out.set(section, keyval[0], keyval[1])
with open(args.file_cfg + ".sorted", 'wb') as fd_out_sorted:
    config_out.write(fd_out_sorted)
