#!/usr/bin/env python
"""Process a ref debug log

 This file will process a log file created by enabling
 the refdebug config option in asterisk.conf.

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


def process_file(options):
    """The routine that kicks off processing a ref file

    Keyword Arguments:
    filename The full path to the file to process

    Returns:
    A tuple containing:
        - A list of objects whose lifetimes were completed
            (i.e., finished objects)
        - A list of objects referenced after destruction
            (i.e., invalid objects)
        - A list of objects whose lifetimes were not completed
            (i.e., leaked objects)
        - A list of objects whose lifetimes are skewed
            (i.e., Object history starting with an unusual ref count)
    """

    finished_objects = []
    invalid_objects = []
    leaked_objects = []
    skewed_objects = []
    current_objects = {}
    filename = options.filepath

    with open(filename, 'r') as ref_file:
        for line in ref_file:
            parsed_line = parse_line(line)
            if not parsed_line:
                continue

            invalid = False
            obj = parsed_line['addr']

            if obj not in current_objects:
                current_objects[obj] = {'log': [], 'curcount': 1}
                if 'constructor' in parsed_line['state']:
                    # This is the normal expected case
                    pass
                elif 'invalid' in parsed_line['state']:
                    invalid = True
                    current_objects[obj]['curcount'] = 0
                    if options.invalid:
                        invalid_objects.append((obj, current_objects[obj]))
                elif 'destructor' in parsed_line['state']:
                    current_objects[obj]['curcount'] = 0
                    if options.skewed:
                        skewed_objects.append((obj, current_objects[obj]))
                else:
                    current_objects[obj]['curcount'] = int(
                        parsed_line['state'])
                    if options.skewed:
                        skewed_objects.append((obj, current_objects[obj]))
            else:
                current_objects[obj]['curcount'] += int(parsed_line['delta'])

            current_objects[obj]['log'].append(
                "[%s] %s:%s %s: %s %s - [%s]" % (
                    parsed_line['thread_id'],
                    parsed_line['file'],
                    parsed_line['line'],
                    parsed_line['function'],
                    parsed_line['delta'],
                    parsed_line['tag'],
                    parsed_line['state']))

            # It is possible for curcount to go below zero if someone
            # unrefs an object by two or more when there aren't that
            # many refs remaining.  This condition abnormally finishes
            # the object.
            if current_objects[obj]['curcount'] <= 0:
                if current_objects[obj]['curcount'] < 0:
                    current_objects[obj]['log'].append(
                        "[%s] %s:%s %s: %s %s - [%s]" % (
                            parsed_line['thread_id'],
                            parsed_line['file'],
                            parsed_line['line'],
                            parsed_line['function'],
                            "+0",
                            "Object abnormally finalized",
                            "**implied destructor**"))
                    # Highlight the abnormally finished object in the
                    # invalid section as well as reporting it in the normal
                    # finished section.
                    if options.invalid:
                        invalid_objects.append((obj, current_objects[obj]))
                if not invalid and options.normal:
                    finished_objects.append((obj, current_objects[obj]))
                del current_objects[obj]

    if options.leaks:
        for key, lines in current_objects.iteritems():
            leaked_objects.append((key, lines))
    return (finished_objects, invalid_objects, leaked_objects, skewed_objects)


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
        print "==== %s Object %s history ====" % (prefix, obj[0])
        for line in obj[1]['log']:
            print line
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
    parser.add_option("-i", "--suppress-invalid", action="store_false",
                      dest="invalid", default=True,
                      help="If specified, don't output invalid object "
                           "references")
    parser.add_option("-l", "--suppress-leaks", action="store_false",
                      dest="leaks", default=True,
                      help="If specified, don't output leaked objects")
    parser.add_option("-n", "--suppress-normal", action="store_false",
                      dest="normal", default=True,
                      help="If specified, don't output objects with a "
                           "complete lifetime")
    parser.add_option("-s", "--suppress-skewed", action="store_false",
                      dest="skewed", default=True,
                      help="If specified, don't output objects with a "
                           "skewed lifetime")

    (options, args) = parser.parse_args(argv)

    if not options.invalid and not options.leaks and not options.normal \
            and not options.skewed:
        print >>sys.stderr, "All options disabled"
        return -1

    if not os.path.isfile(options.filepath):
        print >>sys.stderr, "File not found: %s" % options.filepath
        return -1

    try:
        (finished_objects,
         invalid_objects,
         leaked_objects,
         skewed_objects) = process_file(options)

        if options.invalid and len(invalid_objects):
            print_objects(invalid_objects, "Invalid Referenced")
            ret_code |= 4

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
