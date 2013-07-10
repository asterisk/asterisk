#!/usr/bin/python

import astconfigparser

# configuration parser for sip.conf
sip = astconfigparser.MultiOrderedConfigParser()

# configuration writer for res_sip.conf
res_sip = astconfigparser.MultiOrderedConfigParser()

###############################################################################
### some utility functions
###############################################################################
def section_by_type(section, type='endpoint'):
    """Finds a section based upon the given type, adding it if not found."""
    try:
        return astconfigparser.find_dict(
            res_sip.section(section), 'type', type)
    except LookupError:
        # section for type doesn't exist, so add
        sect = res_sip.add_section(section)
        sect['type'] = type
        return sect

def set_value(key=None, val=None, section=None, type='endpoint'):
    """Sets the key to the value within the section in res_sip.conf"""
    def _set_value(k, v, s):
        set_value(key if key else k, v, s, type)

    # if no value or section return the set_value
    # function with the enclosed key and type
    if not val and not section:
        return _set_value

    # otherwise try to set the value
    section_by_type(section, type)[key] = val

def merge_value(key=None, val=None, section=None,
                type='endpoint', section_to=None):
    """Merge values from the given section with those from the default."""
    def _merge_value(k, v, s):
        merge_value(key if key else k, v, s, type, section_to)

    # if no value or section return the merge_value
    # function with the enclosed key and type
    if not val and not section:
        return _merge_value

    # should return single section
    sect = sip.section(section)
    # for each merged value add it to res_sip.conf
    for i in sect.get_merged(key):
        set_value(key, i, section_to if section_to else section, type)

def is_in(s, sub):
    """Returns true if 'sub' is in 's'"""
    return s.find(sub) != -1

###############################################################################
### mapping functions -
###      define f(key, val, section) where key/val are the key/value pair to
###      write to given section in res_sip.conf
###############################################################################

def set_dtmfmode(key, val, section):
    """Sets the dtmfmode value.  If value matches allowable option in res_sip
       then map it, otherwise set it to none.
    """
    # available res_sip.conf values: frc4733, inband, info, none
    if val != 'inband' or val != 'info':
        print "sip.conf: dtmfmode = %s did not fully map into " \
              "res_sip.conf - setting to 'none'" % val
        val = 'none'
    set_value(key, val, section)

def from_nat(key, val, section):
    """Sets values from nat into the appropriate res_sip.conf options."""
    # nat from sip.conf can be comma separated list of values:
    # yes/no, [auto_]force_rport, [auto_]comedia
    if is_in(val, 'yes'):
        set_value('rtp_symmetric', 'yes', section)
        set_value('rewrite_contact', 'yes', section)
    if is_in(val, 'comedia'):
        set_value('rtp_symmetric', 'yes', section)
    if is_in(val, 'force_rport'):
        set_value('force_rport', 'yes', section)
        set_value('rewrite_contact', 'yes', section)

def set_timers(key, val, section):
    """Sets the timers in res_sip.conf from the session-timers option
       found in sip.conf.
    """
    # res_sip.conf values can be yes/no, required, always
    if val == 'originate':
        set_value('timers', 'always', section)
    elif val == 'accept':
        set_value('timers', 'required', section)
    elif val == 'never':
        set_value('timers', 'no', section)
    else:
        set_value('timers', 'yes', section)

def set_direct_media(key, val, section):
    """Maps values from the sip.conf comma separated direct_media option
       into res_sip.conf direct_media options.
    """
    if is_in(val, 'yes'):
        set_value('direct_media', 'yes', section)
    if is_in(val, 'update'):
        set_value('direct_media_method', 'update', section)
    if is_in(val, 'outgoing'):
        set_value('directed_media_glare_mitigation', 'outgoing', section)
    if is_in(val, 'nonat'):
        set_value('disable_directed_media_on_nat', 'yes', section)

def from_sendrpid(key, val, section):
    """Sets the send_rpid/pai values in res_sip.conf."""
    if val == 'yes' or val == 'rpid':
        set_value('send_rpid', 'yes', section)
    elif val == 'pai':
        set_value('send_pai', 'yes', section)

def set_media_encryption(key, val, section):
    """Sets the media_encryption value in res_sip.conf"""
    if val == 'yes':
        set_value('media_encryption', 'sdes', section)

def from_recordfeature(key, val, section):
    """If record on/off feature is set to automixmon then set
       one_touch_recording, otherwise it can't be mapped.
    """
    if val == 'automixmon':
        set_value('one_touch_recording', 'yes', section)
    else:
        print "sip.conf: %s = %s could not be fully map " \
              "one_touch_recording not set in res_sip.conf" % (key, val)

