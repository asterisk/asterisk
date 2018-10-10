#!/usr/bin/env python
"""Process a ref debug log for lock usage

 This file will process a log file created by Asterisk
 that was compiled with REF_DEBUG and DEBUG_THREADS.

 See http://www.asterisk.org for more information about
 the Asterisk project. Please do not directly contact
 any of the maintainers of this project for assistance;
 the project provides a web site, mailing lists and IRC
 channels for your use.

 This program is free software, distributed under the terms of
 the GNU General Public License Version 2. See the LICENSE file
 at the top of the source tree.

 Copyright (C) 2018, CFWare, LLC
 Corey Farrell <git@cfware.com>
"""

from __future__ import print_function
import sys
import os

from optparse import OptionParser


def process_file(options):
    """The routine that kicks off processing a ref file"""

    object_types = {}
    objects = {}
    filename = options.filepath

    with open(filename, 'r') as ref_file:
        for line in ref_file:
            if 'constructor' not in line and 'destructor' not in line:
                continue
            # The line format is:
            # addr,delta,thread_id,file,line,function,state,tag
            # Only addr, file, line, function, state are used by reflocks.py
            tokens = line.strip().split(',', 7)
            addr = tokens[0]
            state = tokens[6]
            if 'constructor' in state:
                obj_type = '%s:%s:%s' % (tokens[3], tokens[4], tokens[5])
                if obj_type not in object_types:
                    object_types[obj_type] = {
                        'used': 0,
                        'unused': 0,
                        'none': 0
                    }
                objects[addr] = obj_type
            elif 'destructor' in state:
                if addr not in objects:
                    # This error would be reported by refcounter.py.
                    continue
                obj_type = objects[addr]
                del objects[addr]
                if '**lock-state:unused**' in state:
                    object_types[obj_type]['unused'] += 1
                elif '**lock-state:used**' in state:
                    object_types[obj_type]['used'] += 1
                elif '**lock-state:none**' in state:
                    object_types[obj_type]['none'] += 1

    for (allocator, info) in object_types.items():
        stats = [];
        if info['used'] > 0:
            if not options.used:
                continue
            stats.append("%d used" % info['used'])
        if info['unused'] > 0:
            stats.append("%d unused" % info['unused'])
        if info['none'] > 0 and options.none:
            stats.append("%d none" % info['none'])
        if len(stats) == 0:
            continue
        print("%s: %s" % (allocator, ', '.join(stats)))


def main(argv=None):
    """Main entry point for the script"""

    ret_code = 0

    if argv is None:
        argv = sys.argv

    parser = OptionParser()

    parser.add_option("-f", "--file", action="store", type="string",
                      dest="filepath", default="/var/log/asterisk/refs",
                      help="The full path to the refs file to process")
    parser.add_option("-u", "--suppress-used", action="store_false",
                      dest="used", default=True,
                      help="Don't output types that have used locks.")
    parser.add_option("-n", "--show-none", action="store_true",
                      dest="none", default=False,
                      help="Show counts of objects with no locking.")

    (options, args) = parser.parse_args(argv)

    if not os.path.isfile(options.filepath):
        print("File not found: %s" % options.filepath, file=sys.stderr)
        return -1

    try:
        process_file(options)
    except (KeyboardInterrupt, SystemExit, IOError):
        print("File processing cancelled", file=sys.stderr)
        return -1

    return ret_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
