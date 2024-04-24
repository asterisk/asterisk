"""more permission boolean columns

Revision ID: 74dc751dfe8e
Revises: bd335bae5d33
Create Date: 2024-02-27 15:31:09.458313

"""

# revision identifiers, used by Alembic.
revision = '74dc751dfe8e'
down_revision = 'bd335bae5d33'

import itertools
import operator

from alembic import op
import sqlalchemy as sa
from sqlalchemy import case, cast, or_, text
from sqlalchemy.dialects.postgresql import ENUM
from sqlalchemy.sql import table, column

COLUMNS = [ ('ps_aors', 'authenticate_qualify'),
            ('ps_aors', 'remove_existing'),
            ('ps_aors', 'support_path'),
            ('ps_asterisk_publications', 'device_state'),
            ('ps_asterisk_publications', 'mailbox_state'),
            ('ps_contacts', 'authenticate_qualify'),
            ('ps_contacts', 'prune_on_boot'),
            ('ps_endpoint_id_ips', 'srv_lookups'),
            ('ps_endpoints', 'accept_multiple_sdp_answers'),
            ('ps_endpoints', 'aggregate_mwi'),
            ('ps_endpoints', 'allow_overlap'),
            ('ps_endpoints', 'allow_subscribe'),
            ('ps_endpoints', 'allow_transfer'),
            ('ps_endpoints', 'asymmetric_rtp_codec'),
            ('ps_endpoints', 'bind_rtp_to_media_address'),
            ('ps_endpoints', 'bundle'),
            ('ps_endpoints', 'direct_media'),
            ('ps_endpoints', 'disable_direct_media_on_nat'),
            ('ps_endpoints', 'dtls_auto_generate_cert'),
            ('ps_endpoints', 'fax_detect'),
            ('ps_endpoints', 'follow_early_media_fork'),
            ('ps_endpoints', 'force_avp'),
            ('ps_endpoints', 'force_rport'),
            ('ps_endpoints', 'g726_non_standard'),
            ('ps_endpoints', 'ice_support'),
            ('ps_endpoints', 'inband_progress'),
            ('ps_endpoints', 'media_encryption_optimistic'),
            ('ps_endpoints', 'media_use_received_transport'),
            ('ps_endpoints', 'moh_passthrough'),
            ('ps_endpoints', 'notify_early_inuse_ringing'),
            ('ps_endpoints', 'one_touch_recording'),
            ('ps_endpoints', 'preferred_codec_only'),
            ('ps_endpoints', 'refer_blind_progress'),
            ('ps_endpoints', 'rewrite_contact'),
            ('ps_endpoints', 'rpid_immediate'),
            ('ps_endpoints', 'rtcp_mux'),
            ('ps_endpoints', 'rtp_ipv6'),
            ('ps_endpoints', 'rtp_symmetric'),
            ('ps_endpoints', 'send_diversion'),
            ('ps_endpoints', 'send_pai'),
            ('ps_endpoints', 'send_rpid'),
            ('ps_endpoints', 'srtp_tag_32'),
            ('ps_endpoints', 'suppress_q850_reason_headers'),
            ('ps_endpoints', 't38_udptl'),
            ('ps_endpoints', 't38_udptl_ipv6'),
            ('ps_endpoints', 't38_udptl_nat'),
            ('ps_endpoints', 'trust_id_inbound'),
            ('ps_endpoints', 'trust_id_outbound'),
            ('ps_endpoints', 'use_avpf'),
            ('ps_endpoints', 'use_ptime'),
            ('ps_endpoints', 'user_eq_phone'),
            ('ps_endpoints', 'webrtc'),
            ('ps_globals', 'disable_multi_domain'),
            ('ps_globals', 'ignore_uri_user_options'),
            ('ps_globals', 'mwi_disable_initial_unsolicited'),
            ('ps_outbound_publishes', 'multi_user'),
            ('ps_registrations', 'auth_rejection_permanent'),
            ('ps_registrations', 'line'),
            ('ps_registrations', 'support_path'),
            ('ps_resource_list', 'full_state'),
            ('ps_subscription_persistence', 'prune_on_boot'),
            ('ps_systems', 'accept_multiple_sdp_answers'),
            ('ps_systems', 'compact_headers'),
            ('ps_systems', 'disable_tcp_switch'),
            ('ps_systems', 'follow_early_media_fork'),
            ('ps_transports', 'allow_reload'),
            ('ps_transports', 'allow_wildcard_certs'),
            ('ps_transports', 'require_client_cert'),
            ('ps_transports', 'symmetric_transport'),
            ('ps_transports', 'verify_client'),
            ('ps_transports', 'verify_server') ]

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)

def upgrade():
    for table_name, column_list in itertools.groupby(COLUMNS, operator.itemgetter(0)):
        with op.batch_alter_table(table_name) as batch_op:
            for _, column_name in column_list:
                batch_op.alter_column(column_name,
                                      type_=ast_bool_values,
                                      existing_type=yesno_values,
                                      postgresql_using='"{}"::text::{}'.format(column_name, AST_BOOL_NAME))

def downgrade():
    for table_name, column_list in itertools.groupby(COLUMNS, operator.itemgetter(0)):
        subject = table(table_name)
        values_exprs = {}
        for _, column_name in column_list:
            subject.append_column(column(column_name))
            values_exprs[column_name] = cast(
                case((or_(subject.c[column_name] == text("'yes'"),
                          subject.c[column_name] == text("'1'"),
                          subject.c[column_name] == text("'on'"),
                          subject.c[column_name] == text("'true'")), text("'yes'")),
                     else_=text("'no'")),
                ast_bool_values)

        op.execute(
            subject.update().values(values_exprs)
        )

    for table_name, column_list in itertools.groupby(COLUMNS, operator.itemgetter(0)):
        with op.batch_alter_table(table_name) as batch_op:
            for _, column_name in column_list:
                batch_op.alter_column(column_name,
                                      type_=yesno_values,
                                      existing_type=ast_bool_values,
                                      postgresql_using='"{}"::text::{}'.format(column_name, YESNO_NAME))