def from_progressinband(key, val, section):
    """Sets the inband_progress value in res_sip.conf"""
    # progressinband can = yes/no/never
    if val == 'never':
        val = 'no'
    set_value('inband_progress', val, section)

def from_host(key, val, section):
    """Sets contact info in an AOR section in in res_sip.conf using 'host'
       data from sip.conf
    """
    # all aors have the same name as the endpoint so makes
    # it easy to endpoint's 'aors' value
    set_value('aors', section, section)
    if val != 'dynamic':
        set_value('contact', val, section, 'aor')
    else:
        set_value('max_contacts', 1, section, 'aor')

def from_subscribemwi(key, val, section):
    """Checks the subscribemwi value in sip.conf.  If yes places the
       mailbox value in mailboxes within the endpoint, otherwise puts
       it in the aor.
    """
    mailboxes = sip.get('mailbox', section)
    type = 'endpoint' if val == 'yes' else 'aor'
    set_value('mailboxes', mailboxes, section, type)

###############################################################################

# options in res_sip.conf on an endpoint that have no sip.conf equivalent:
# type, rtp_ipv6, 100rel, trust_id_outbound, aggregate_mwi,
# connected_line_method

# known sip.conf peer keys that can be mapped to a res_sip.conf section/key
peer_map = {
    # sip.conf option      mapping function     res_sip.conf option(s)
    ###########################################################################
    'context':            set_value,
    'dtmfmode':           set_dtmfmode,
    'disallow':           merge_value,
    'allow':              merge_value,
    'nat':                from_nat,            # rtp_symmetric, force_rport,
                                               # rewrite_contact
    'icesupport':         set_value('ice_support'),
    'autoframing':        set_value('use_ptime'),
    'outboundproxy':      set_value('outbound_proxy'),
    'mohsuggest':         set_value,
    'session-timers':     set_timers,          # timers
    'session-minse':      set_value('timers_min_se'),
    'session-expires':    set_value('timers_sess_expires'),
    'externip':           set_value('external_media_address'),
    'externhost':         set_value('external_media_address'),
    # identify_by ?
    'direct_media':       set_direct_media,    # direct_media
                                               # direct_media_method
                                               # directed_media_glare_mitigation
                                               # disable_directed_media_on_nat
    'callerid':           set_value,           # callerid
    'callingpres':        set_value('callerid_privacy'),
    'cid_tag':            set_value('callerid_tag'),
    'trustpid':           set_value('trust_id_inbound'),
    'sendrpid':           from_sendrpid,       # send_pai, send_rpid
    'send_diversion':     set_value,
    'encrpytion':         set_media_encryption,
    'use_avpf':           set_value,
    'recordonfeature':    from_recordfeature,  # automixon
    'recordofffeature':   from_recordfeature,  # automixon
    'progressinband':     from_progressinband, # in_band_progress
    'callgroup':          set_value,
    'pickupgroup':        set_value,
    'namedcallgroup':     set_value,
    'namedpickupgroup':   set_value,
    'busylevel':          set_value('devicestate_busy_at'),

############################ maps to an aor ###################################

    'host':               from_host,           # contact, max_contacts
    'subscribemwi':       from_subscribemwi,   # mailboxes
    'qualifyfreq':        set_value('qualify_frequency', type='aor'),

############################# maps to auth#####################################
#        type = auth
#        username
#        password
#        md5_cred
#        realm
#        nonce_lifetime
#        auth_type
######################### maps to acl/security ################################

    'permit':             merge_value(type='security', section_to='acl'),
    'deny':               merge_value(type='security', section_to='acl'),
    'acl':                merge_value(type='security', section_to='acl'),
    'contactpermit':      merge_value(type='security', section_to='acl'),
    'contactdeny':        merge_value(type='security', section_to='acl'),
    'contactacl':         merge_value(type='security', section_to='acl'),

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
}

def map_peer(section):
    for key, fun in peer_map.iteritems():
        try:
            fun(key, sip.get(key, section), section)
        except LookupError:
            pass
#            print "%s not found for section %s - putting nothing in res_sip.conf" % (key, section)

    # since we are pulling from sip.conf this should always return
    # a single peer value and never a list of peers
    peer = sip.section(section)
    # loop through the peer and print out any that can't be mapped
    # for key in peer.keys():
    #     if key not in peer_map:
    #         print "Peer: [{}] {} could not be mapped".format(section, key)

def convert():
    for section in sip.sections():
        if section == 'authentication':
            pass
        elif section != 'general':
            map_peer(section)

###############################################################################

if __name__ == "__main__":
    sip.read('sip.conf')
    convert()
    res_sip.write('res_sip.conf')
