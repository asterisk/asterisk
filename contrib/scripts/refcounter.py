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
import subprocess

from optparse import OptionParser


class AO2Ref:
    def __init__(self, tokens, manager):
        (addr, delta, thread_id,
         fname, line, func,
         state, self.ptr, self.tag) = tokens

        self.is_indirect = False
        self.is_ptr = self.ptr != '(nil)'
        self.is_bad_ptr = False
        self.delta = int(delta)

        if not self.is_ptr:
            self.ptr = ''

        self.ao2object = manager.ao2_find_or_create(addr, self.delta, state)

        if self.is_ptr and manager.options.processpointers and self.delta < 0:
            orig = self.ao2object.unsetRefByPtr(self, self.ptr)
            if not orig:
                self.is_bad_ptr = True
                print "Error: Pointer %s did not contain object %s" % (
                    self.ptr, addr)
                self.ao2object.log(self)
            else:
                self.ao2object.filterLog(orig)
        else:
            if self.is_ptr and manager.options.processpointers:
                self.ao2object.setRefByPtr(self, self.ptr)
            self.ao2object.log(self)

        # text contains everything before ptr, tag and current refcount
        self.text = "[%s] %s:%s %s: %s" % (thread_id, fname, line, func, delta)
        self.ao2object.addDelta(self.delta)

    def finalize(self, manager):
        ptr_txt = ''
        if self.is_ptr:
            (offsetTo, offset) = manager.ao2_find_offset(self.ptr)

            if offsetTo is None:
                ptr_txt = ' ptr:%s' % self.ptr
            else:
                ptr_txt = ' offset:%s[%d]' % (offsetTo.addr, offset)
                self.is_indirect = True

            if self.is_bad_ptr:
                ptr_txt = " bad%s" % ptr_txt

        self.text = "%s%s %s" % (self.text, ptr_txt, self.tag)

    def isSameObject(self, ref):
        return self.ao2object == ref.ao2object

    def directDelta(self):
        if self.is_indirect:
            return 0

        return self.delta

    def printRef(self, refs):
        print "%s - [%d]" % (self.text, refs)
        return self.delta


class AO2Object:
    def __init__(self, manager, addr, delta, state):
        self.manager = manager
        self._log = []
        self.addr = addr
        self.addrNum = int(addr, 16)
        self.startrefcount = 0
        self.invalid = False
        self.skewed = False
        self.size = 'unknown'
        self.refbyptr = {}
        if state.startswith('**constructor'):
            self.size = int(state.split("**")[2])
            self.manager.setCurrent(addr, self)
        elif state.startswith('**invalid'):
            self.setInvalid()
        elif state.startswith('**destructor'):
            # the object is skewed but not leaked
            self.startrefcount = -delta
            self.setSkewed()
        else:
            self.startrefcount = int(state)
            self.manager.setCurrent(addr, self)
            self.setSkewed()

        self.refcount = self.startrefcount

    def log(self, ref):
        self._log.append(ref)

    def filterLog(self, ref):
        self._log.remove(ref)

    def addDelta(self, delta):
        self.refcount += delta
        if self.refcount <= 0:
            if self.refcount < 0:
                self.setInvalid()
            self.manager.setFinished(self.addr, self.invalid)

    def getOffset(self, addrNum):
        if self.size == 'unknown' or self.size <= 0:
            return -1

        offset = addrNum - self.addrNum
        if offset < 0 or offset >= self.size:
            return -1

        return offset

    def directRefs(self):
        count = self.startrefcount
        for obj in self._log:
            count += obj.directDelta()
        return count

    def finalize(self):
        for ref in self._log:
            ref.finalize(self.manager)

    def setInvalid(self):
        if not self.invalid:
            self.invalid = True
            self.manager.setInvalid(self)

    def setSkewed(self):
        if not self.skewed:
            self.skewed = True
            self.manager.setSkewed(self)

    def setRefByPtr(self, ref, ptr):
        if ptr not in self.refbyptr:
            self.refbyptr[ptr] = [ref]
        else:
            self.refbyptr[ptr].append(ref)

    def unsetRefByPtr(self, unref, ptr):
        if ptr in self.refbyptr:
            ptrobj = self.refbyptr[ptr]
            for ref in ptrobj:
                if ref.isSameObject(unref):
                    ptrobj.remove(ref)
                    if len(ptrobj) == 0:
                        del self.refbyptr[ptr]
                    return ref

        return None

    def printObj(self, prefix):
        print "==== %s Object %s (%s bytes) history ====" % (
            prefix, self.addr, self.size)
        refs = 0
        for ref in self._log:
            refs += ref.printRef(refs)
        print "\n"


