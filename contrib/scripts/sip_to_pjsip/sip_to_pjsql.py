#!/usr/bin/env python

from sip_to_pjsip import convert
import sip_to_pjsip
import optparse


import sqlconfigparser


def write_pjsip(filename, pjsip, non_mappings):
    """
    Write pjsip.sql file to disk
    """
    try:
        with open(filename, 'wt') as fp:
            pjsip.write(fp)

    except IOError:
        print("Could not open file " + filename + " for writing")

def cli_options():
    """
    Parse command line options and apply them. If invalid input is given,
    print usage information

    """
    global user
    global password
    global host
    global port
    global database
    global table

    usage = "usage: %prog [options] [input-file [output-file]]\n\n" \
        "Converts the chan_sip configuration input-file to mysql output-file.\n" \
        "The input-file defaults to 'sip.conf'.\n" \
        "The output-file defaults to 'pjsip.sql'."
    parser = optparse.OptionParser(usage=usage)
    parser.add_option('-u', '--user', dest='user', default="root",
                      help='mysql username')
    parser.add_option('-p', '--password', dest='password', default="root",
                      help='mysql password')
    parser.add_option('-H', '--host', dest='host', default="127.0.0.1",
                      help='mysql host ip')
    parser.add_option('-P', '--port', dest='port', default="3306",
                      help='mysql port number')
    parser.add_option('-D', '--database', dest='database', default="asterisk",
                      help='mysql port number')
    parser.add_option('-t', '--table', dest='table', default="sippeers",
                      help='name of sip realtime peers table')

    options, args = parser.parse_args()

    user = options.user
    password = options.password
    host = options.host
    port = options.port
    database = options.database
    table = options.table

    sip_filename = args[0] if len(args) else 'sip.conf'
    pjsip_filename = args[1] if len(args) == 2 else 'pjsip.sql'

    return sip_filename, pjsip_filename

if __name__ == "__main__":
    sip_filename, pjsip_filename = cli_options()
    sip = sqlconfigparser.SqlConfigParser(table)
    sip_to_pjsip.sip = sip
    sip.connect(user,password,host,port,database)
    print('Please, report any issue at:')
    print('    https://issues.asterisk.org/')
    print('Reading ' + sip_filename)
    sip.read(sip_filename)
    print('Converting to PJSIP realtime sql...')
    pjsip, non_mappings = convert(sip, pjsip_filename, dict(), False)
    print('Writing ' + pjsip_filename)
    write_pjsip(pjsip_filename, pjsip, non_mappings)

