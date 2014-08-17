#!/usr/bin/env python
# -*- coding: UTF-8 -*-

import os, re, argparse


parser = argparse.ArgumentParser()
parser.add_argument("file_man", help="file .man to convert to .cfg")
args = parser.parse_args()
if not os.path.isfile(args.file_man):
    print "ERROR: The path %s is not valid" % args.file_man
    exit(1)
with open(args.file_man + ".cfg", "a") as fd_out:
    with open(args.file_man, "r") as fd_in:
        for input_line in fd_in:
            input_line = input_line.strip()
            m = re.search("^(\S\S+)\s\s\s+(.+)", input_line, re.DOTALL)
            if m: output_line = m.group(1) + "=" + m.group(2) + "\n"
            else: output_line = input_line + "\n"
            fd_out.write(output_line)
