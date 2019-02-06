BEGIN;

CREATE TABLE alembic_version (
    version_num VARCHAR(32) NOT NULL, 
    CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);

-- Running upgrade  -> 4da0c5f79a9c

CREATE TYPE type_values AS ENUM ('friend', 'user', 'peer');

CREATE TYPE sip_transport_values AS ENUM ('udp', 'tcp', 'tls', 'ws', 'wss', 'udp,tcp', 'tcp,udp');

CREATE TYPE sip_dtmfmode_values AS ENUM ('rfc2833', 'info', 'shortinfo', 'inband', 'auto');

CREATE TYPE sip_directmedia_values AS ENUM ('yes', 'no', 'nonat', 'update');

CREATE TYPE yes_no_values AS ENUM ('yes', 'no');

CREATE TYPE sip_progressinband_values AS ENUM ('yes', 'no', 'never');

CREATE TYPE sip_session_timers_values AS ENUM ('accept', 'refuse', 'originate');

CREATE TYPE sip_session_refresher_values AS ENUM ('uac', 'uas');

CREATE TYPE sip_callingpres_values AS ENUM ('allowed_not_screened', 'allowed_passed_screen', 'allowed_failed_screen', 'allowed', 'prohib_not_screened', 'prohib_passed_screen', 'prohib_failed_screen', 'prohib');

CREATE TABLE sippeers (
    id SERIAL NOT NULL, 
    name VARCHAR(40) NOT NULL, 
    ipaddr VARCHAR(45), 
    port INTEGER, 
    regseconds INTEGER, 
    defaultuser VARCHAR(40), 
    fullcontact VARCHAR(80), 
    regserver VARCHAR(20), 
    useragent VARCHAR(20), 
    lastms INTEGER, 
    host VARCHAR(40), 
    type type_values, 
    context VARCHAR(40), 
    permit VARCHAR(95), 
    deny VARCHAR(95), 
    secret VARCHAR(40), 
    md5secret VARCHAR(40), 
    remotesecret VARCHAR(40), 
    transport sip_transport_values, 
    dtmfmode sip_dtmfmode_values, 
    directmedia sip_directmedia_values, 
    nat VARCHAR(29), 
    callgroup VARCHAR(40), 
    pickupgroup VARCHAR(40), 
    language VARCHAR(40), 
    disallow VARCHAR(200), 
    allow VARCHAR(200), 
    insecure VARCHAR(40), 
    trustrpid yes_no_values, 
    progressinband sip_progressinband_values, 
    promiscredir yes_no_values, 
    useclientcode yes_no_values, 
    accountcode VARCHAR(40), 
    setvar VARCHAR(200), 
    callerid VARCHAR(40), 
    amaflags VARCHAR(40), 
    callcounter yes_no_values, 
    busylevel INTEGER, 
    allowoverlap yes_no_values, 
    allowsubscribe yes_no_values, 
    videosupport yes_no_values, 
    maxcallbitrate INTEGER, 
    rfc2833compensate yes_no_values, 
    mailbox VARCHAR(40), 
    "session-timers" sip_session_timers_values, 
    "session-expires" INTEGER, 
    "session-minse" INTEGER, 
    "session-refresher" sip_session_refresher_values, 
    t38pt_usertpsource VARCHAR(40), 
    regexten VARCHAR(40), 
    fromdomain VARCHAR(40), 
    fromuser VARCHAR(40), 
    qualify VARCHAR(40), 
    defaultip VARCHAR(45), 
    rtptimeout INTEGER, 
    rtpholdtimeout INTEGER, 
    sendrpid yes_no_values, 
    outboundproxy VARCHAR(40), 
    callbackextension VARCHAR(40), 
    timert1 INTEGER, 
    timerb INTEGER, 
    qualifyfreq INTEGER, 
    constantssrc yes_no_values, 
    contactpermit VARCHAR(95), 
    contactdeny VARCHAR(95), 
    usereqphone yes_no_values, 
    textsupport yes_no_values, 
    faxdetect yes_no_values, 
    buggymwi yes_no_values, 
    auth VARCHAR(40), 
    fullname VARCHAR(40), 
    trunkname VARCHAR(40), 
    cid_number VARCHAR(40), 
    callingpres sip_callingpres_values, 
    mohinterpret VARCHAR(40), 
    mohsuggest VARCHAR(40), 
    parkinglot VARCHAR(40), 
    hasvoicemail yes_no_values, 
    subscribemwi yes_no_values, 
    vmexten VARCHAR(40), 
    autoframing yes_no_values, 
    rtpkeepalive INTEGER, 
    "call-limit" INTEGER, 
    g726nonstandard yes_no_values, 
    ignoresdpversion yes_no_values, 
    allowtransfer yes_no_values, 
    dynamic yes_no_values, 
    path VARCHAR(256), 
    supportpath yes_no_values, 
    PRIMARY KEY (id), 
    UNIQUE (name)
);

CREATE INDEX sippeers_name ON sippeers (name);

CREATE INDEX sippeers_name_host ON sippeers (name, host);

CREATE INDEX sippeers_ipaddr_port ON sippeers (ipaddr, port);

CREATE INDEX sippeers_host_port ON sippeers (host, port);

CREATE TYPE iax_requirecalltoken_values AS ENUM ('yes', 'no', 'auto');

CREATE TYPE iax_encryption_values AS ENUM ('yes', 'no', 'aes128');

CREATE TYPE iax_transfer_values AS ENUM ('yes', 'no', 'mediaonly');

CREATE TABLE iaxfriends (
    id SERIAL NOT NULL, 
    name VARCHAR(40) NOT NULL, 
    type type_values, 
    username VARCHAR(40), 
    mailbox VARCHAR(40), 
    secret VARCHAR(40), 
    dbsecret VARCHAR(40), 
    context VARCHAR(40), 
    regcontext VARCHAR(40), 
    host VARCHAR(40), 
    ipaddr VARCHAR(40), 
    port INTEGER, 
    defaultip VARCHAR(20), 
    sourceaddress VARCHAR(20), 
    mask VARCHAR(20), 
    regexten VARCHAR(40), 
    regseconds INTEGER, 
    accountcode VARCHAR(20), 
    mohinterpret VARCHAR(20), 
    mohsuggest VARCHAR(20), 
    inkeys VARCHAR(40), 
    outkeys VARCHAR(40), 
    language VARCHAR(10), 
    callerid VARCHAR(100), 
    cid_number VARCHAR(40), 
    sendani yes_no_values, 
    fullname VARCHAR(40), 
    trunk yes_no_values, 
    auth VARCHAR(20), 
    maxauthreq INTEGER, 
    requirecalltoken iax_requirecalltoken_values, 
    encryption iax_encryption_values, 
    transfer iax_transfer_values, 
    jitterbuffer yes_no_values, 
    forcejitterbuffer yes_no_values, 
    disallow VARCHAR(200), 
    allow VARCHAR(200), 
    codecpriority VARCHAR(40), 
    qualify VARCHAR(10), 
    qualifysmoothing yes_no_values, 
    qualifyfreqok VARCHAR(10), 
    qualifyfreqnotok VARCHAR(10), 
    timezone VARCHAR(20), 
    adsi yes_no_values, 
    amaflags VARCHAR(20), 
    setvar VARCHAR(200), 
    PRIMARY KEY (id), 
    UNIQUE (name)
);

