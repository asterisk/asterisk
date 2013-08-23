#!/usr/bin/python

###############################################################################
# TODO:
# (1) There is more work to do here, at least for the sip.conf items that
#     aren't currently parsed. An issue will be created for that.
# (2) All of the scripts should probably be passed through pylint and have
#     as many PEP8 issues fixed as possible
# (3) A public review is probably warranted at that point of the entire script
###############################################################################

import optparse
import astdicts
import astconfigparser

PREFIX = 'res_sip_'

###############################################################################
### some utility functions
###############################################################################
def section_by_type(section, res_sip, type):
    """Finds a section based upon the given type, adding it if not found."""
    try:
        return astconfigparser.find_dict(
            res_sip.section(section), 'type', type)
    except LookupError:
        # section for type doesn't exist, so add
        sect = res_sip.add_section(section)
        sect['type'] = type
        return sect

def set_value(key=None, val=None, section=None, res_sip=None,
              nmapped=None, type='endpoint'):
    """Sets the key to the value within the section in res_sip.conf"""
    def _set_value(k, v, s, r, n):
        set_value(key if key else k, v, s, r, n, type)

    # if no value or section return the set_value
    # function with the enclosed key and type
    if not val and not section:
        return _set_value

    # otherwise try to set the value
    section_by_type(section, res_sip, type)[key] = \
        val[0] if isinstance(val, list) else val

def merge_value(key=None, val=None, section=None, res_sip=None,
                nmapped=None, type='endpoint', section_to=None):
    """Merge values from the given section with those from the default."""
    def _merge_value(k, v, s, r, n):
        merge_value(key if key else k, v, s, r, n, type, section_to)

    # if no value or section return the merge_value
    # function with the enclosed key and type
    if not val and not section:
        return _merge_value

    # should return a single value section list
    sect = sip.section(section)[0]
    # for each merged value add it to res_sip.conf
    for i in sect.get_merged(key):
        set_value(key, i, section_to if section_to else section,
                  res_sip, nmapped, type)

def is_in(s, sub):
    """Returns true if 'sub' is in 's'"""
    return s.find(sub) != -1

def non_mapped(nmapped):
    def _non_mapped(section, key, val):
        """Writes a non-mapped value from sip.conf to the non-mapped object."""
        if section not in nmapped:
            nmapped[section] = astconfigparser.Section()
            if isinstance(val, list):
                for v in val:
                    # since coming from sip.conf we can assume
                    # single section lists
                    nmapped[section][0][key] = v
            else:
                nmapped[section][0][key] = val
    return _non_mapped

###############################################################################
### mapping functions -
###      define f(key, val, section) where key/val are the key/value pair to
###      write to given section in res_sip.conf
###############################################################################

def set_dtmfmode(key, val, section, res_sip, nmapped):
    """Sets the dtmfmode value.  If value matches allowable option in res_sip
       then map it, otherwise set it to none.
    """
    # available res_sip.conf values: frc4733, inband, info, none
    if val != 'inband' or val != 'info':
        nmapped(section, key, val + " ; did not fully map - set to none")
        val = 'none'
    set_value(key, val, section, res_sip, nmapped)

def from_nat(key, val, section, res_sip, nmapped):
    """Sets values from nat into the appropriate res_sip.conf options."""
    # nat from sip.conf can be comma separated list of values:
    # yes/no, [auto_]force_rport, [auto_]comedia
    if is_in(val, 'yes'):
        set_value('rtp_symmetric', 'yes', section, res_sip, nmapped)
        set_value('rewrite_contact', 'yes', section, res_sip, nmapped)
    if is_in(val, 'comedia'):
        set_value('rtp_symmetric', 'yes', section, res_sip, nmapped)
    if is_in(val, 'force_rport'):
        set_value('force_rport', 'yes', section, res_sip, nmapped)
        set_value('rewrite_contact', 'yes', section, res_sip, nmapped)

def set_timers(key, val, section, res_sip, nmapped):
    """Sets the timers in res_sip.conf from the session-timers option
       found in sip.conf.
    """
    # res_sip.conf values can be yes/no, required, always
    if val == 'originate':
        set_value('timers', 'always', section, res_sip, nmapped)
    elif val == 'accept':
        set_value('timers', 'required', section, res_sip, nmapped)
    elif val == 'never':
        set_value('timers', 'no', section, res_sip, nmapped)
    else:
        set_value('timers', 'yes', section, res_sip, nmapped)

