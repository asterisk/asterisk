#!/usr/bin/env python
"""Process a ref debug log

 This file will process a log file created by the REF_DEBUG
 build option in Asterisk.

 See http://www.asterisk.org for more information about
 the Asterisk project. Please do not directly contact
 any of the maintainers of this project for assistance;
 the project provides a web site, mailing lists and IRC
 channels for your use.

 This program is free software, distributed under the terms of
 the GNU General Public License Version 2. See the LICENSE file
 at the top of the source tree.

 Copyright (C) 2014, Digium, Inc.
 Matt Jordan <mjordan@digium.com>
"""

import sys
import os

from optparse import OptionParser

def parse_line(line):
    """Parse out a line into its constituent parts.

    Keyword Arguments:
    line The line from a ref debug log to parse out

    Returns:
    A dictionary containing the options, or None
    """
    tokens = line.strip().split(',', 7)
    if len(tokens) < 8:
        print "ERROR: ref debug line '%s' contains fewer tokens than " \
              "expected: %d" % (line.strip(), len(tokens))
        return None

    processed_line = {'addr': tokens[0],
                      'delta': tokens[1],
                      'thread_id': tokens[2],
                      'file': tokens[3],
                      'line': tokens[4],
                      'function': tokens[5],
                      'state': tokens[6],
                      'tag': tokens[7],
                     }
    return processed_line


def process_file(filename):
    """The routine that kicks off processing a ref file

    Keyword Arguments:
    filename The full path to the file to process

    Returns:
    A tuple containing:
        - A list of objects whose lifetimes were completed
        - A list of objects whose lifetimes were not completed
        - A list of objects whose lifetimes are skewed
    """

    finished_objects = []
    leaked_objects = []
    skewed_objects = []
    current_objects = {}

    with open(filename, 'r') as ref_file:
        for line in ref_file:
            parsed_line = parse_line(line)
            if not parsed_line:
                continue

            obj = parsed_line['addr']

            if obj not in current_objects:
                current_objects[obj] = []
                if 'constructor' not in parsed_line['state']:
                    skewed_objects.append(current_objects[obj])
            current_objects[obj].append(parsed_line)

            if 'destructor' in parsed_line['state']:
                lifetime = current_objects.get(obj)
                finished_objects.append(lifetime)
                del current_objects[obj]

    leaked_objects = current_objects.values()
    return (finished_objects, leaked_objects, skewed_objects)


def print_objects(objects, prefix=""):
    """Prints out the objects that were processed

    Keyword Arguments:
    objects A list of objects to print
    prefix  A prefix to print that specifies something about
            this object
    """

    print "======== %s Objects ========" % prefix
    print "\n"
    for obj in objects:
        print "==== %s Object %s history ====" % (prefix, obj[0]['addr'])
        for entry in obj:
            print "[%s] %s:%s %s: %s %s - [%s]" % (entry['thread_id'],
                                              entry['file'], entry['line'],
                                              entry['function'],
                                              entry['delta'], entry['tag'],
                                              entry['state'])
        print "\n"


def main(argv=None):
    """Main entry point for the script"""

    ret_code = 0

    if argv is None:
        argv = sys.argv

    parser = OptionParser()

    parser.add_option("-f", "--file", action="store", type="string",
                      dest="filepath", default="/var/log/asterisk/refs",
                      help="The full path to the refs file to process")
    parser.add_option("-l", "--suppress-leaks", action="store_false",
                      dest="leaks", default=True,
                      help="If specified, don't output leaked objects")
    parser.add_option("-n", "--suppress-normal", action="store_false",
                      dest="normal", default=True,
                      help="If specified, don't output objects with a " \
                           "complete lifetime")
    parser.add_option("-s", "--suppress-skewed", action="store_false",
                      dest="skewed", default=True,
                      help="If specified, don't output objects with a " \
                           "skewed lifetime")

    (options, args) = parser.parse_args(argv)

    if not os.path.isfile(options.filepath):
        print >>sys.stderr, "File not found: %s" % options.filepath
        return -1

    try:
        (finished_objects,
         leaked_objects,
         skewed_objects) = process_file(options.filepath)

        if options.leaks and len(leaked_objects):
            print_objects(leaked_objects, "Leaked")
            ret_code |= 1

        if options.skewed and len(skewed_objects):
            print_objects(skewed_objects, "Skewed")
            ret_code |= 2

        if options.normal:
            print_objects(finished_objects, "Finalized")

    except (KeyboardInterrupt, SystemExit, IOError):
        print >>sys.stderr, "File processing cancelled"
        return -1

    return ret_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
