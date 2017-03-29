"""Add tables for pjsip

Revision ID: 43956d550a44
Revises: 4da0c5f79a9c
Create Date: 2013-09-30 13:23:59.676690

"""

# revision identifiers, used by Alembic.
revision = '43956d550a44'
down_revision = '4da0c5f79a9c'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM


YESNO_VALUES = ['yes', 'no']
PJSIP_CID_PRIVACY_VALUES = ['allowed_not_screened', 'allowed_passed_screened',
                            'allowed_failed_screened', 'allowed',
                            'prohib_not_screened', 'prohib_passed_screened',
                            'prohib_failed_screened', 'prohib', 'unavailable']
PJSIP_100REL_VALUES = ['no', 'required', 'yes']
PJSIP_CONNECTED_LINE_METHOD_VALUES = ['invite', 'reinvite', 'update']
PJSIP_DIRECT_MEDIA_GLARE_MITIGATION_VALUES = ['none', 'outgoing', 'incoming']
PJSIP_DTMF_MODE_VALUES = ['rfc4733', 'inband', 'info']
PJSIP_IDENTIFY_BY_VALUES = ['username']
PJSIP_TIMERS_VALUES = ['forced', 'no', 'required', 'yes']
PJSIP_MEDIA_ENCRYPTION_VALUES = ['no', 'sdes', 'dtls']
PJSIP_T38UDPTL_EC_VALUES = ['none', 'fec', 'redundancy']
PJSIP_DTLS_SETUP_VALUES = ['active', 'passive', 'actpass']
PJSIP_AUTH_TYPE_VALUES = ['md5', 'userpass']
PJSIP_TRANSPORT_METHOD_VALUES = ['default', 'unspecified', 'tlsv1', 'sslv2',
                                 'sslv3', 'sslv23']
PJSIP_TRANSPORT_PROTOCOL_VALUES = ['udp', 'tcp', 'tls', 'ws', 'wss']