CREATE INDEX iaxfriends_name ON iaxfriends (name);

CREATE INDEX iaxfriends_name_host ON iaxfriends (name, host);

CREATE INDEX iaxfriends_name_ipaddr_port ON iaxfriends (name, ipaddr, port);

CREATE INDEX iaxfriends_ipaddr_port ON iaxfriends (ipaddr, port);

CREATE INDEX iaxfriends_host_port ON iaxfriends (host, port);

CREATE TABLE voicemail (
    uniqueid SERIAL NOT NULL, 
    context VARCHAR(80) NOT NULL, 
    mailbox VARCHAR(80) NOT NULL, 
    password VARCHAR(80) NOT NULL, 
    fullname VARCHAR(80), 
    alias VARCHAR(80), 
    email VARCHAR(80), 
    pager VARCHAR(80), 
    attach yes_no_values, 
    attachfmt VARCHAR(10), 
    serveremail VARCHAR(80), 
    language VARCHAR(20), 
    tz VARCHAR(30), 
    deletevoicemail yes_no_values, 
    saycid yes_no_values, 
    sendvoicemail yes_no_values, 
    review yes_no_values, 
    tempgreetwarn yes_no_values, 
    operator yes_no_values, 
    envelope yes_no_values, 
    sayduration INTEGER, 
    forcename yes_no_values, 
    forcegreetings yes_no_values, 
    callback VARCHAR(80), 
    dialout VARCHAR(80), 
    exitcontext VARCHAR(80), 
    maxmsg INTEGER, 
    volgain NUMERIC(5, 2), 
    imapuser VARCHAR(80), 
    imappassword VARCHAR(80), 
    imapserver VARCHAR(80), 
    imapport VARCHAR(8), 
    imapflags VARCHAR(80), 
    stamp TIMESTAMP WITHOUT TIME ZONE, 
    PRIMARY KEY (uniqueid)
);

CREATE INDEX voicemail_mailbox ON voicemail (mailbox);

CREATE INDEX voicemail_context ON voicemail (context);

CREATE INDEX voicemail_mailbox_context ON voicemail (mailbox, context);

CREATE INDEX voicemail_imapuser ON voicemail (imapuser);

CREATE TABLE meetme (
    bookid SERIAL NOT NULL, 
    confno VARCHAR(80) NOT NULL, 
    starttime TIMESTAMP WITHOUT TIME ZONE, 
    endtime TIMESTAMP WITHOUT TIME ZONE, 
    pin VARCHAR(20), 
    adminpin VARCHAR(20), 
    opts VARCHAR(20), 
    adminopts VARCHAR(20), 
    recordingfilename VARCHAR(80), 
    recordingformat VARCHAR(10), 
    maxusers INTEGER, 
    members INTEGER NOT NULL, 
    PRIMARY KEY (bookid)
);

CREATE INDEX meetme_confno_start_end ON meetme (confno, starttime, endtime);

CREATE TYPE moh_mode_values AS ENUM ('custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3');

CREATE TABLE musiconhold (
    name VARCHAR(80) NOT NULL, 
    mode moh_mode_values, 
    directory VARCHAR(255), 
    application VARCHAR(255), 
    digit VARCHAR(1), 
    sort VARCHAR(10), 
    format VARCHAR(10), 
    stamp TIMESTAMP WITHOUT TIME ZONE, 
    PRIMARY KEY (name)
);

INSERT INTO alembic_version (version_num) VALUES ('4da0c5f79a9c');

-- Running upgrade 4da0c5f79a9c -> 43956d550a44

CREATE TYPE yesno_values AS ENUM ('yes', 'no');

CREATE TYPE pjsip_connected_line_method_values AS ENUM ('invite', 'reinvite', 'update');

CREATE TYPE pjsip_direct_media_glare_mitigation_values AS ENUM ('none', 'outgoing', 'incoming');

CREATE TYPE pjsip_dtmf_mode_values AS ENUM ('rfc4733', 'inband', 'info');

CREATE TYPE pjsip_identify_by_values AS ENUM ('username');

CREATE TYPE pjsip_timer_values AS ENUM ('forced', 'no', 'required', 'yes');

CREATE TYPE pjsip_cid_privacy_values AS ENUM ('allowed_not_screened', 'allowed_passed_screened', 'allowed_failed_screened', 'allowed', 'prohib_not_screened', 'prohib_passed_screened', 'prohib_failed_screened', 'prohib', 'unavailable');

CREATE TYPE pjsip_100rel_values AS ENUM ('no', 'required', 'yes');

CREATE TYPE pjsip_media_encryption_values AS ENUM ('no', 'sdes', 'dtls');

CREATE TYPE pjsip_t38udptl_ec_values AS ENUM ('none', 'fec', 'redundancy');

CREATE TYPE pjsip_dtls_setup_values AS ENUM ('active', 'passive', 'actpass');

CREATE TABLE ps_endpoints (
    id VARCHAR(40) NOT NULL, 
    transport VARCHAR(40), 
    aors VARCHAR(200), 
    auth VARCHAR(40), 
    context VARCHAR(40), 
    disallow VARCHAR(200), 
    allow VARCHAR(200), 
    direct_media yesno_values, 
    connected_line_method pjsip_connected_line_method_values, 
    direct_media_method pjsip_connected_line_method_values, 
    direct_media_glare_mitigation pjsip_direct_media_glare_mitigation_values, 
    disable_direct_media_on_nat yesno_values, 
    dtmf_mode pjsip_dtmf_mode_values, 
    external_media_address VARCHAR(40), 
    force_rport yesno_values, 
    ice_support yesno_values, 
    identify_by pjsip_identify_by_values, 
    mailboxes VARCHAR(40), 
    moh_suggest VARCHAR(40), 
    outbound_auth VARCHAR(40), 
    outbound_proxy VARCHAR(40), 
    rewrite_contact yesno_values, 
    rtp_ipv6 yesno_values, 
    rtp_symmetric yesno_values, 
    send_diversion yesno_values, 
    send_pai yesno_values, 
    send_rpid yesno_values, 
    timers_min_se INTEGER, 
    timers pjsip_timer_values, 
    timers_sess_expires INTEGER, 
    callerid VARCHAR(40), 
    callerid_privacy pjsip_cid_privacy_values, 
    callerid_tag VARCHAR(40), 
    "100rel" pjsip_100rel_values, 
    aggregate_mwi yesno_values, 
    trust_id_inbound yesno_values, 
    trust_id_outbound yesno_values, 
    use_ptime yesno_values, 
    use_avpf yesno_values, 
    media_encryption pjsip_media_encryption_values, 
    inband_progress yesno_values, 
    call_group VARCHAR(40), 
    pickup_group VARCHAR(40), 
    named_call_group VARCHAR(40), 
    named_pickup_group VARCHAR(40), 
    device_state_busy_at INTEGER, 
    fax_detect yesno_values, 
    t38_udptl yesno_values, 
    t38_udptl_ec pjsip_t38udptl_ec_values, 
    t38_udptl_maxdatagram INTEGER, 
    t38_udptl_nat yesno_values, 
    t38_udptl_ipv6 yesno_values, 
    tone_zone VARCHAR(40), 
    language VARCHAR(40), 
    one_touch_recording yesno_values, 
    record_on_feature VARCHAR(40), 
    record_off_feature VARCHAR(40), 
    rtp_engine VARCHAR(40), 
    allow_transfer yesno_values, 
    allow_subscribe yesno_values, 
    sdp_owner VARCHAR(40), 
    sdp_session VARCHAR(40), 
    tos_audio INTEGER, 
    tos_video INTEGER, 
    cos_audio INTEGER, 
    cos_video INTEGER, 
    sub_min_expiry INTEGER, 
    from_domain VARCHAR(40), 
    from_user VARCHAR(40), 
    mwi_fromuser VARCHAR(40), 
    dtls_verify VARCHAR(40), 
    dtls_rekey VARCHAR(40), 
    dtls_cert_file VARCHAR(200), 
    dtls_private_key VARCHAR(200), 
    dtls_cipher VARCHAR(200), 
    dtls_ca_file VARCHAR(200), 
    dtls_ca_path VARCHAR(200), 
    dtls_setup pjsip_dtls_setup_values, 
    srtp_tag_32 yesno_values, 
    UNIQUE (id)
);