def set_direct_media(key, val, section, res_sip, nmapped):
    """Maps values from the sip.conf comma separated direct_media option
       into res_sip.conf direct_media options.
    """
    if is_in(val, 'yes'):
        set_value('direct_media', 'yes', section, res_sip, nmapped)
    if is_in(val, 'update'):
        set_value('direct_media_method', 'update', section, res_sip, nmapped)
    if is_in(val, 'outgoing'):
        set_value('directed_media_glare_mitigation', 'outgoing', section, res_sip, nmapped)
    if is_in(val, 'nonat'):
        set_value('disable_directed_media_on_nat','yes', section, res_sip, nmapped)
    if (val == 'no'):
        set_value('direct_media', 'no', section, res_sip, nmapped)

def from_sendrpid(key, val, section, res_sip, nmapped):
    """Sets the send_rpid/pai values in res_sip.conf."""
    if val == 'yes' or val == 'rpid':
        set_value('send_rpid', 'yes', section, res_sip, nmapped)
    elif val == 'pai':
        set_value('send_pai', 'yes', section, res_sip, nmapped)

def set_media_encryption(key, val, section, res_sip, nmapped):
    """Sets the media_encryption value in res_sip.conf"""
    if val == 'yes':
        set_value('media_encryption', 'sdes', section, res_sip, nmapped)

def from_recordfeature(key, val, section, res_sip, nmapped):
    """If record on/off feature is set to automixmon then set
       one_touch_recording, otherwise it can't be mapped.
    """
    if val == 'automixmon':
        set_value('one_touch_recording', 'yes', section, res_sip, nmapped)
    else:
        nmapped(section, key, val + " ; could not be fully mapped")

def from_progressinband(key, val, section, res_sip, nmapped):
    """Sets the inband_progress value in res_sip.conf"""
    # progressinband can = yes/no/never
    if val == 'never':
        val = 'no'
    set_value('inband_progress', val, section, res_sip, nmapped)

def from_host(key, val, section, res_sip, nmapped):
    """Sets contact info in an AOR section in in res_sip.conf using 'host'
       data from sip.conf
    """
    # all aors have the same name as the endpoint so makes
    # it easy to endpoint's 'aors' value
    set_value('aors', section, section, res_sip, nmapped)
    if val != 'dynamic':
        set_value('contact', val, section, res_sip, nmapped, 'aor')
    else:
        set_value('max_contacts', 1, section, res_sip, nmapped, 'aor')

def from_subscribemwi(key, val, section, res_sip, nmapped):
    """Checks the subscribemwi value in sip.conf.  If yes places the
       mailbox value in mailboxes within the endpoint, otherwise puts
       it in the aor.
    """
    mailboxes = sip.get('mailbox', section, res_sip)
    type = 'endpoint' if val == 'yes' else 'aor'
    set_value('mailboxes', mailboxes, section, res_sip, nmapped, type)

###############################################################################

# options in res_sip.conf on an endpoint that have no sip.conf equivalent:
# type, rtp_ipv6, 100rel, trust_id_outbound, aggregate_mwi,
# connected_line_method

# known sip.conf peer keys that can be mapped to a res_sip.conf section/key
peer_map = [
    # sip.conf option      mapping function     res_sip.conf option(s)
    ###########################################################################
    ['context',            set_value],
    ['dtmfmode',           set_dtmfmode],
    ['disallow',           merge_value],
    ['allow',              merge_value],
    ['nat',                from_nat],            # rtp_symmetric, force_rport,
                                                 # rewrite_contact
    ['icesupport',         set_value('ice_support')],
    ['autoframing',        set_value('use_ptime')],
    ['outboundproxy',      set_value('outbound_proxy')],
    ['mohsuggest',         set_value],
    ['session-timers',     set_timers],          # timers
    ['session-minse',      set_value('timers_min_se')],
    ['session-expires',    set_value('timers_sess_expires')],
    ['externip',           set_value('external_media_address')],
    ['externhost',         set_value('external_media_address')],
    # identify_by ?
    ['directmedia',        set_direct_media],    # direct_media
                                                 # direct_media_method
                                                 # directed_media_glare_mitigation
                                                 # disable_directed_media_on_nat
    ['callerid',           set_value],           # callerid
    ['callingpres',        set_value('callerid_privacy')],
    ['cid_tag',            set_value('callerid_tag')],
    ['trustpid',           set_value('trust_id_inbound')],
    ['sendrpid',           from_sendrpid],       # send_pai, send_rpid
    ['send_diversion',     set_value],
    ['encrpytion',         set_media_encryption],
    ['use_avpf',           set_value],
    ['recordonfeature',    from_recordfeature],  # automixon
    ['recordofffeature',   from_recordfeature],  # automixon
    ['progressinband',     from_progressinband], # in_band_progress
    ['callgroup',          set_value],
    ['pickupgroup',        set_value],
    ['namedcallgroup',     set_value],
    ['namedpickupgroup',   set_value],
    ['busylevel',          set_value('devicestate_busy_at')],

############################ maps to an aor ###################################

    ['host',               from_host],           # contact, max_contacts
    ['subscribemwi',       from_subscribemwi],   # mailboxes
    ['qualifyfreq',        set_value('qualify_frequency', type='aor')],

############################# maps to auth#####################################
#        type = auth
#        username
#        password
#        md5_cred
#        realm
#        nonce_lifetime
#        auth_type
######################### maps to acl/security ################################

    ['permit',             merge_value(type='security', section_to='acl')],
    ['deny',               merge_value(type='security', section_to='acl')],
    ['acl',                merge_value(type='security', section_to='acl')],
    ['contactpermit',      merge_value(type='security', section_to='acl')],
    ['contactdeny',        merge_value(type='security', section_to='acl')],
    ['contactacl',         merge_value(type='security', section_to='acl')],

########################### maps to transport #################################
#        type = transport
#        protocol
#        bind
#        async_operations
#        ca_list_file
#        cert_file
#        privkey_file
#        password
#        external_signaling_address - externip & externhost
#        external_signaling_port
#        external_media_address
#        domain
#        verify_server
#        verify_client
#        require_client_cert
#        method
#        cipher
#        localnet
######################### maps to domain_alias ################################
#        type = domain_alias
#        domain
######################### maps to registration ################################
#        type = registration
#        server_uri
#        client_uri
#        contact_user
#        transport
#        outbound_proxy
#        expiration
#        retry_interval
#        max_retries
#        auth_rejection_permanent
#        outbound_auth
########################### maps to identify ##################################
#        type = identify
#        endpoint
#        match
]