def upgrade():
    op.create_table(
        'ps_endpoints',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('transport', sa.String(40)),
        sa.Column('aors', sa.String(200)),
        sa.Column('auth', sa.String(40)),
        sa.Column('context', sa.String(40)),
        sa.Column('disallow', sa.String(200)),
        sa.Column('allow', sa.String(200)),
        sa.Column('direct_media', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('connected_line_method',
                  sa.Enum(*PJSIP_CONNECTED_LINE_METHOD_VALUES, name='pjsip_connected_line_method_values')),
        sa.Column('direct_media_method',
                  sa.Enum(*PJSIP_CONNECTED_LINE_METHOD_VALUES, name='pjsip_connected_line_method_values')),
        sa.Column('direct_media_glare_mitigation',
                  sa.Enum(*PJSIP_DIRECT_MEDIA_GLARE_MITIGATION_VALUES, name='pjsip_direct_media_glare_mitigation_values')),
        sa.Column('disable_direct_media_on_nat', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('dtmf_mode', sa.Enum(*PJSIP_DTMF_MODE_VALUES, name='pjsip_dtmf_mode_values')),
        sa.Column('external_media_address', sa.String(40)),
        sa.Column('force_rport', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('ice_support', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('identify_by', sa.Enum(*PJSIP_IDENTIFY_BY_VALUES, name='pjsip_identify_by_values')),
        sa.Column('mailboxes', sa.String(40)),
        sa.Column('moh_suggest', sa.String(40)),
        sa.Column('outbound_auth', sa.String(40)),
        sa.Column('outbound_proxy', sa.String(40)),
        sa.Column('rewrite_contact', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('rtp_ipv6', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('rtp_symmetric', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('send_diversion', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('send_pai', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('send_rpid', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('timers_min_se', sa.Integer),
        sa.Column('timers', sa.Enum(*PJSIP_TIMERS_VALUES, name='pjsip_timer_values')),
        sa.Column('timers_sess_expires', sa.Integer),
        sa.Column('callerid', sa.String(40)),
        sa.Column('callerid_privacy', sa.Enum(*PJSIP_CID_PRIVACY_VALUES, name='pjsip_cid_privacy_values')),
        sa.Column('callerid_tag', sa.String(40)),
        sa.Column('100rel', sa.Enum(*PJSIP_100REL_VALUES, name='pjsip_100rel_values')),
        sa.Column('aggregate_mwi', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('trust_id_inbound', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('trust_id_outbound', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('use_ptime', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('use_avpf', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('media_encryption', sa.Enum(*PJSIP_MEDIA_ENCRYPTION_VALUES, name='pjsip_media_encryption_values')),
        sa.Column('inband_progress', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('call_group', sa.String(40)),
        sa.Column('pickup_group', sa.String(40)),
        sa.Column('named_call_group', sa.String(40)),
        sa.Column('named_pickup_group', sa.String(40)),
        sa.Column('device_state_busy_at', sa.Integer),
        sa.Column('fax_detect', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('t38_udptl', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('t38_udptl_ec', sa.Enum(*PJSIP_T38UDPTL_EC_VALUES, name='pjsip_t38udptl_ec_values')),
        sa.Column('t38_udptl_maxdatagram', sa.Integer),
        sa.Column('t38_udptl_nat', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('t38_udptl_ipv6', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('tone_zone', sa.String(40)),
        sa.Column('language', sa.String(40)),
        sa.Column('one_touch_recording', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('record_on_feature', sa.String(40)),
        sa.Column('record_off_feature', sa.String(40)),
        sa.Column('rtp_engine', sa.String(40)),
        sa.Column('allow_transfer', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('allow_subscribe', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('sdp_owner', sa.String(40)),
        sa.Column('sdp_session', sa.String(40)),
        sa.Column('tos_audio', sa.Integer),
        sa.Column('tos_video', sa.Integer),
        sa.Column('cos_audio', sa.Integer),
        sa.Column('cos_video', sa.Integer),
        sa.Column('sub_min_expiry', sa.Integer),
        sa.Column('from_domain', sa.String(40)),
        sa.Column('from_user', sa.String(40)),
        sa.Column('mwi_fromuser', sa.String(40)),
        sa.Column('dtls_verify', sa.String(40)),
        sa.Column('dtls_rekey', sa.String(40)),
        sa.Column('dtls_cert_file', sa.String(200)),
        sa.Column('dtls_private_key', sa.String(200)),
        sa.Column('dtls_cipher', sa.String(200)),
        sa.Column('dtls_ca_file', sa.String(200)),
        sa.Column('dtls_ca_path', sa.String(200)),
        sa.Column('dtls_setup', sa.Enum(*PJSIP_DTLS_SETUP_VALUES, name='pjsip_dtls_setup_values')),
        sa.Column('srtp_tag_32', sa.Enum(*YESNO_VALUES, name='yesno_values')),
    )

    op.create_index('ps_endpoints_id', 'ps_endpoints', ['id'])

    op.create_table(
        'ps_auths',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('auth_type', sa.Enum(*PJSIP_AUTH_TYPE_VALUES, name='pjsip_auth_type_values')),
        sa.Column('nonce_lifetime', sa.Integer),
        sa.Column('md5_cred', sa.String(40)),
        sa.Column('password', sa.String(80)),
        sa.Column('realm', sa.String(40)),
        sa.Column('username', sa.String(40)),
    )

    op.create_index('ps_auths_id', 'ps_auths', ['id'])

    op.create_table(
        'ps_aors',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('contact', sa.String(40)),
        sa.Column('default_expiration', sa.Integer),
        sa.Column('mailboxes', sa.String(80)),
        sa.Column('max_contacts', sa.Integer),
        sa.Column('minimum_expiration', sa.Integer),
        sa.Column('remove_existing', sa.Enum(*YESNO_VALUES, name='yesno_values')),
        sa.Column('qualify_frequency', sa.Integer),
        sa.Column('authenticate_qualify', sa.Enum(*YESNO_VALUES, name='yesno_values')),
    )

    op.create_index('ps_aors_id', 'ps_aors', ['id'])

    op.create_table(
        'ps_contacts',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('uri', sa.String(40)),
        sa.Column('expiration_time', sa.String(40)),
        sa.Column('qualify_frequency', sa.Integer),
    )

    op.create_index('ps_contacts_id', 'ps_contacts', ['id'])

    op.create_table(
        'ps_domain_aliases',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('domain', sa.String(80)),
    )

    op.create_index('ps_domain_aliases_id', 'ps_domain_aliases', ['id'])

    op.create_table(
        'ps_endpoint_id_ips',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('endpoint', sa.String(40)),
        sa.Column('match', sa.String(80)),
    )

    op.create_index('ps_endpoint_id_ips_id',
                    'ps_endpoint_id_ips', ['id'])


def downgrade():
    context = op.get_context()

    op.drop_table('ps_endpoints')
    op.drop_table('ps_auths')
    op.drop_table('ps_aors')
    op.drop_table('ps_contacts')
    op.drop_table('ps_domain_aliases')
    op.drop_table('ps_endpoint_id_ips')

    enums = ['yesno_values',
             'pjsip_100rel_values','pjsip_auth_type_values','pjsip_cid_privacy_values',
             'pjsip_connected_line_method_values','pjsip_direct_media_glare_mitigation_values',
             'pjsip_dtls_setup_values','pjsip_dtmf_mode_values','pjsip_identify_by_values',
             'pjsip_media_encryption_values','pjsip_t38udptl_ec_values','pjsip_timer_values']

    if context.bind.dialect.name == 'postgresql':
        for e in enums:
            ENUM(name=e).drop(op.get_bind(), checkfirst=False)
