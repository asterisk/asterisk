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


def parse_line(line, processpointers):
    """Parse out a line into its constituent parts.

    Keyword Arguments:
    line The line from a ref debug log to parse out

    Returns:
    A dictionary containing the options, or None
    """
    tokens = line.strip().split(',', 8)
    if len(tokens) < 9:
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
                      'ptr': tokens[7],
                      'tag': tokens[8],
                      }

    line_obj = {
        'addr': processed_line['addr'],
        'delta': processed_line['delta'],
        'tag': processed_line['tag'],
        'ptr': processed_line['ptr'],
        'state': processed_line['state'],
        'text': "[%s] %s:%s %s" % (processed_line['thread_id'],
                                   processed_line['file'],
                                   processed_line['line'],
                                   processed_line['function']),
        'is_ptr': False
    }

    if line_obj['ptr'] != '(nil)':
        line_obj['is_ptr'] = processpointers
        line_obj['ptr'] = ' ptr:%s' % line_obj['ptr']
    else:
        line_obj['ptr'] = ''

    return line_obj


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
    byptr = {}
    filename = options.filepath

    with open(filename, 'r') as ref_file:
        for txtline in ref_file:
            line = parsed_line = parse_line(txtline, options.processpointers)
            if not line:
                continue

            invalid = False
            obj = line['addr']

            if obj not in current_objects:
                currobj = {'log': [], 'curcount': 1, 'size': "unknown"}
                current_objects[obj] = currobj
                if 'constructor' in line['state']:
                    currobj['size'] = line['state'].split("**")[2]
                    # This is the normal expected case
                    pass
                elif 'invalid' in line['state']:
                    invalid = True
                    currobj['curcount'] = 0
                    if options.invalid:
                        invalid_objects.append((obj, currobj))
                elif 'destructor' in line['state']:
                    currobj['curcount'] = 0
                    if options.skewed:
                        skewed_objects.append((obj, currobj))
                else:
                    currobj['curcount'] = int(line['state'])
                    if options.skewed:
                        skewed_objects.append((obj, currobj))
            else:
                currobj = current_objects[obj]
                currobj['curcount'] += int(line['delta'])

            if line['is_ptr']:
                ptr = line['ptr']
                if line['delta'] == "-1":
                    if ptr in byptr and byptr[ptr]['addr'] == obj:
                        currobj['log'].remove(byptr[ptr])
                        del byptr[ptr]
                    else:
                        currobj['log'].append(line)
                elif line['delta'] == "+1":
                    byptr[ptr] = line
                    currobj['log'].append(line)
            else:
                currobj['log'].append(line)

            # It is possible for curcount to go below zero if someone
            # unrefs an object by two or more when there aren't that
            # many refs remaining.  This condition abnormally finishes
            # the object.
            if currobj['curcount'] <= 0:
                if currobj['curcount'] < 0:
                    # Highlight the abnormally finished object in the
                    # invalid section as well as reporting it in the normal
                    # finished section.
                    if options.invalid:
                        invalid_objects.append((obj, currobj))
                if not invalid and options.normal:
                    finished_objects.append((obj, currobj))
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
        print "==== %s Object %s (%s bytes) history ====" % (prefix, obj[0], obj[1]['size'])
        current_count = 0;
        for line in obj[1]['log']:
            print "%s: %s%s %s - [%d]" % (line['text'], line['delta'], line['ptr'], line['tag'], current_count)
            current_count += int(line['delta'])
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
    parser.add_option("--ignorepointers", action="store_false",
                      dest="processpointers", default=True,
                      help="If specified, don't check for matching pointers in unrefs")

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
