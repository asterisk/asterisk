#!/usr/bin/env python
# vim: set ts=8 sw=4 sts=4 et ai tw=79:
'''
Usage: ./spandspflow2pcap.py SPANDSP_LOG SENDFAX_PCAP

Takes a log from Asterisk with SpanDSP, extracts the "received" data
and puts it in a pcap file. Use 'fax set debug on' and configure
logger.conf to get fax logs.

Input data should look something like this::

    [2013-08-07 15:17:34] FAX[23479] res_fax.c: FLOW T.38 Rx     5: IFP c0 01 ...

Output data will look like a valid pcap file ;-)

This allows you to reconstruct received faxes into replayable pcaps.

Replaying is expected to be done by SIPp with sipp-sendfax.xml. The
SIPp binary used for replaying must have image (fax) support. This means
you'll need a version higher than 3.5.0 (unreleased when writing this),
or the git master branch: https://github.com/SIPp/sipp


Author: Walter Doekes, OSSO B.V. (2013,2015,2016)
License: Public Domain
'''
from base64 import b16decode
from datetime import datetime, timedelta
from re import search
from time import mktime
from struct import pack
import sys


LOSSY = False
EMPTY_RECOVERY = False


def n2b(text):
    return b16decode(text.replace(' ', '').replace('\n', '').upper())


class FaxPcap(object):
    PCAP_PREAMBLE = n2b('d4 c3 b2 a1 02 00 04 00'
                        '00 00 00 00 00 00 00 00'
                        'ff ff 00 00 71 00 00 00')

    def __init__(self, outfile):
        self.outfile = outfile
        self.date = None
        self.dateoff = timedelta(seconds=0)
        self.seqno = None
        self.udpseqno = 128
        self.prev_data = None

        # Only do this if at pos 0?
        self.outfile.write(self.PCAP_PREAMBLE)

    def data2packet(self, date, udpseqno, seqno, data, prev_data):
        sum16 = '\x43\x21'  # checksum is irrelevant for sipp sending

        new_prev = data  # without seqno..
        data = '%s%s' % (pack('>H', seqno), data)
        if prev_data:
            if LOSSY and (seqno % 3) == 2:
                return '', new_prev
            if EMPTY_RECOVERY:
                # struct ast_frame f[16], we have room for a few
                # packets.
                packets = 14
                data += '\x00%c%s%s' % (
                    chr(packets + 1), '\x00' * packets, prev_data)
            else:
                # Add 1 previous packet, without the seqno.
                data += '\x00\x01' + prev_data

        kwargs = {'udpseqno': pack('>H', udpseqno), 'sum16': sum16}

        kwargs['data'] = data
        kwargs['lenb16'] = pack('>H', len(kwargs['data']) + 8)
        udp = '\x00\x01\x00\x02%(lenb16)s%(sum16)s%(data)s' % kwargs

        kwargs['data'] = udp
        kwargs['lenb16'] = pack('>H', len(kwargs['data']) + 20)
        ip = ('\x45\xb8%(lenb16)s%(udpseqno)s\x00\x00\xf9\x11%(sum16)s\x01'
              '\x01\x01\x01\x02\x02\x02\x02%(data)s') % kwargs

        kwargs['data'] = ip
        frame = ('\x00\x00\x00\x01\x00\x06\x00\x30\x48\xb1\x1c\x34\x00\x00'
                 '\x08\x00%(data)s') % kwargs

        kwargs['data'] = frame
        sec = mktime(date.timetuple())
        msec = date.microsecond
        datalen = len(kwargs['data'])
        kwargs['pre'] = pack('<IIII', sec, msec, datalen, datalen)
        packet = '%(pre)s%(data)s' % kwargs

        return (packet, new_prev)

    def add(self, date, seqno, data):
        if self.seqno is None:
            self.seqno = 0
            for i in range(seqno):
                # In case the first zeroes were dropped, add them.
                self.add(date, i, '\x00')
        assert seqno == self.seqno, '%s != %s' % (seqno, self.seqno)

        # Data is prepended by len(data).
        data = chr(len(data)) + data

        # Auto-increasing dates
        if self.date is None or date > self.date:
            # print 'date is larger', date, self.date
            self.date = date
        elif (date < self.date.replace(microsecond=0)):
            assert False, ('We increased too fast.. decrease delta: %r/%r' %
                           (date, self.date))
        else:
            self.date += timedelta(microseconds=9000)

        print(seqno, '\t', self.date + self.dateoff)

        # Make packet.
        packet, prev_data = self.data2packet(self.date + self.dateoff,
                                             self.udpseqno, self.seqno,
                                             data, self.prev_data)
        self.outfile.write(packet)

        # Increase values.
        self.udpseqno += 1
        self.seqno += 1
        self.prev_data = prev_data

    def add_garbage(self, date):
        if self.date is None or date > self.date:
            self.date = date

        packet, ignored = self.data2packet(self.date, self.udpseqno,
                                           0xffff, 'GARBAGE', '')
        self.udpseqno += 1

        self.outfile.write(packet)


with open(sys.argv[1], 'r') as infile:
    with open(sys.argv[2], 'wb') as outfile:
        first = True
        p = FaxPcap(outfile)
        # p.add(datetime.now(), 0, n2b('06'))
        # p.add(datetime.now(), 1, n2b('c0 01 80 00 00 ff'))

        for lineno, line in enumerate(infile):
            # Look for lines like:
            # [2013-08-07 15:17:34] FAX[23479] res_fax.c: \
            #   FLOW T.38 Rx     5: IFP c0 01 80 00 00 ff
            if 'FLOW T.38 Rx' not in line:
                continue
            if 'IFP' not in line:
                continue

            match = search(r'(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)', line)
            assert match
            date = datetime(*[int(i) for i in match.groups()])

            match = search(r'Rx\s*(\d+):', line)
            assert match
            seqno = int(match.groups()[0])

            match = search(r': IFP ([0-9a-f ]+)', line)
            assert match
            data = n2b(match.groups()[0])

            # Have the file start a second early.
            if first:
                p.add_garbage(date)
                first = False

            # Add the packets.
            #
            # T.38 basic format of UDPTL payload section with redundancy:
            #
            # UDPTL_SEQNO
            # - 2 sequence number (big endian)
            # UDPTL_PRIMARY_PAYLOAD (T30?)
            # - 1 subpacket length (excluding this byte)
            # - 1 type of message (e.g. 0xd0 for data(?))
            # - 1 items in data field (e.g. 0x01)
            # - 2 length of data (big endian)
            # - N data
            # RECOVERY (optional)
            # - 2 count of previous seqno packets (big endian)
            # - N UDPTL_PRIMARY_PAYLOAD of (seqno-1)
            # - N UDPTL_PRIMARY_PAYLOAD of (seqno-2)
            # - ...
            #
            p.add(date, seqno, data)
