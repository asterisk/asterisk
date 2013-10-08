SET TRANSACTION READ WRITE

/

CREATE TABLE alembic_version (
    version_num VARCHAR2(32 CHAR) NOT NULL
)

/

-- Running upgrade None -> 4da0c5f79a9c

CREATE TABLE sippeers (
    id INTEGER NOT NULL, 
    name VARCHAR2(40 CHAR) NOT NULL, 
    ipaddr VARCHAR2(45 CHAR), 
    port INTEGER, 
    regseconds INTEGER, 
    defaultuser VARCHAR2(40 CHAR), 
    fullcontact VARCHAR2(80 CHAR), 
    regserver VARCHAR2(20 CHAR), 
    useragent VARCHAR2(20 CHAR), 
    lastms INTEGER, 
    host VARCHAR2(40 CHAR), 
    type VARCHAR(6 CHAR), 
    context VARCHAR2(40 CHAR), 
    permit VARCHAR2(95 CHAR), 
    deny VARCHAR2(95 CHAR), 
    secret VARCHAR2(40 CHAR), 
    md5secret VARCHAR2(40 CHAR), 
    remotesecret VARCHAR2(40 CHAR), 
    transport VARCHAR(7 CHAR), 
    dtmfmode VARCHAR(9 CHAR), 
    directmedia VARCHAR(6 CHAR), 
    nat VARCHAR2(29 CHAR), 
    callgroup VARCHAR2(40 CHAR), 
    pickupgroup VARCHAR2(40 CHAR), 
    language VARCHAR2(40 CHAR), 
    disallow VARCHAR2(200 CHAR), 
    allow VARCHAR2(200 CHAR), 
    insecure VARCHAR2(40 CHAR), 
    trustrpid VARCHAR(3 CHAR), 
    progressinband VARCHAR(5 CHAR), 
    promiscredir VARCHAR(3 CHAR), 
    useclientcode VARCHAR(3 CHAR), 
    accountcode VARCHAR2(40 CHAR), 
    setvar VARCHAR2(200 CHAR), 
    callerid VARCHAR2(40 CHAR), 
    amaflags VARCHAR2(40 CHAR), 
    callcounter VARCHAR(3 CHAR), 
    busylevel INTEGER, 
    allowoverlap VARCHAR(3 CHAR), 
    allowsubscribe VARCHAR(3 CHAR), 
    videosupport VARCHAR(3 CHAR), 
    maxcallbitrate INTEGER, 
    rfc2833compensate VARCHAR(3 CHAR), 
    mailbox VARCHAR2(40 CHAR), 
    "session-timers" VARCHAR(9 CHAR), 
    "session-expires" INTEGER, 
    "session-minse" INTEGER, 
    "session-refresher" VARCHAR(3 CHAR), 
    t38pt_usertpsource VARCHAR2(40 CHAR), 
    regexten VARCHAR2(40 CHAR), 
    fromdomain VARCHAR2(40 CHAR), 
    fromuser VARCHAR2(40 CHAR), 
    qualify VARCHAR2(40 CHAR), 
    defaultip VARCHAR2(45 CHAR), 
    rtptimeout INTEGER, 
    rtpholdtimeout INTEGER, 
    sendrpid VARCHAR(3 CHAR), 
    outboundproxy VARCHAR2(40 CHAR), 
    callbackextension VARCHAR2(40 CHAR), 
    timert1 INTEGER, 
    timerb INTEGER, 
    qualifyfreq INTEGER, 
    constantssrc VARCHAR(3 CHAR), 
    contactpermit VARCHAR2(95 CHAR), 
    contactdeny VARCHAR2(95 CHAR), 
    usereqphone VARCHAR(3 CHAR), 
    textsupport VARCHAR(3 CHAR), 
    faxdetect VARCHAR(3 CHAR), 
    buggymwi VARCHAR(3 CHAR), 
    auth VARCHAR2(40 CHAR), 
    fullname VARCHAR2(40 CHAR), 
    trunkname VARCHAR2(40 CHAR), 
    cid_number VARCHAR2(40 CHAR), 
    callingpres VARCHAR(21 CHAR), 
    mohinterpret VARCHAR2(40 CHAR), 
    mohsuggest VARCHAR2(40 CHAR), 
    parkinglot VARCHAR2(40 CHAR), 
    hasvoicemail VARCHAR(3 CHAR), 
    subscribemwi VARCHAR(3 CHAR), 
    vmexten VARCHAR2(40 CHAR), 
    autoframing VARCHAR(3 CHAR), 
    rtpkeepalive INTEGER, 
    "call-limit" INTEGER, 
    g726nonstandard VARCHAR(3 CHAR), 
    ignoresdpversion VARCHAR(3 CHAR), 
    allowtransfer VARCHAR(3 CHAR), 
    dynamic VARCHAR(3 CHAR), 
    path VARCHAR2(256 CHAR), 
    supportpath VARCHAR(3 CHAR), 
    PRIMARY KEY (id), 
    UNIQUE (name), 
    CONSTRAINT type_values CHECK (type IN ('friend', 'user', 'peer')), 
    CONSTRAINT sip_transport_values CHECK (transport IN ('udp', 'tcp', 'tls', 'ws', 'wss', 'udp,tcp', 'tcp,udp')), 
    CONSTRAINT sip_dtmfmode_values CHECK (dtmfmode IN ('rfc2833', 'info', 'shortinfo', 'inband', 'auto')), 
    CONSTRAINT sip_directmedia_values CHECK (directmedia IN ('yes', 'no', 'nonat', 'update')), 
    CONSTRAINT yes_no_values CHECK (trustrpid IN ('yes', 'no')), 
    CONSTRAINT sip_progressinband_values CHECK (progressinband IN ('yes', 'no', 'never')), 
    CONSTRAINT yes_no_values CHECK (promiscredir IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (useclientcode IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (callcounter IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (allowoverlap IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (allowsubscribe IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (videosupport IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (rfc2833compensate IN ('yes', 'no')), 
    CONSTRAINT sip_session_timers_values CHECK ("session-timers" IN ('accept', 'refuse', 'originate')), 
    CONSTRAINT sip_session_refresher_values CHECK ("session-refresher" IN ('uac', 'uas')), 
    CONSTRAINT yes_no_values CHECK (sendrpid IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (constantssrc IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (usereqphone IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (textsupport IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (faxdetect IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (buggymwi IN ('yes', 'no')), 
    CONSTRAINT sip_callingpres_values CHECK (callingpres IN ('allowed_not_screened', 'allowed_passed_screen', 'allowed_failed_screen', 'allowed', 'prohib_not_screened', 'prohib_passed_screen', 'prohib_failed_screen', 'prohib')), 
    CONSTRAINT yes_no_values CHECK (hasvoicemail IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (subscribemwi IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (autoframing IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (g726nonstandard IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (ignoresdpversion IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (allowtransfer IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (dynamic IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (supportpath IN ('yes', 'no'))
)

/

CREATE INDEX sippeers_name ON sippeers (name)

/

CREATE INDEX sippeers_name_host ON sippeers (name, host)

/

CREATE INDEX sippeers_ipaddr_port ON sippeers (ipaddr, port)

/

CREATE INDEX sippeers_host_port ON sippeers (host, port)

/

CREATE TABLE iaxfriends (
    id INTEGER NOT NULL, 
    name VARCHAR2(40 CHAR) NOT NULL, 
    type VARCHAR(6 CHAR), 
    username VARCHAR2(40 CHAR), 
    mailbox VARCHAR2(40 CHAR), 
    secret VARCHAR2(40 CHAR), 
    dbsecret VARCHAR2(40 CHAR), 
    context VARCHAR2(40 CHAR), 
    regcontext VARCHAR2(40 CHAR), 
    host VARCHAR2(40 CHAR), 
    ipaddr VARCHAR2(40 CHAR), 
    port INTEGER, 
    defaultip VARCHAR2(20 CHAR), 
    sourceaddress VARCHAR2(20 CHAR), 
    mask VARCHAR2(20 CHAR), 
    regexten VARCHAR2(40 CHAR), 
    regseconds INTEGER, 
    accountcode VARCHAR2(20 CHAR), 
    mohinterpret VARCHAR2(20 CHAR), 
    mohsuggest VARCHAR2(20 CHAR), 
    inkeys VARCHAR2(40 CHAR), 
    outkeys VARCHAR2(40 CHAR), 
    language VARCHAR2(10 CHAR), 
    callerid VARCHAR2(100 CHAR), 
    cid_number VARCHAR2(40 CHAR), 
    sendani VARCHAR(3 CHAR), 
    fullname VARCHAR2(40 CHAR), 
    trunk VARCHAR(3 CHAR), 
    auth VARCHAR2(20 CHAR), 
    maxauthreq INTEGER, 
    requirecalltoken VARCHAR(4 CHAR), 
    encryption VARCHAR(6 CHAR), 
    transfer VARCHAR(9 CHAR), 
    jitterbuffer VARCHAR(3 CHAR), 
    forcejitterbuffer VARCHAR(3 CHAR), 
    disallow VARCHAR2(200 CHAR), 
    allow VARCHAR2(200 CHAR), 
    codecpriority VARCHAR2(40 CHAR), 
    qualify VARCHAR2(10 CHAR), 
    qualifysmoothing VARCHAR(3 CHAR), 
    qualifyfreqok VARCHAR2(10 CHAR), 
    qualifyfreqnotok VARCHAR2(10 CHAR), 
    timezone VARCHAR2(20 CHAR), 
    adsi VARCHAR(3 CHAR), 
    amaflags VARCHAR2(20 CHAR), 
    setvar VARCHAR2(200 CHAR), 
    PRIMARY KEY (id), 
    UNIQUE (name), 
    CONSTRAINT type_values CHECK (type IN ('friend', 'user', 'peer')), 
    CONSTRAINT yes_no_values CHECK (sendani IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (trunk IN ('yes', 'no')), 
    CONSTRAINT iax_requirecalltoken_values CHECK (requirecalltoken IN ('yes', 'no', 'auto')), 
    CONSTRAINT iax_encryption_values CHECK (encryption IN ('yes', 'no', 'aes128')), 
    CONSTRAINT iax_transfer_values CHECK (transfer IN ('yes', 'no', 'mediaonly')), 
    CONSTRAINT yes_no_values CHECK (jitterbuffer IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (forcejitterbuffer IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (qualifysmoothing IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (adsi IN ('yes', 'no'))
)

/

CREATE INDEX iaxfriends_name ON iaxfriends (name)

/

CREATE INDEX iaxfriends_name_host ON iaxfriends (name, host)

/

CREATE INDEX iaxfriends_name_ipaddr_port ON iaxfriends (name, ipaddr, port)

/

CREATE INDEX iaxfriends_ipaddr_port ON iaxfriends (ipaddr, port)

/

CREATE INDEX iaxfriends_host_port ON iaxfriends (host, port)

/

CREATE TABLE voicemail (
    uniqueid INTEGER NOT NULL, 
    context VARCHAR2(80 CHAR) NOT NULL, 
    mailbox VARCHAR2(80 CHAR) NOT NULL, 
    password VARCHAR2(80 CHAR) NOT NULL, 
    fullname VARCHAR2(80 CHAR), 
    alias VARCHAR2(80 CHAR), 
    email VARCHAR2(80 CHAR), 
    pager VARCHAR2(80 CHAR), 
    attach VARCHAR(3 CHAR), 
    attachfmt VARCHAR2(10 CHAR), 
    serveremail VARCHAR2(80 CHAR), 
    language VARCHAR2(20 CHAR), 
    tz VARCHAR2(30 CHAR), 
    deletevoicemail VARCHAR(3 CHAR), 
    saycid VARCHAR(3 CHAR), 
    sendvoicemail VARCHAR(3 CHAR), 
    review VARCHAR(3 CHAR), 
    tempgreetwarn VARCHAR(3 CHAR), 
    operator VARCHAR(3 CHAR), 
    envelope VARCHAR(3 CHAR), 
    sayduration INTEGER, 
    forcename VARCHAR(3 CHAR), 
    forcegreetings VARCHAR(3 CHAR), 
    callback VARCHAR2(80 CHAR), 
    dialout VARCHAR2(80 CHAR), 
    exitcontext VARCHAR2(80 CHAR), 
    maxmsg INTEGER, 
    volgain NUMERIC(5, 2), 
    imapuser VARCHAR2(80 CHAR), 
    imappassword VARCHAR2(80 CHAR), 
    imapserver VARCHAR2(80 CHAR), 
    imapport VARCHAR2(8 CHAR), 
    imapflags VARCHAR2(80 CHAR), 
    stamp DATE, 
    PRIMARY KEY (uniqueid), 
    CONSTRAINT yes_no_values CHECK (attach IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (deletevoicemail IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (saycid IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (sendvoicemail IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (review IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (tempgreetwarn IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (operator IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (envelope IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (forcename IN ('yes', 'no')), 
    CONSTRAINT yes_no_values CHECK (forcegreetings IN ('yes', 'no'))
)

/

CREATE INDEX voicemail_mailbox ON voicemail (mailbox)

/

CREATE INDEX voicemail_context ON voicemail (context)

/

CREATE INDEX voicemail_mailbox_context ON voicemail (mailbox, context)

/

CREATE INDEX voicemail_imapuser ON voicemail (imapuser)

/

CREATE TABLE meetme (
    bookid INTEGER NOT NULL, 
    confno VARCHAR2(80 CHAR) NOT NULL, 
    starttime DATE, 
    endtime DATE, 
    pin VARCHAR2(20 CHAR), 
    adminpin VARCHAR2(20 CHAR), 
    opts VARCHAR2(20 CHAR), 
    adminopts VARCHAR2(20 CHAR), 
    recordingfilename VARCHAR2(80 CHAR), 
    recordingformat VARCHAR2(10 CHAR), 
    maxusers INTEGER, 
    members INTEGER NOT NULL, 
    PRIMARY KEY (bookid)
)

/

CREATE INDEX meetme_confno_start_end ON meetme (confno, starttime, endtime)

/

CREATE TABLE musiconhold (
    name VARCHAR2(80 CHAR) NOT NULL, 
    "mode" VARCHAR(10 CHAR), 
    directory VARCHAR2(255 CHAR), 
    application VARCHAR2(255 CHAR), 
    digit VARCHAR2(1 CHAR), 
    sort VARCHAR2(10 CHAR), 
    format VARCHAR2(10 CHAR), 
    stamp DATE, 
    PRIMARY KEY (name), 
    CONSTRAINT moh_mode_values CHECK ("mode" IN ('custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3'))
)

/

-- Running upgrade 4da0c5f79a9c -> 43956d550a44

CREATE TABLE ps_endpoints (
    id VARCHAR2(40 CHAR) NOT NULL, 
    transport VARCHAR2(40 CHAR), 
    aors VARCHAR2(200 CHAR), 
    auth VARCHAR2(40 CHAR), 
    context VARCHAR2(40 CHAR), 
    disallow VARCHAR2(200 CHAR), 
    allow VARCHAR2(200 CHAR), 
    direct_media VARCHAR(3 CHAR), 
    connected_line_method VARCHAR(8 CHAR), 
    direct_media_method VARCHAR(8 CHAR), 
    direct_media_glare_mitigation VARCHAR(8 CHAR), 
    disable_direct_media_on_nat VARCHAR(3 CHAR), 
    dtmfmode VARCHAR(7 CHAR), 
    external_media_address VARCHAR2(40 CHAR), 
    force_rport VARCHAR(3 CHAR), 
    ice_support VARCHAR(3 CHAR), 
    identify_by VARCHAR(8 CHAR), 
    mailboxes VARCHAR2(40 CHAR), 
    mohsuggest VARCHAR2(40 CHAR), 
    outbound_auth VARCHAR2(40 CHAR), 
    outbound_proxy VARCHAR2(40 CHAR), 
    rewrite_contact VARCHAR(3 CHAR), 
    rtp_ipv6 VARCHAR(3 CHAR), 
    rtp_symmetric VARCHAR(3 CHAR), 
    send_diversion VARCHAR(3 CHAR), 
    send_pai VARCHAR(3 CHAR), 
    send_rpid VARCHAR(3 CHAR), 
    timers_min_se INTEGER, 
    timers VARCHAR(8 CHAR), 
    timers_sess_expires INTEGER, 
    callerid VARCHAR2(40 CHAR), 
    callerid_privacy VARCHAR(23 CHAR), 
    callerid_tag VARCHAR2(40 CHAR), 
    100rel VARCHAR(8 CHAR), 
    aggregate_mwi VARCHAR(3 CHAR), 
    trust_id_inbound VARCHAR(3 CHAR), 
    trust_id_outbound VARCHAR(3 CHAR), 
    use_ptime VARCHAR(3 CHAR), 
    use_avpf VARCHAR(3 CHAR), 
    media_encryption VARCHAR(4 CHAR), 
    inband_progress VARCHAR(3 CHAR), 
    callgroup VARCHAR2(40 CHAR), 
    pickupgroup VARCHAR2(40 CHAR), 
    namedcallgroup VARCHAR2(40 CHAR), 
    namedpickupgroup VARCHAR2(40 CHAR), 
    devicestate_busy_at INTEGER, 
    faxdetect VARCHAR(3 CHAR), 
    t38udptl VARCHAR(3 CHAR), 
    t38udptl_ec VARCHAR(10 CHAR), 
    t38udptl_maxdatagram INTEGER, 
    t38udptl_nat VARCHAR(3 CHAR), 
    t38udptl_ipv6 VARCHAR(3 CHAR), 
    tonezone VARCHAR2(40 CHAR), 
    language VARCHAR2(40 CHAR), 
    one_touch_recording VARCHAR(3 CHAR), 
    recordonfeature VARCHAR2(40 CHAR), 
    recordofffeature VARCHAR2(40 CHAR), 
    rtpengine VARCHAR2(40 CHAR), 
    allowtransfer VARCHAR(3 CHAR), 
    allowsubscribe VARCHAR(3 CHAR), 
    sdpowner VARCHAR2(40 CHAR), 
    sdpsession VARCHAR2(40 CHAR), 
    tos_audio INTEGER, 
    tos_video INTEGER, 
    cos_audio INTEGER, 
    cos_video INTEGER, 
    subminexpiry INTEGER, 
    fromdomain VARCHAR2(40 CHAR), 
    fromuser VARCHAR2(40 CHAR), 
    mwifromuser VARCHAR2(40 CHAR), 
    dtlsverify VARCHAR2(40 CHAR), 
    dtlsrekey VARCHAR2(40 CHAR), 
    dtlscertfile VARCHAR2(200 CHAR), 
    dtlsprivatekey VARCHAR2(200 CHAR), 
    dtlscipher VARCHAR2(200 CHAR), 
    dtlscafile VARCHAR2(200 CHAR), 
    dtlscapath VARCHAR2(200 CHAR), 
    dtlssetup VARCHAR(7 CHAR), 
    srtp_tag_32 VARCHAR(3 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (direct_media IN ('yes', 'no')), 
    CONSTRAINT pjsip_connected_line_method_values CHECK (connected_line_method IN ('invite', 'reinvite', 'update')), 
    CONSTRAINT pjsip_connected_line_method_values CHECK (direct_media_method IN ('invite', 'reinvite', 'update')), 
    CONSTRAINT pjsip_direct_media_glare_mitigation_values CHECK (direct_media_glare_mitigation IN ('none', 'outgoing', 'incoming')), 
    CONSTRAINT yesno_values CHECK (disable_direct_media_on_nat IN ('yes', 'no')), 
    CONSTRAINT pjsip_dtmf_mode_values CHECK (dtmfmode IN ('rfc4733', 'inband', 'info')), 
    CONSTRAINT yesno_values CHECK (force_rport IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (ice_support IN ('yes', 'no')), 
    CONSTRAINT pjsip_identify_by_values CHECK (identify_by IN ('username')), 
    CONSTRAINT yesno_values CHECK (rewrite_contact IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (rtp_ipv6 IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (rtp_symmetric IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (send_diversion IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (send_pai IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (send_rpid IN ('yes', 'no')), 
    CONSTRAINT pjsip_timer_values CHECK (timers IN ('forced', 'no', 'required', 'yes')), 
    CONSTRAINT pjsip_cid_privacy_values CHECK (callerid_privacy IN ('allowed_not_screened', 'allowed_passed_screened', 'allowed_failed_screened', 'allowed', 'prohib_not_screened', 'prohib_passed_screened', 'prohib_failed_screened', 'prohib', 'unavailable')), 
    CONSTRAINT pjsip_100rel_values CHECK (100rel IN ('no', 'required', 'yes')), 
    CONSTRAINT yesno_values CHECK (aggregate_mwi IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (trust_id_inbound IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (trust_id_outbound IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (use_ptime IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (use_avpf IN ('yes', 'no')), 
    CONSTRAINT pjsip_media_encryption_values CHECK (media_encryption IN ('no', 'sdes', 'dtls')), 
    CONSTRAINT yesno_values CHECK (inband_progress IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (faxdetect IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (t38udptl IN ('yes', 'no')), 
    CONSTRAINT pjsip_t38udptl_ec_values CHECK (t38udptl_ec IN ('none', 'fec', 'redundancy')), 
    CONSTRAINT yesno_values CHECK (t38udptl_nat IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (t38udptl_ipv6 IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (one_touch_recording IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (allowtransfer IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (allowsubscribe IN ('yes', 'no')), 
    CONSTRAINT pjsip_dtls_setup_values CHECK (dtlssetup IN ('active', 'passive', 'actpass')), 
    CONSTRAINT yesno_values CHECK (srtp_tag_32 IN ('yes', 'no'))
)

/

CREATE INDEX ps_endpoints_id ON ps_endpoints (id)

/

CREATE TABLE ps_auths (
    id VARCHAR2(40 CHAR) NOT NULL, 
    auth_type VARCHAR(8 CHAR), 
    nonce_lifetime INTEGER, 
    md5_cred VARCHAR2(40 CHAR), 
    password VARCHAR2(80 CHAR), 
    realm VARCHAR2(40 CHAR), 
    username VARCHAR2(40 CHAR), 
    UNIQUE (id), 
    CONSTRAINT pjsip_auth_type_values CHECK (auth_type IN ('md5', 'userpass'))
)

/

CREATE INDEX ps_auths_id ON ps_auths (id)

/

CREATE TABLE ps_aors (
    id VARCHAR2(40 CHAR) NOT NULL, 
    contact VARCHAR2(40 CHAR), 
    default_expiration INTEGER, 
    mailboxes VARCHAR2(80 CHAR), 
    max_contacts INTEGER, 
    minimum_expiration INTEGER, 
    remove_existing VARCHAR(3 CHAR), 
    qualify_frequency INTEGER, 
    authenticate_qualify VARCHAR(3 CHAR), 
    UNIQUE (id), 
    CONSTRAINT yesno_values CHECK (remove_existing IN ('yes', 'no')), 
    CONSTRAINT yesno_values CHECK (authenticate_qualify IN ('yes', 'no'))
)

/

CREATE INDEX ps_aors_id ON ps_aors (id)

/

CREATE TABLE ps_contacts (
    id VARCHAR2(40 CHAR) NOT NULL, 
    uri VARCHAR2(40 CHAR), 
    expiration_time VARCHAR2(40 CHAR), 
    qualify_frequency INTEGER, 
    UNIQUE (id)
)

/

CREATE INDEX ps_contacts_id ON ps_contacts (id)

/

CREATE TABLE ps_domain_aliases (
    id VARCHAR2(40 CHAR) NOT NULL, 
    domain VARCHAR2(80 CHAR), 
    UNIQUE (id)
)

/

CREATE INDEX ps_domain_aliases_id ON ps_domain_aliases (id)

/

CREATE TABLE ps_endpoint_id_ips (
    id VARCHAR2(40 CHAR) NOT NULL, 
    endpoint VARCHAR2(40 CHAR), 
    match VARCHAR2(80 CHAR), 
    UNIQUE (id)
)

/

CREATE INDEX ps_endpoint_id_ips_id ON ps_endpoint_id_ips (id)

/

INSERT INTO alembic_version (version_num) VALUES ('43956d550a44')

/

COMMIT

/