def map_peer(sip, section, res_sip, nmapped):
    for i in peer_map:
        try:
            # coming from sip.conf the values should mostly be a list with a
            # single value.  In the few cases that they are not a specialized
            # function (see merge_value) is used to retrieve the values.
            i[1](i[0], sip.get(section, i[0])[0], section, res_sip, nmapped)
        except LookupError:
            pass # key not found in sip.conf

def find_non_mapped(sections, nmapped):
    for section, sect in sections.iteritems():
        try:
            # since we are pulling from sip.conf this should always
            # be a single value list
            sect = sect[0]
            # loop through the section and store any values that were not mapped
            for key in sect.keys(True):
                for i in peer_map:
                    if i[0] == key:
                        break;
                else:
                    nmapped(section, key, sect[key])
        except LookupError:
            pass

def convert(sip, filename, non_mappings):
    res_sip = astconfigparser.MultiOrderedConfigParser()
    non_mappings[filename] = astdicts.MultiOrderedDict()
    nmapped = non_mapped(non_mappings[filename])
    for section in sip.sections():
        if section == 'authentication':
            pass
        else:
            map_peer(sip, section, res_sip, nmapped)

    find_non_mapped(sip.defaults(), nmapped)
    find_non_mapped(sip.sections(), nmapped)

    for key, val in sip.includes().iteritems():
        res_sip.add_include(PREFIX + key, convert(val, PREFIX + key, non_mappings)[0])
    return res_sip, non_mappings

def write_res_sip(filename, res_sip, non_mappings):
    try:
        with open(filename, 'wt') as fp:
            fp.write(';--\n')
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n')
            fp.write('Non mapped elements start\n')
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n\n')
            astconfigparser.write_dicts(fp, non_mappings[filename])
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n')
            fp.write('Non mapped elements end\n')
            fp.write(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n')
            fp.write('--;\n\n')
            # write out include file(s)
            for key, val in res_sip.includes().iteritems():
                write_res_sip(key, val, non_mappings)
                fp.write('#include "%s"\n' % key)
            fp.write('\n')
            # write out mapped data elements
            astconfigparser.write_dicts(fp, res_sip.defaults())
            astconfigparser.write_dicts(fp, res_sip.sections())

    except IOError:
        print "Could not open file ", filename, " for writing"

###############################################################################

def cli_options():
    global PREFIX
    usage = "usage: %prog [options] [input-file [output-file]]\n\n" \
        "input-file defaults to 'sip.conf'\n" \
        "output-file defaults to 'res_sip.conf'"
    parser = optparse.OptionParser(usage=usage)
    parser.add_option('-p', '--prefix', dest='prefix', default=PREFIX,
                      help='output prefix for include files')

    options, args = parser.parse_args()
    PREFIX = options.prefix

    sip_filename = args[0] if len(args) else 'sip.conf'
    res_sip_filename = args[1] if len(args) == 2 else 'res_sip.conf'

    return sip_filename, res_sip_filename

if __name__ == "__main__":
    sip_filename, res_sip_filename = cli_options()
    # configuration parser for sip.conf
    sip = astconfigparser.MultiOrderedConfigParser()
    sip.read(sip_filename)
    res_sip, non_mappings = convert(sip, res_sip_filename, dict())
    write_res_sip(res_sip_filename, res_sip, non_mappings)
