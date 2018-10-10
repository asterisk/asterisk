#!/usr/bin/env python
"""Process a ref debug log for memory usage

 This will provide information about total and peak
 allocations.

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


def create_stats():
    """Create statistics object"""
    return {
        'count': 0,
        'overhead': 0,
        'user_data': 0,
        'totalmem': 0
    }


def update_stats(current, peak, total, key, direction, delta):
    """Update statistics objects"""

    if direction == 1:
        total[key] += delta

    delta *= direction
    current[key] += delta
    if current[key] > peak[key]:
        peak[key] = current[key]


def process_file(options):
    """The routine that kicks off processing a ref file"""

    current = create_stats()
    total = create_stats()
    peak = create_stats()
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
                split_state = state.split("**")
                if len(split_state) < 4:
                    print("File does not contain object size information", file=sys.stderr)
                    sys.exit(1)

                obj_type = '%s:%s:%s' % (tokens[3], tokens[4], tokens[5])
                if obj_type not in object_types:
                    object_types[obj_type] = {
                        'used': 0,
                        'unused': 0,
                        'none': 0
                    }
                overhead = int(split_state[2])
                user_data = int(split_state[3])
                obj = objects[addr] = {
                    'overhead': overhead,
                    'user_data': user_data,
                    'obj_type': obj_type
                }

                direction = 1
            else:
                if addr not in objects:
                    # This error would be reported by refcounter.py.
                    continue
                obj = objects[addr]
                del objects[addr]
                direction = -1
                obj_type = obj['obj_type']
                if '**lock-state:unused**' in state:
                    object_types[obj_type]['unused'] += 1
                elif '**lock-state:used**' in state:
                    object_types[obj_type]['used'] += 1

            # Increment current and peak usage
            update_stats(current, peak, total, 'count', direction, 1)
            update_stats(current, peak, total, 'overhead', direction, obj['overhead'])
            update_stats(current, peak, total, 'user_data', direction, obj['user_data'])
            update_stats(current, peak, total, 'totalmem', direction, obj['overhead'] + obj['user_data'])

    print("Total usage statistics:")
    print("%20s: %d" % ("Count", total['count']))
    print("%20s: %d" % ("Total Memory (k)", total['totalmem'] / 1024))
    print("%20s: %d (%.2f%%)" % ("Overhead (k)", total['overhead'] / 1024, total['overhead'] * 100.0 / total['totalmem']))
    print("%20s: %d" % ("User Data (k)", total['user_data'] / 1024))
    print("")
    print("Peak usage statistics:")
    print("%20s: %d" % ("Count", peak['count']))
    print("%20s: %d" % ("Total Memory (k)", peak['totalmem'] / 1024))
    print("%20s: %d (%.2f%%)" % ("Overhead (k)", peak['overhead'] / 1024, peak['overhead'] * 100.0 / peak['totalmem']))
    print("%20s: %d" % ("User Data (k)", peak['user_data'] / 1024))
    print("")

    lockbyobj = {'used': 0, 'total': 0}
    lockbytype = {'used': 0, 'total': 0}
    for (allocator, info) in object_types.items():
        lockbyobj['used'] += info['used']
        lockbyobj['total'] += info['used'] + info['unused']

        if info['used'] != 0:
            lockbytype['used'] += 1
        elif info['unused'] == 0:
            # This object type doesn't have locking.
            continue
        lockbytype['total'] += 1

    print("Lock usage statistics:")
    print("%20s: %d of %d used (%.2f%%)" % (
        "By object",
        lockbyobj['used'],
        lockbyobj['total'],
        lockbyobj['used'] * 100.0 / lockbyobj['total']))
    print("%20s: %d of %d used (%.2f%%)" % (
        "By type",
        lockbytype['used'],
        lockbytype['total'],
        lockbytype['used'] * 100.0 / lockbytype['total']))


def main(argv=None):
    """Main entry point for the script"""

    ret_code = 0

    if argv is None:
        argv = sys.argv

    parser = OptionParser()

    parser.add_option("-f", "--file", action="store", type="string",
                      dest="filepath", default="/var/log/asterisk/refs",
                      help="The full path to the refs file to process")

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