CREATE INDEX ps_endpoints_id ON ps_endpoints (id);

CREATE TYPE pjsip_auth_type_values AS ENUM ('md5', 'userpass');

CREATE TABLE ps_auths (
    id VARCHAR(40) NOT NULL, 
    auth_type pjsip_auth_type_values, 
    nonce_lifetime INTEGER, 
    md5_cred VARCHAR(40), 
    password VARCHAR(80), 
    realm VARCHAR(40), 
    username VARCHAR(40), 
    UNIQUE (id)
);

CREATE INDEX ps_auths_id ON ps_auths (id);

CREATE TABLE ps_aors (
    id VARCHAR(40) NOT NULL, 
    contact VARCHAR(40), 
    default_expiration INTEGER, 
    mailboxes VARCHAR(80), 
    max_contacts INTEGER, 
    minimum_expiration INTEGER, 
    remove_existing yesno_values, 
    qualify_frequency INTEGER, 
    authenticate_qualify yesno_values, 
    UNIQUE (id)
);

CREATE INDEX ps_aors_id ON ps_aors (id);

CREATE TABLE ps_contacts (
    id VARCHAR(40) NOT NULL, 
    uri VARCHAR(40), 
    expiration_time VARCHAR(40), 
    qualify_frequency INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_contacts_id ON ps_contacts (id);

CREATE TABLE ps_domain_aliases (
    id VARCHAR(40) NOT NULL, 
    domain VARCHAR(80), 
    UNIQUE (id)
);

CREATE INDEX ps_domain_aliases_id ON ps_domain_aliases (id);

CREATE TABLE ps_endpoint_id_ips (
    id VARCHAR(40) NOT NULL, 
    endpoint VARCHAR(40), 
    match VARCHAR(80), 
    UNIQUE (id)
);

CREATE INDEX ps_endpoint_id_ips_id ON ps_endpoint_id_ips (id);

UPDATE alembic_version SET version_num='43956d550a44' WHERE alembic_version.version_num = '4da0c5f79a9c';

-- Running upgrade 43956d550a44 -> 581a4264e537

CREATE TABLE extensions (
    id BIGSERIAL NOT NULL, 
    context VARCHAR(40) NOT NULL, 
    exten VARCHAR(40) NOT NULL, 
    priority INTEGER NOT NULL, 
    app VARCHAR(40) NOT NULL, 
    appdata VARCHAR(256) NOT NULL, 
    PRIMARY KEY (id), 
    UNIQUE (context, exten, priority), 
    UNIQUE (id)
);

UPDATE alembic_version SET version_num='581a4264e537' WHERE alembic_version.version_num = '43956d550a44';

-- Running upgrade 581a4264e537 -> 2fc7930b41b3

CREATE TYPE pjsip_redirect_method_values AS ENUM ('user', 'uri_core', 'uri_pjsip');

CREATE TABLE ps_systems (
    id VARCHAR(40) NOT NULL, 
    timer_t1 INTEGER, 
    timer_b INTEGER, 
    compact_headers yesno_values, 
    threadpool_initial_size INTEGER, 
    threadpool_auto_increment INTEGER, 
    threadpool_idle_timeout INTEGER, 
    threadpool_max_size INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_systems_id ON ps_systems (id);

CREATE TABLE ps_globals (
    id VARCHAR(40) NOT NULL, 
    max_forwards INTEGER, 
    user_agent VARCHAR(40), 
    default_outbound_endpoint VARCHAR(40), 
    UNIQUE (id)
);

CREATE INDEX ps_globals_id ON ps_globals (id);

CREATE TYPE pjsip_transport_method_values AS ENUM ('default', 'unspecified', 'tlsv1', 'sslv2', 'sslv3', 'sslv23');

CREATE TYPE pjsip_transport_protocol_values AS ENUM ('udp', 'tcp', 'tls', 'ws', 'wss');

CREATE TABLE ps_transports (
    id VARCHAR(40) NOT NULL, 
    async_operations INTEGER, 
    bind VARCHAR(40), 
    ca_list_file VARCHAR(200), 
    cert_file VARCHAR(200), 
    cipher VARCHAR(200), 
    domain VARCHAR(40), 
    external_media_address VARCHAR(40), 
    external_signaling_address VARCHAR(40), 
    external_signaling_port INTEGER, 
    method pjsip_transport_method_values, 
    local_net VARCHAR(40), 
    password VARCHAR(40), 
    priv_key_file VARCHAR(200), 
    protocol pjsip_transport_protocol_values, 
    require_client_cert yesno_values, 
    verify_client yesno_values, 
    verifiy_server yesno_values, 
    tos yesno_values, 
    cos yesno_values, 
    UNIQUE (id)
);

CREATE INDEX ps_transports_id ON ps_transports (id);

CREATE TABLE ps_registrations (
    id VARCHAR(40) NOT NULL, 
    auth_rejection_permanent yesno_values, 
    client_uri VARCHAR(40), 
    contact_user VARCHAR(40), 
    expiration INTEGER, 
    max_retries INTEGER, 
    outbound_auth VARCHAR(40), 
    outbound_proxy VARCHAR(40), 
    retry_interval INTEGER, 
    forbidden_retry_interval INTEGER, 
    server_uri VARCHAR(40), 
    transport VARCHAR(40), 
    support_path yesno_values, 
    UNIQUE (id)
);

CREATE INDEX ps_registrations_id ON ps_registrations (id);

ALTER TABLE ps_endpoints ADD COLUMN media_address VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN redirect_method pjsip_redirect_method_values;

ALTER TABLE ps_endpoints ADD COLUMN set_var TEXT;

ALTER TABLE ps_endpoints RENAME mwi_fromuser TO mwi_from_user;

ALTER TABLE ps_contacts ADD COLUMN outbound_proxy VARCHAR(40);

ALTER TABLE ps_contacts ADD COLUMN path TEXT;

ALTER TABLE ps_aors ADD COLUMN maximum_expiration INTEGER;

ALTER TABLE ps_aors ADD COLUMN outbound_proxy VARCHAR(40);

ALTER TABLE ps_aors ADD COLUMN support_path yesno_values;

UPDATE alembic_version SET version_num='2fc7930b41b3' WHERE alembic_version.version_num = '581a4264e537';

-- Running upgrade 2fc7930b41b3 -> 21e526ad3040

ALTER TABLE ps_globals ADD COLUMN debug VARCHAR(40);

UPDATE alembic_version SET version_num='21e526ad3040' WHERE alembic_version.version_num = '2fc7930b41b3';

-- Running upgrade 21e526ad3040 -> 28887f25a46f

CREATE TYPE queue_autopause_values AS ENUM ('yes', 'no', 'all');

CREATE TYPE queue_strategy_values AS ENUM ('ringall', 'leastrecent', 'fewestcalls', 'random', 'rrmemory', 'linear', 'wrandom', 'rrordered');

CREATE TABLE queues (
    name VARCHAR(128) NOT NULL, 
    musiconhold VARCHAR(128), 
    announce VARCHAR(128), 
    context VARCHAR(128), 
    timeout INTEGER, 
    ringinuse yesno_values, 
    setinterfacevar yesno_values, 
    setqueuevar yesno_values, 
    setqueueentryvar yesno_values, 
    monitor_format VARCHAR(8), 
    membermacro VARCHAR(512), 
    membergosub VARCHAR(512), 
    queue_youarenext VARCHAR(128), 
    queue_thereare VARCHAR(128), 
    queue_callswaiting VARCHAR(128), 
    queue_quantity1 VARCHAR(128), 
    queue_quantity2 VARCHAR(128), 
    queue_holdtime VARCHAR(128), 
    queue_minutes VARCHAR(128), 
    queue_minute VARCHAR(128), 
    queue_seconds VARCHAR(128), 
    queue_thankyou VARCHAR(128), 
    queue_callerannounce VARCHAR(128), 
    queue_reporthold VARCHAR(128), 
    announce_frequency INTEGER, 
    announce_to_first_user yesno_values, 
    min_announce_frequency INTEGER, 
    announce_round_seconds INTEGER, 
    announce_holdtime VARCHAR(128), 
    announce_position VARCHAR(128), 
    announce_position_limit INTEGER, 
    periodic_announce VARCHAR(50), 
    periodic_announce_frequency INTEGER, 
    relative_periodic_announce yesno_values, 
    random_periodic_announce yesno_values, 
    retry INTEGER, 
    wrapuptime INTEGER, 
    penaltymemberslimit INTEGER, 
    autofill yesno_values, 
    monitor_type VARCHAR(128), 
    autopause queue_autopause_values, 
    autopausedelay INTEGER, 
    autopausebusy yesno_values, 
    autopauseunavail yesno_values, 
    maxlen INTEGER, 
    servicelevel INTEGER, 
    strategy queue_strategy_values, 
    joinempty VARCHAR(128), 
    leavewhenempty VARCHAR(128), 
    reportholdtime yesno_values, 
    memberdelay INTEGER, 
    weight INTEGER, 
    timeoutrestart yesno_values, 
    defaultrule VARCHAR(128), 
    timeoutpriority VARCHAR(128), 
    PRIMARY KEY (name)
);

CREATE TABLE queue_members (
    queue_name VARCHAR(80) NOT NULL, 
    interface VARCHAR(80) NOT NULL, 
    uniqueid VARCHAR(80) NOT NULL, 
    membername VARCHAR(80), 
    state_interface VARCHAR(80), 
    penalty INTEGER, 
    paused INTEGER, 
    PRIMARY KEY (queue_name, interface)
);

UPDATE alembic_version SET version_num='28887f25a46f' WHERE alembic_version.version_num = '21e526ad3040';

-- Running upgrade 28887f25a46f -> 4c573e7135bd

ALTER TABLE ps_endpoints ALTER COLUMN tos_audio TYPE VARCHAR(10);

ALTER TABLE ps_endpoints ALTER COLUMN tos_video TYPE VARCHAR(10);

ALTER TABLE ps_endpoints DROP COLUMN cos_audio;

ALTER TABLE ps_endpoints DROP COLUMN cos_video;

ALTER TABLE ps_endpoints ADD COLUMN cos_audio INTEGER;

ALTER TABLE ps_endpoints ADD COLUMN cos_video INTEGER;

ALTER TABLE ps_transports ALTER COLUMN tos TYPE VARCHAR(10);

ALTER TABLE ps_transports DROP COLUMN cos;

ALTER TABLE ps_transports ADD COLUMN cos INTEGER;

UPDATE alembic_version SET version_num='4c573e7135bd' WHERE alembic_version.version_num = '28887f25a46f';

-- Running upgrade 4c573e7135bd -> 3855ee4e5f85

ALTER TABLE ps_endpoints ADD COLUMN message_context VARCHAR(40);

ALTER TABLE ps_contacts ADD COLUMN user_agent VARCHAR(40);

UPDATE alembic_version SET version_num='3855ee4e5f85' WHERE alembic_version.version_num = '4c573e7135bd';

-- Running upgrade 3855ee4e5f85 -> e96a0b8071c

ALTER TABLE ps_globals ALTER COLUMN user_agent TYPE VARCHAR(255);

ALTER TABLE ps_contacts ALTER COLUMN id TYPE VARCHAR(255);

ALTER TABLE ps_contacts ALTER COLUMN uri TYPE VARCHAR(255);

ALTER TABLE ps_contacts ALTER COLUMN user_agent TYPE VARCHAR(255);

ALTER TABLE ps_registrations ALTER COLUMN client_uri TYPE VARCHAR(255);

ALTER TABLE ps_registrations ALTER COLUMN server_uri TYPE VARCHAR(255);

UPDATE alembic_version SET version_num='e96a0b8071c' WHERE alembic_version.version_num = '3855ee4e5f85';

-- Running upgrade e96a0b8071c -> c6d929b23a8

CREATE TABLE ps_subscription_persistence (
    id VARCHAR(40) NOT NULL, 
    packet VARCHAR(2048), 
    src_name VARCHAR(128), 
    src_port INTEGER, 
    transport_key VARCHAR(64), 
    local_name VARCHAR(128), 
    local_port INTEGER, 
    cseq INTEGER, 
    tag VARCHAR(128), 
    endpoint VARCHAR(40), 
    expires INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_subscription_persistence_id ON ps_subscription_persistence (id);

UPDATE alembic_version SET version_num='c6d929b23a8' WHERE alembic_version.version_num = 'e96a0b8071c';

-- Running upgrade c6d929b23a8 -> 51f8cb66540e

ALTER TABLE ps_endpoints ADD COLUMN force_avp yesno_values;

ALTER TABLE ps_endpoints ADD COLUMN media_use_received_transport yesno_values;

UPDATE alembic_version SET version_num='51f8cb66540e' WHERE alembic_version.version_num = 'c6d929b23a8';

-- Running upgrade 51f8cb66540e -> 1d50859ed02e

ALTER TABLE ps_endpoints ADD COLUMN accountcode VARCHAR(20);

UPDATE alembic_version SET version_num='1d50859ed02e' WHERE alembic_version.version_num = '51f8cb66540e';

-- Running upgrade 1d50859ed02e -> 1758e8bbf6b

ALTER TABLE sippeers ALTER COLUMN useragent TYPE VARCHAR(255);

UPDATE alembic_version SET version_num='1758e8bbf6b' WHERE alembic_version.version_num = '1d50859ed02e';

-- Running upgrade 1758e8bbf6b -> 5139253c0423

ALTER TABLE queue_members DROP COLUMN uniqueid;

ALTER TABLE queue_members ADD COLUMN uniqueid INTEGER NOT NULL;

ALTER TABLE queue_members ADD UNIQUE (uniqueid);

UPDATE alembic_version SET version_num='5139253c0423' WHERE alembic_version.version_num = '1758e8bbf6b';

-- Running upgrade 5139253c0423 -> d39508cb8d8

CREATE TABLE queue_rules (
    rule_name VARCHAR(80) NOT NULL, 
    time VARCHAR(32) NOT NULL, 
    min_penalty VARCHAR(32) NOT NULL, 
    max_penalty VARCHAR(32) NOT NULL
);

UPDATE alembic_version SET version_num='d39508cb8d8' WHERE alembic_version.version_num = '5139253c0423';

-- Running upgrade d39508cb8d8 -> 5950038a6ead

ALTER TABLE ps_transports ALTER COLUMN verifiy_server TYPE yesno_values;

ALTER TABLE ps_transports RENAME verifiy_server TO verify_server;

UPDATE alembic_version SET version_num='5950038a6ead' WHERE alembic_version.version_num = 'd39508cb8d8';

-- Running upgrade 5950038a6ead -> 10aedae86a32

CREATE TYPE sip_directmedia_values_v2 AS ENUM ('yes', 'no', 'nonat', 'update', 'outgoing');

ALTER TABLE sippeers ALTER COLUMN directmedia TYPE sip_directmedia_values_v2 USING directmedia::text::sip_directmedia_values_v2;

DROP TYPE sip_directmedia_values;

UPDATE alembic_version SET version_num='10aedae86a32' WHERE alembic_version.version_num = '5950038a6ead';

-- Running upgrade 10aedae86a32 -> 371a3bf4143e

ALTER TABLE ps_endpoints ADD COLUMN user_eq_phone yesno_values;

UPDATE alembic_version SET version_num='371a3bf4143e' WHERE alembic_version.version_num = '10aedae86a32';

-- Running upgrade 371a3bf4143e -> 15b1430ad6f1

ALTER TABLE ps_endpoints ADD COLUMN moh_passthrough yesno_values;

UPDATE alembic_version SET version_num='15b1430ad6f1' WHERE alembic_version.version_num = '371a3bf4143e';

-- Running upgrade 15b1430ad6f1 -> 945b1098bdd

ALTER TABLE ps_endpoints ADD COLUMN media_encryption_optimistic yesno_values;

UPDATE alembic_version SET version_num='945b1098bdd' WHERE alembic_version.version_num = '15b1430ad6f1';

-- Running upgrade 945b1098bdd -> 45e3f47c6c44

ALTER TABLE ps_globals ADD COLUMN endpoint_identifier_order VARCHAR(40);

UPDATE alembic_version SET version_num='45e3f47c6c44' WHERE alembic_version.version_num = '945b1098bdd';

-- Running upgrade 45e3f47c6c44 -> 23530d604b96

ALTER TABLE ps_endpoints ADD COLUMN rpid_immediate yesno_values;

UPDATE alembic_version SET version_num='23530d604b96' WHERE alembic_version.version_num = '45e3f47c6c44';

-- Running upgrade 23530d604b96 -> 31cd4f4891ec

CREATE TYPE pjsip_dtmf_mode_values_v2 AS ENUM ('rfc4733', 'inband', 'info', 'auto');

ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE pjsip_dtmf_mode_values_v2 USING dtmf_mode::text::pjsip_dtmf_mode_values_v2;

DROP TYPE pjsip_dtmf_mode_values;

UPDATE alembic_version SET version_num='31cd4f4891ec' WHERE alembic_version.version_num = '23530d604b96';

-- Running upgrade 31cd4f4891ec -> 461d7d691209

ALTER TABLE ps_aors ADD COLUMN qualify_timeout INTEGER;

ALTER TABLE ps_contacts ADD COLUMN qualify_timeout INTEGER;

UPDATE alembic_version SET version_num='461d7d691209' WHERE alembic_version.version_num = '31cd4f4891ec';

-- Running upgrade 461d7d691209 -> a541e0b5e89

ALTER TABLE ps_globals ADD COLUMN max_initial_qualify_time INTEGER;

UPDATE alembic_version SET version_num='a541e0b5e89' WHERE alembic_version.version_num = '461d7d691209';

-- Running upgrade a541e0b5e89 -> 28b8e71e541f

ALTER TABLE ps_endpoints ADD COLUMN g726_non_standard yesno_values;

UPDATE alembic_version SET version_num='28b8e71e541f' WHERE alembic_version.version_num = 'a541e0b5e89';

-- Running upgrade 28b8e71e541f -> 498357a710ae

ALTER TABLE ps_endpoints ADD COLUMN rtp_keepalive INTEGER;

UPDATE alembic_version SET version_num='498357a710ae' WHERE alembic_version.version_num = '28b8e71e541f';

-- Running upgrade 498357a710ae -> 26f10cadc157

ALTER TABLE ps_endpoints ADD COLUMN rtp_timeout INTEGER;

ALTER TABLE ps_endpoints ADD COLUMN rtp_timeout_hold INTEGER;

UPDATE alembic_version SET version_num='26f10cadc157' WHERE alembic_version.version_num = '498357a710ae';

-- Running upgrade 26f10cadc157 -> 154177371065

ALTER TABLE ps_globals ADD COLUMN default_from_user VARCHAR(80);

UPDATE alembic_version SET version_num='154177371065' WHERE alembic_version.version_num = '26f10cadc157';

-- Running upgrade 154177371065 -> 28ce1e718f05

ALTER TABLE ps_registrations ADD COLUMN fatal_retry_interval INTEGER;

UPDATE alembic_version SET version_num='28ce1e718f05' WHERE alembic_version.version_num = '154177371065';

-- Running upgrade 28ce1e718f05 -> 339a3bdf53fc

ALTER TABLE ps_endpoints ALTER COLUMN accountcode TYPE VARCHAR(80);

ALTER TABLE sippeers ALTER COLUMN accountcode TYPE VARCHAR(80);

ALTER TABLE iaxfriends ALTER COLUMN accountcode TYPE VARCHAR(80);

UPDATE alembic_version SET version_num='339a3bdf53fc' WHERE alembic_version.version_num = '28ce1e718f05';

-- Running upgrade 339a3bdf53fc -> 189a235b3fd7

ALTER TABLE ps_globals ADD COLUMN keep_alive_interval INTEGER;

UPDATE alembic_version SET version_num='189a235b3fd7' WHERE alembic_version.version_num = '339a3bdf53fc';

-- Running upgrade 189a235b3fd7 -> 2d078ec071b7

ALTER TABLE ps_aors ALTER COLUMN contact TYPE VARCHAR(255);

UPDATE alembic_version SET version_num='2d078ec071b7' WHERE alembic_version.version_num = '189a235b3fd7';

-- Running upgrade 2d078ec071b7 -> 26d7f3bf0fa5

ALTER TABLE ps_endpoints ADD COLUMN bind_rtp_to_media_address yesno_values;

UPDATE alembic_version SET version_num='26d7f3bf0fa5' WHERE alembic_version.version_num = '2d078ec071b7';

-- Running upgrade 26d7f3bf0fa5 -> 136885b81223

ALTER TABLE ps_globals ADD COLUMN regcontext VARCHAR(80);

UPDATE alembic_version SET version_num='136885b81223' WHERE alembic_version.version_num = '26d7f3bf0fa5';

-- Running upgrade 136885b81223 -> 423f34ad36e2

ALTER TABLE ps_aors ALTER COLUMN qualify_timeout TYPE FLOAT;

ALTER TABLE ps_contacts ALTER COLUMN qualify_timeout TYPE FLOAT;

UPDATE alembic_version SET version_num='423f34ad36e2' WHERE alembic_version.version_num = '136885b81223';

-- Running upgrade 423f34ad36e2 -> dbc44d5a908

ALTER TABLE ps_systems ADD COLUMN disable_tcp_switch yesno_values;

ALTER TABLE ps_registrations ADD COLUMN line yesno_values;

ALTER TABLE ps_registrations ADD COLUMN endpoint VARCHAR(40);

UPDATE alembic_version SET version_num='dbc44d5a908' WHERE alembic_version.version_num = '423f34ad36e2';

-- Running upgrade dbc44d5a908 -> 3bcc0b5bc2c9

ALTER TABLE ps_transports ADD COLUMN allow_reload yesno_values;

UPDATE alembic_version SET version_num='3bcc0b5bc2c9' WHERE alembic_version.version_num = 'dbc44d5a908';

-- Running upgrade 3bcc0b5bc2c9 -> 5813202e92be

ALTER TABLE ps_globals ADD COLUMN contact_expiration_check_interval INTEGER;

UPDATE alembic_version SET version_num='5813202e92be' WHERE alembic_version.version_num = '3bcc0b5bc2c9';

-- Running upgrade 5813202e92be -> 1c688d9a003c

ALTER TABLE ps_globals ADD COLUMN default_voicemail_extension VARCHAR(40);

ALTER TABLE ps_aors ADD COLUMN voicemail_extension VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN voicemail_extension VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN mwi_subscribe_replaces_unsolicited INTEGER;

UPDATE alembic_version SET version_num='1c688d9a003c' WHERE alembic_version.version_num = '5813202e92be';

-- Running upgrade 1c688d9a003c -> 8d478ab86e29

ALTER TABLE ps_globals ADD COLUMN disable_multi_domain yesno_values;

UPDATE alembic_version SET version_num='8d478ab86e29' WHERE alembic_version.version_num = '1c688d9a003c';

-- Running upgrade 8d478ab86e29 -> 65eb22eb195

ALTER TABLE ps_globals ADD COLUMN unidentified_request_count INTEGER;

ALTER TABLE ps_globals ADD COLUMN unidentified_request_period INTEGER;

ALTER TABLE ps_globals ADD COLUMN unidentified_request_prune_interval INTEGER;

ALTER TABLE ps_globals ADD COLUMN default_realm VARCHAR(40);

UPDATE alembic_version SET version_num='65eb22eb195' WHERE alembic_version.version_num = '8d478ab86e29';

-- Running upgrade 65eb22eb195 -> 81b01a191a46

ALTER TABLE ps_contacts ADD COLUMN reg_server VARCHAR(20);

ALTER TABLE ps_contacts ADD CONSTRAINT ps_contacts_uq UNIQUE (id, reg_server);

UPDATE alembic_version SET version_num='81b01a191a46' WHERE alembic_version.version_num = '65eb22eb195';

-- Running upgrade 81b01a191a46 -> 6be31516058d

ALTER TABLE ps_contacts ADD COLUMN authenticate_qualify yesno_values;

UPDATE alembic_version SET version_num='6be31516058d' WHERE alembic_version.version_num = '81b01a191a46';

-- Running upgrade 6be31516058d -> d7e3c73eb2bf

ALTER TABLE ps_endpoints ADD COLUMN deny VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN permit VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN acl VARCHAR(40);

ALTER TABLE ps_endpoints ADD COLUMN contact_deny VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN contact_permit VARCHAR(95);

ALTER TABLE ps_endpoints ADD COLUMN contact_acl VARCHAR(40);

UPDATE alembic_version SET version_num='d7e3c73eb2bf' WHERE alembic_version.version_num = '6be31516058d';

-- Running upgrade d7e3c73eb2bf -> a845e4d8ade8

ALTER TABLE ps_contacts ADD COLUMN via_addr VARCHAR(40);

ALTER TABLE ps_contacts ADD COLUMN via_port INTEGER;

ALTER TABLE ps_contacts ADD COLUMN call_id VARCHAR(255);

UPDATE alembic_version SET version_num='a845e4d8ade8' WHERE alembic_version.version_num = 'd7e3c73eb2bf';

-- Running upgrade a845e4d8ade8 -> ef7efc2d3964

ALTER TABLE ps_contacts ADD COLUMN endpoint VARCHAR(40);

ALTER TABLE ps_contacts ALTER COLUMN expiration_time TYPE BIGINT USING expiration_time::bigint;

CREATE INDEX ps_contacts_qualifyfreq_exp ON ps_contacts (qualify_frequency, expiration_time);

CREATE INDEX ps_aors_qualifyfreq_contact ON ps_aors (qualify_frequency, contact);

UPDATE alembic_version SET version_num='ef7efc2d3964' WHERE alembic_version.version_num = 'a845e4d8ade8';

-- Running upgrade ef7efc2d3964 -> 9deac0ae4717

ALTER TABLE ps_endpoints ADD COLUMN subscribe_context VARCHAR(40);

UPDATE alembic_version SET version_num='9deac0ae4717' WHERE alembic_version.version_num = 'ef7efc2d3964';

-- Running upgrade 9deac0ae4717 -> 4a6c67fa9b7a

ALTER TABLE ps_endpoints ADD COLUMN fax_detect_timeout INTEGER;

UPDATE alembic_version SET version_num='4a6c67fa9b7a' WHERE alembic_version.version_num = '9deac0ae4717';

-- Running upgrade 4a6c67fa9b7a -> c7a44a5a0851

ALTER TABLE ps_globals ADD COLUMN mwi_tps_queue_high INTEGER;

ALTER TABLE ps_globals ADD COLUMN mwi_tps_queue_low INTEGER;

ALTER TABLE ps_globals ADD COLUMN mwi_disable_initial_unsolicited yesno_values;

UPDATE alembic_version SET version_num='c7a44a5a0851' WHERE alembic_version.version_num = '4a6c67fa9b7a';

-- Running upgrade c7a44a5a0851 -> 3772f8f828da

ALTER TYPE pjsip_identify_by_values RENAME TO pjsip_identify_by_values_tmp;

CREATE TYPE pjsip_identify_by_values AS ENUM ('username', 'auth_username');

ALTER TABLE ps_endpoints ALTER COLUMN identify_by TYPE pjsip_identify_by_values USING identify_by::text::pjsip_identify_by_values;

DROP TYPE pjsip_identify_by_values_tmp;

UPDATE alembic_version SET version_num='3772f8f828da' WHERE alembic_version.version_num = 'c7a44a5a0851';

-- Running upgrade 3772f8f828da -> 4e2493ef32e6

ALTER TABLE ps_endpoints ADD COLUMN contact_user VARCHAR(80);

UPDATE alembic_version SET version_num='4e2493ef32e6' WHERE alembic_version.version_num = '3772f8f828da';

-- Running upgrade 4e2493ef32e6 -> 7f3e21abe318

ALTER TABLE ps_endpoints ADD COLUMN preferred_codec_only yesno_values;

UPDATE alembic_version SET version_num='7f3e21abe318' WHERE alembic_version.version_num = '4e2493ef32e6';

-- Running upgrade 7f3e21abe318 -> a6ef36f1309

ALTER TABLE ps_globals ADD COLUMN ignore_uri_user_options yesno_values;

UPDATE alembic_version SET version_num='a6ef36f1309' WHERE alembic_version.version_num = '7f3e21abe318';

-- Running upgrade a6ef36f1309 -> 4468b4a91372

ALTER TABLE ps_endpoints ADD COLUMN asymmetric_rtp_codec yesno_values;

UPDATE alembic_version SET version_num='4468b4a91372' WHERE alembic_version.version_num = 'a6ef36f1309';

-- Running upgrade 4468b4a91372 -> 28ab27a7826d

ALTER TABLE ps_endpoint_id_ips ADD COLUMN srv_lookups yesno_values;

UPDATE alembic_version SET version_num='28ab27a7826d' WHERE alembic_version.version_num = '4468b4a91372';

-- Running upgrade 28ab27a7826d -> 465e70e8c337

ALTER TABLE ps_endpoint_id_ips ADD COLUMN match_header VARCHAR(255);

UPDATE alembic_version SET version_num='465e70e8c337' WHERE alembic_version.version_num = '28ab27a7826d';

-- Running upgrade 465e70e8c337 -> 15db7b91a97a

ALTER TABLE ps_endpoints ADD COLUMN rtcp_mux yesno_values;

UPDATE alembic_version SET version_num='15db7b91a97a' WHERE alembic_version.version_num = '465e70e8c337';

-- Running upgrade 15db7b91a97a -> f638dbe2eb23

ALTER TABLE ps_transports ADD COLUMN symmetric_transport yesno_values;

ALTER TABLE ps_subscription_persistence ADD COLUMN contact_uri VARCHAR(256);

UPDATE alembic_version SET version_num='f638dbe2eb23' WHERE alembic_version.version_num = '15db7b91a97a';

-- Running upgrade f638dbe2eb23 -> 8fce4c573e15

ALTER TABLE ps_endpoints ADD COLUMN allow_overlap yesno_values;

UPDATE alembic_version SET version_num='8fce4c573e15' WHERE alembic_version.version_num = 'f638dbe2eb23';

-- Running upgrade 8fce4c573e15 -> 2da192dbbc65

CREATE TABLE ps_outbound_publishes (
    id VARCHAR(40) NOT NULL, 
    expiration INTEGER, 
    outbound_auth VARCHAR(40), 
    outbound_proxy VARCHAR(256), 
    server_uri VARCHAR(256), 
    from_uri VARCHAR(256), 
    to_uri VARCHAR(256), 
    event VARCHAR(40), 
    max_auth_attempts INTEGER, 
    transport VARCHAR(40), 
    multi_user yesno_values, 
    "@body" VARCHAR(40), 
    "@context" VARCHAR(256), 
    "@exten" VARCHAR(256), 
    UNIQUE (id)
);

CREATE INDEX ps_outbound_publishes_id ON ps_outbound_publishes (id);

CREATE TABLE ps_inbound_publications (
    id VARCHAR(40) NOT NULL, 
    endpoint VARCHAR(40), 
    "event_asterisk-devicestate" VARCHAR(40), 
    "event_asterisk-mwi" VARCHAR(40), 
    UNIQUE (id)
);

CREATE INDEX ps_inbound_publications_id ON ps_inbound_publications (id);

CREATE TABLE ps_asterisk_publications (
    id VARCHAR(40) NOT NULL, 
    devicestate_publish VARCHAR(40), 
    mailboxstate_publish VARCHAR(40), 
    device_state yesno_values, 
    device_state_filter VARCHAR(256), 
    mailbox_state yesno_values, 
    mailbox_state_filter VARCHAR(256), 
    UNIQUE (id)
);

CREATE INDEX ps_asterisk_publications_id ON ps_asterisk_publications (id);

UPDATE alembic_version SET version_num='2da192dbbc65' WHERE alembic_version.version_num = '8fce4c573e15';

-- Running upgrade 2da192dbbc65 -> 1d0e332c32af

CREATE TABLE ps_resource_list (
    id VARCHAR(40) NOT NULL, 
    list_item VARCHAR(2048), 
    event VARCHAR(40), 
    full_state yesno_values, 
    notification_batch_interval INTEGER, 
    UNIQUE (id)
);

CREATE INDEX ps_resource_list_id ON ps_resource_list (id);

UPDATE alembic_version SET version_num='1d0e332c32af' WHERE alembic_version.version_num = '2da192dbbc65';

-- Running upgrade 1d0e332c32af -> 86bb1efa278d

ALTER TABLE ps_endpoints ADD COLUMN refer_blind_progress yesno_values;

UPDATE alembic_version SET version_num='86bb1efa278d' WHERE alembic_version.version_num = '1d0e332c32af';

-- Running upgrade 86bb1efa278d -> d7983954dd96

ALTER TABLE ps_endpoints ADD COLUMN notify_early_inuse_ringing yesno_values;

UPDATE alembic_version SET version_num='d7983954dd96' WHERE alembic_version.version_num = '86bb1efa278d';

-- Running upgrade d7983954dd96 -> 39959b9c2566

ALTER TABLE ps_endpoints ADD COLUMN max_audio_streams INTEGER;

ALTER TABLE ps_endpoints ADD COLUMN max_video_streams INTEGER;

UPDATE alembic_version SET version_num='39959b9c2566' WHERE alembic_version.version_num = 'd7983954dd96';

-- Running upgrade 39959b9c2566 -> 164abbd708c

CREATE TYPE pjsip_dtmf_mode_values_v3 AS ENUM ('rfc4733', 'inband', 'info', 'auto', 'auto_info');

ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE pjsip_dtmf_mode_values_v3 USING dtmf_mode::text::pjsip_dtmf_mode_values_v3;

DROP TYPE pjsip_dtmf_mode_values_v2;

UPDATE alembic_version SET version_num='164abbd708c' WHERE alembic_version.version_num = '39959b9c2566';

-- Running upgrade 164abbd708c -> 44ccced114ce

ALTER TABLE ps_endpoints ADD COLUMN webrtc yesno_values;

UPDATE alembic_version SET version_num='44ccced114ce' WHERE alembic_version.version_num = '164abbd708c';

-- Running upgrade 44ccced114ce -> f3d1c5d38b56

ALTER TABLE ps_contacts ADD COLUMN prune_on_boot yesno_values;

UPDATE alembic_version SET version_num='f3d1c5d38b56' WHERE alembic_version.version_num = '44ccced114ce';

-- Running upgrade f3d1c5d38b56 -> b83645976fdd

CREATE TYPE sha_hash_values AS ENUM ('SHA-1', 'SHA-256');

ALTER TABLE ps_endpoints ADD COLUMN dtls_fingerprint sha_hash_values;

UPDATE alembic_version SET version_num='b83645976fdd' WHERE alembic_version.version_num = 'f3d1c5d38b56';

-- Running upgrade b83645976fdd -> a1698e8bb9c5

ALTER TABLE ps_endpoints ADD COLUMN incoming_mwi_mailbox VARCHAR(40);

UPDATE alembic_version SET version_num='a1698e8bb9c5' WHERE alembic_version.version_num = 'b83645976fdd';

-- Running upgrade a1698e8bb9c5 -> 20abce6d1e3c

ALTER TYPE pjsip_identify_by_values RENAME TO pjsip_identify_by_values_tmp;

CREATE TYPE pjsip_identify_by_values AS ENUM ('username', 'auth_username', 'ip');

ALTER TABLE ps_endpoints ALTER COLUMN identify_by TYPE pjsip_identify_by_values USING identify_by::text::pjsip_identify_by_values;

DROP TYPE pjsip_identify_by_values_tmp;

UPDATE alembic_version SET version_num='20abce6d1e3c' WHERE alembic_version.version_num = 'a1698e8bb9c5';

-- Running upgrade 20abce6d1e3c -> de83fac997e2

ALTER TABLE ps_endpoints ADD COLUMN bundle yesno_values;

UPDATE alembic_version SET version_num='de83fac997e2' WHERE alembic_version.version_num = '20abce6d1e3c';

-- Running upgrade de83fac997e2 -> 041c0d3d1857

ALTER TABLE ps_endpoints ADD COLUMN dtls_auto_generate_cert yesno_values;

UPDATE alembic_version SET version_num='041c0d3d1857' WHERE alembic_version.version_num = 'de83fac997e2';

-- Running upgrade 041c0d3d1857 -> e2f04d309071

ALTER TABLE queue_members ADD COLUMN wrapuptime INTEGER;

UPDATE alembic_version SET version_num='e2f04d309071' WHERE alembic_version.version_num = '041c0d3d1857';

-- Running upgrade e2f04d309071 -> 52798ad97bdf

ALTER TABLE ps_endpoints ALTER COLUMN identify_by TYPE varchar(80) USING identify_by::text::pjsip_identify_by_values;

DROP TYPE pjsip_identify_by_values;

UPDATE alembic_version SET version_num='52798ad97bdf' WHERE alembic_version.version_num = 'e2f04d309071';

-- Running upgrade 52798ad97bdf -> d3e4284f8707

ALTER TABLE ps_subscription_persistence ADD COLUMN prune_on_boot yesno_values;

UPDATE alembic_version SET version_num='d3e4284f8707' WHERE alembic_version.version_num = '52798ad97bdf';

-- Running upgrade d3e4284f8707 -> 0be05c3a8225

ALTER TABLE ps_systems ADD COLUMN follow_early_media_fork yesno_values;

ALTER TABLE ps_systems ADD COLUMN accept_multiple_sdp_answers yesno_values;

ALTER TABLE ps_endpoints ADD COLUMN follow_early_media_fork yesno_values;

ALTER TABLE ps_endpoints ADD COLUMN accept_multiple_sdp_answers yesno_values;

UPDATE alembic_version SET version_num='0be05c3a8225' WHERE alembic_version.version_num = 'd3e4284f8707';

-- Running upgrade 0be05c3a8225 -> 19b00bc19b7b

ALTER TABLE ps_endpoints ADD COLUMN suppress_q850_reason_header yesno_values;

UPDATE alembic_version SET version_num='19b00bc19b7b' WHERE alembic_version.version_num = '0be05c3a8225';

-- Running upgrade 19b00bc19b7b -> 1d3ed26d9978

ALTER TABLE ps_contacts ALTER COLUMN uri TYPE VARCHAR(511);

UPDATE alembic_version SET version_num='1d3ed26d9978' WHERE alembic_version.version_num = '19b00bc19b7b';

-- Running upgrade 1d3ed26d9978 -> fe6592859b85

CREATE TYPE ast_bool_values AS ENUM ('0', '1', 'off', 'on', 'false', 'true', 'no', 'yes');

ALTER TABLE ps_endpoints ALTER COLUMN mwi_subscribe_replaces_unsolicited TYPE VARCHAR(5);

ALTER TABLE ps_endpoints ALTER COLUMN mwi_subscribe_replaces_unsolicited TYPE ast_bool_values;

UPDATE alembic_version SET version_num='fe6592859b85' WHERE alembic_version.version_num = '1d3ed26d9978';

-- Running upgrade fe6592859b85 -> 7f85dd44c775

ALTER TABLE ps_endpoints ALTER COLUMN suppress_q850_reason_header TYPE yesno_values;

ALTER TABLE ps_endpoints RENAME suppress_q850_reason_header TO suppress_q850_reason_headers;

UPDATE alembic_version SET version_num='7f85dd44c775' WHERE alembic_version.version_num = 'fe6592859b85';

-- Running upgrade 7f85dd44c775 -> 2bb1a85135ad

ALTER TABLE ps_globals ADD COLUMN use_callerid_contact ast_bool_values;

UPDATE alembic_version SET version_num='2bb1a85135ad' WHERE alembic_version.version_num = '7f85dd44c775';

-- Running upgrade 2bb1a85135ad -> 1ac563b350a8

ALTER TABLE ps_endpoints ADD COLUMN trust_connected_line ast_bool_values;

ALTER TABLE ps_endpoints ADD COLUMN send_connected_line ast_bool_values;

UPDATE alembic_version SET version_num='1ac563b350a8' WHERE alembic_version.version_num = '2bb1a85135ad';

-- Running upgrade 1ac563b350a8 -> 0838f8db6a61

ALTER TABLE ps_globals ADD COLUMN send_contact_status_on_update_registration ast_bool_values;

UPDATE alembic_version SET version_num='0838f8db6a61' WHERE alembic_version.version_num = '1ac563b350a8';

COMMIT;

