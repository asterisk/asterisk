#!/usr/bin/env python
# vim: set ts=8 sw=4 sts=4 et ai tw=79:
'''
Usage: ./spandspflow2pcap.py SPANDSP_LOG SENDFAX_PCAP

Takes a log from Asterisk with SpanDSP, extracts the "received" data
and puts it in a pcap file. Use 'fax set debug on' and configure
logger.conf to get fax logs.

Input data should look something like this::

    [2013-08-07 15:17:34] FAX[23479] res_fax.c: FLOW T.38 Rx     5: IFP c0 ...

Output data will look like a valid pcap file ;-)

This allows you to reconstruct received faxes into replayable pcaps.

Replaying is expected to be done by SIPp with sipp-sendfax.xml. The
SIPp binary used for replaying must have image (fax) support. This means
you'll need a version higher than 3.5.0 (unreleased when writing this),
or the git master branch: https://github.com/SIPp/sipp


Author: Walter Doekes, OSSO B.V. (2013,2015,2016,2019)
License: Public Domain
'''
from base64 import b16decode
from collections import namedtuple
from datetime import datetime, timedelta
from re import search
from time import mktime
from struct import pack
import os
import sys


LOSSY = False
EMPTY_RECOVERY = False


IFP = namedtuple('IFP', 'date seqno data')  # datetime, int, bytearray


def n2b(text):
    """
    Convert "aa bb cc" to bytearray('\xaa\xbb\xcc').
    """
    return bytearray(
        b16decode(text.replace(' ', '').replace('\n', '').upper()))


class SkipPacket(Exception):
    pass


class FaxPcap(object):
    PCAP_PREAMBLE = n2b(
        'd4 c3 b2 a1 02 00 04 00'
        '00 00 00 00 00 00 00 00'
        'ff ff 00 00 71 00 00 00')

    def __init__(self, outfile):
        self.outfile = outfile
        self.date = None
        self.seqno = None
        self.udpseqno = 128
        self.prev_data = None

        # Only do this if at pos 0?

    def add(self, ifp):
        """
        Add the IFP packet.

        T.38 basic format of UDPTL payload section with redundancy:

        UDPTL_SEQNO
        - 2 sequence number (big endian)
        UDPTL_PRIMARY_PAYLOAD (T30?)
        - 1 subpacket length (excluding this byte)
        - 1 type of message (e.g. 0xd0 for data(?))
        - 1 items in data field (e.g. 0x01)
        - 2 length of data (big endian)
        - N data
        RECOVERY (optional)
        - 2 count of previous seqno packets (big endian)
        - N UDPTL_PRIMARY_PAYLOAD of (seqno-1)
        - N UDPTL_PRIMARY_PAYLOAD of (seqno-2)
        - ...
        """
        # First packet?
        if self.seqno is None:
            # Add preamble.
            self._add_preamble()
            # Start a second late (optional).
            self._add_garbage(ifp.date)

            # Set sequence, and fill with missing leading zeroes.
            self.seqno = 0
            for i in range(ifp.seqno):
                self.add(IFP(date=ifp.date, seqno=i, data=bytearray([0])))

        # Auto-increasing dates
        if self.date is None or ifp.date > self.date:
            self.date = ifp.date
        elif ifp.date < self.date.replace(microsecond=0):
            assert False, 'More packets than expected in 1s? {!r}/{!r}'.format(
                ifp.date, self.date)
        else:
            self.date += timedelta(microseconds=9000)

        # Add packet.
        self.seqno = ifp.seqno
        try:
            self.outfile.write(self._make_packet(ifp.data))
        except SkipPacket:
            pass

    def _add_preamble(self):
        self.outfile.write(self.PCAP_PREAMBLE)

    def _add_garbage(self, date):
        if self.date is None or date > self.date:
            self.date = date

        self.seqno = 0xffff
        self.outfile.write(self._make_packet(
            bytearray(b'GARBAGE'), is_ifp=False))

    def _make_packet(self, ifp_data, is_ifp=True):
        sum16 = bytearray(b'\x43\x21')  # the OS fixes the checksums for us

        data = bytearray()
        if is_ifp:
            data.append(len(ifp_data))  # length
            data.extend(ifp_data)       # data
            self.prev_data, prev_data = data[:], self.prev_data
        else:
            data.extend(ifp_data)
            prev_data = None

        if prev_data:
            if LOSSY and (self.seqno % 3) == 2:
                self.udpseqno += 1
                raise SkipPacket()

            if EMPTY_RECOVERY:
                # struct ast_frame f[16], we have room for a few
                # packets.
                packets = 14
                data.extend([0, packets + 1] + [0] * packets)
                data.extend(prev_data)
            else:
                # Add 1 previous packet, without the seqno.
                data.extend([0, 1])
                data.extend(prev_data)

        # Wrap it in UDP
        udp = bytearray(
            b'\x00\x01\x00\x02%(len)s%(sum16)s%(seqno)s%(data)s' % {
                b'len': pack('>H', len(data) + 10),
                b'sum16': sum16,
                b'seqno': pack('>H', self.seqno),
                b'data': data})

        # Wrap it in IP
        ip = bytearray(
            b'\x45\xb8%(len)s%(udpseqno)s\x00\x00\xf9\x11%(sum16)s'
            b'\x01\x01\x01\x01\x02\x02\x02\x02%(udp)s' % {
                b'len': pack('>H', len(udp) + 20),
                b'udpseqno': pack('>H', self.udpseqno),
                b'sum16': sum16,
                b'udp': udp})

        # Wrap it in Ethernet
        ethernet = bytearray(
            b'\x00\x00\x00\x01\x00\x06\x00\x30\x48\xb1\x1c\x34\x00\x00'
            b'\x08\x00%(ip)s' % {b'ip': ip})

        # Wrap it in a pcap packet
        packet = bytearray(b'%(prelude)s%(ethernet)s' % {
            b'prelude': pack(
                '<IIII', int(mktime(self.date.timetuple())),
                self.date.microsecond, len(ethernet), len(ethernet)),
            b'ethernet': ethernet})

        # Increase values.
        self.udpseqno += 1

        return packet


class SpandspLog:
    def __init__(self, fp):
        self._fp = fp

    def __iter__(self):
        r"""
        Looks for lines line:

            [2013-08-07 15:17:34] FAX[23479] res_fax.c: \
              FLOW T.38 Rx     5: IFP c0 01 80 00 00 ff

        And yields:

            IFP(date=..., seqno=..., data=...)
        """
        prev_seqno = None

        for lineno, line in enumerate(self._fp):
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

            if prev_seqno is not None:
                # Expected all sequence numbers. But you can safely disable
                # this check.
                assert seqno == prev_seqno + 1, '%s+1 != %s' % (
                    seqno, prev_seqno)
                pass
            prev_seqno = seqno

            yield IFP(date=date, seqno=seqno, data=data)


def main(logname, pcapname):
    with open(sys.argv[1], 'r') as infile:
        log = SpandspLog(infile)

        # with open(sys.argv[2], 'xb') as outfile:  # py3 exclusive write, bin
        create_or_fail = os.O_CREAT | os.O_EXCL | os.O_WRONLY
        try:
            fd = os.open(sys.argv[2], create_or_fail, 0o600)
        except Exception:
            raise
        else:
            with os.fdopen(fd, 'wb') as outfile:
                pcap = FaxPcap(outfile)
                for data in log:
                    pcap.add(data)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.stderr.write('Usage: {} LOGFILE PCAP\n'.format(sys.argv[0]))
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