class ObjectManager:
    def __init__(self, options):
        self.options = options
        self.finished = []
        self.invalid = []
        self.skewed = []
        self.leaked = []
        self.indirect = []
        self.current = {}

    def setFinished(self, addr, invalid):
        if not invalid and self.options.normal:
            self.finished.append(self.current[addr])
        del self.current[addr]

    def setCurrent(self, addr, obj):
        self.current[addr] = obj

    def setInvalid(self, obj):
        if self.options.invalid:
            self.invalid.append(obj)

    def setSkewed(self, obj):
        if self.options.skewed:
            self.skewed.append(obj)

    def ao2_find_or_create(self, addr, delta, state):
        if addr in self.current:
            obj = self.current[addr]
            return obj

        return AO2Object(self, addr, delta, state)

    def ao2_find_offset(self, addr):
        addrNum = int(addr, 16)

        for obj in self.current:
            offset = self.current[obj].getOffset(addrNum)
            if offset >= 0:
                return (self.current[obj], offset)

        return (None, None)

    def _lineToTokens(self, txtline):
        tokens = txtline.strip().split(',', 8)

        if len(tokens) < 9:
            print >>sys.stderr, "ERROR: ref debug line '%s' contains fewer " \
                  "tokens than expected: %d" % (txtline.strip(), len(tokens))
            sys.exit(-1)

        return tokens

    def process(self):
        self.obj_filter = {}
        if self.options.preprocess and not self.options.normal:
            # Get list of objects that leak.  This has the potential to miss certain
            # skewed or invalid objects so it's disabled by default.
            with open(self.options.filepath, 'r') as ref_file:
                for txtline in ref_file:
                    (addr, delta) = self._lineToTokens(txtline)[:2]

                    if addr not in self.obj_filter:
                        self.obj_filter[addr] = 0;

                    self.obj_filter[addr] += int(delta)

                    if self.obj_filter[addr] == 0:
                        del self.obj_filter[addr]

        if self.options.preprocess and not self.obj_filter:
            # Preprocess is enabled and determined no objects leaked.
            return

        with open(self.options.filepath, 'r') as ref_file:
            for txtline in ref_file:
                tokens = self._lineToTokens(txtline)
                addr = tokens[0]
                if not self.options.preprocess or addr in self.obj_filter:
                    AO2Ref(tokens, self)

        for key, obj in self.current.iteritems():
            obj.finalize()

        for key, obj in self.current.iteritems():
            if obj.directRefs() == 0:
                if self.options.indirect:
                    self.indirect.append(obj)
            elif self.options.leaks:
                self.leaked.append(obj)

        self.current = {}

    def __print_objects(self, objects, prefix=""):
        """Prints out the objects that were processed

        Keyword Arguments:
        objects A list of objects to print
        prefix  A prefix to print that specifies something about
                this object
        """

        print "======== %s Objects ========" % prefix
        print "\n"
        for obj in objects:
            obj.printObj(prefix)

    def printReport(self):
        ret_code = 0

        if self.options.invalid and len(self.invalid):
            self.__print_objects(self.invalid, "Invalid Referenced")
            ret_code |= 4

        if self.options.leaks and len(self.leaked):
            self.__print_objects(self.leaked, "Leaked")
            ret_code |= 1

        if self.options.indirect and len(self.indirect):
            self.__print_objects(self.indirect, "Indirectly Leaked")
            ret_code |= 1

        if self.options.skewed and len(self.skewed):
            self.__print_objects(self.skewed, "Skewed")
            ret_code |= 2

        if self.options.normal:
            self.__print_objects(self.finished, "Finalized")

        return ret_code


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
    parser.add_option("", "--suppress-indirect", action="store_false",
                      dest="indirect", default=True,
                      help="If specified don't output objects without "
                           "direct leaks.")
    parser.add_option("--ignorepointers", action="store_false",
                      dest="processpointers", default=True,
                      help="If specified, don't check for matching "
                           "pointers in unrefs")
    parser.add_option("--preprocess", action="store_true",
                      dest="preprocess", default=False,
                      help="Perform two-pass processing.  Ignored "
                           "unless normal objects are suppressed.  "
                           "This option may cause invalid or skewed "
                           "objects to be missed.")

    (options, args) = parser.parse_args(argv)

    if not options.invalid and not options.leaks and not options.normal \
            and not options.skewed and not options.indirect:
        print >>sys.stderr, "All options disabled"
        return -1

    if not os.path.isfile(options.filepath):
        print >>sys.stderr, "File not found: %s" % options.filepath
        return -1

    try:
        proc = ObjectManager(options)
        proc.process()
        ret_code = proc.printReport()

    except (KeyboardInterrupt, SystemExit, IOError):
        print >>sys.stderr, "File processing cancelled"
        return -1

    return ret_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
