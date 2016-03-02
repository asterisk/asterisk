"""Add/Update tables for pjsip

Revision ID: 2fc7930b41b3
Revises: 581a4264e537
Create Date: 2014-01-14 09:23:53.923454

"""

# revision identifiers, used by Alembic.
revision = '2fc7930b41b3'
down_revision = '581a4264e537'

from alembic import op
from alembic import context
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

PJSIP_REDIRECT_METHOD_NAME = 'pjsip_redirect_method_values'
PJSIP_REDIRECT_METHOD_VALUES = ['user', 'uri_core', 'uri_pjsip']

PJSIP_TRANSPORT_METHOD_NAME = 'pjsip_transport_method_values'
PJSIP_TRANSPORT_METHOD_VALUES = ['default', 'unspecified', 'tlsv1', 'sslv2',
                                 'sslv3', 'sslv23']

PJSIP_TRANSPORT_PROTOCOL_NAME = 'pjsip_transport_protocol_values'
PJSIP_TRANSPORT_PROTOCOL_VALUES = ['udp', 'tcp', 'tls', 'ws', 'wss']

def upgrade():
    ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    # for some reason when using 'add_column' if you don't create the enum
    # first it will think it already exists and fail
    pjsip_redirect_method_values = sa.Enum(
        *PJSIP_REDIRECT_METHOD_VALUES, name=PJSIP_REDIRECT_METHOD_NAME)
    check = False if context.is_offline_mode() else True
    pjsip_redirect_method_values.create(op.get_bind(), checkfirst=check)

    pjsip_transport_method_values = sa.Enum(
        *PJSIP_TRANSPORT_METHOD_VALUES, name=PJSIP_TRANSPORT_METHOD_NAME)

    pjsip_transport_protocol_values = sa.Enum(
        *PJSIP_TRANSPORT_PROTOCOL_VALUES, name=PJSIP_TRANSPORT_PROTOCOL_NAME)

    ######################### create tables ##########################

    op.create_table(
        'ps_systems',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('timer_t1', sa.Integer),
        sa.Column('timer_b', sa.Integer),
        sa.Column('compact_headers', yesno_values),
        sa.Column('threadpool_initial_size', sa.Integer),
        sa.Column('threadpool_auto_increment', sa.Integer),
        sa.Column('threadpool_idle_timeout', sa.Integer),
        sa.Column('threadpool_max_size', sa.Integer),
    )

    op.create_index('ps_systems_id', 'ps_systems', ['id'])

    op.create_table(
        'ps_globals',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('max_forwards', sa.Integer),
        sa.Column('user_agent', sa.String(40)),
        sa.Column('default_outbound_endpoint', sa.String(40)),
    )

    op.create_index('ps_globals_id', 'ps_globals', ['id'])

    op.create_table(
        'ps_transports',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('async_operations', sa.Integer),
        sa.Column('bind', sa.String(40)),
        sa.Column('ca_list_file', sa.String(200)),
        sa.Column('cert_file', sa.String(200)),
        sa.Column('cipher', sa.String(200)),
        sa.Column('domain', sa.String(40)),
        sa.Column('external_media_address', sa.String(40)),
        sa.Column('external_signaling_address', sa.String(40)),
        sa.Column('external_signaling_port', sa.Integer),
        sa.Column('method', pjsip_transport_method_values),
        sa.Column('local_net', sa.String(40)),
        sa.Column('password', sa.String(40)),
        sa.Column('priv_key_file', sa.String(200)),
        sa.Column('protocol', pjsip_transport_protocol_values),
        sa.Column('require_client_cert', yesno_values),
        sa.Column('verify_client', yesno_values),
        sa.Column('verifiy_server', yesno_values),
        sa.Column('tos', yesno_values),
        sa.Column('cos', yesno_values),
    )

    op.create_index('ps_transports_id', 'ps_transports', ['id'])

    op.create_table(
        'ps_registrations',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('auth_rejection_permanent', yesno_values),
        sa.Column('client_uri', sa.String(40)),
        sa.Column('contact_user', sa.String(40)),
        sa.Column('expiration', sa.Integer),
        sa.Column('max_retries', sa.Integer),
        sa.Column('outbound_auth', sa.String(40)),
        sa.Column('outbound_proxy', sa.String(40)),
        sa.Column('retry_interval', sa.Integer),
        sa.Column('forbidden_retry_interval', sa.Integer),
        sa.Column('server_uri', sa.String(40)),
        sa.Column('transport', sa.String(40)),
        sa.Column('support_path', yesno_values),
    )

    op.create_index('ps_registrations_id', 'ps_registrations', ['id'])

    ########################## add columns ###########################
    with op.batch_alter_table('ps_endpoints') as batch_op:
    # new columns for endpoints
        batch_op.add_column(sa.Column('media_address', sa.String(40)))
        batch_op.add_column(sa.Column('redirect_method',
                                            pjsip_redirect_method_values))
        batch_op.add_column(sa.Column('set_var', sa.Text()))

    # rename mwi_fromuser to mwi_from_user
        batch_op.alter_column('mwi_fromuser',
                    new_column_name='mwi_from_user',
                    existing_type=sa.String(40))

    # new columns for contacts
    op.add_column('ps_contacts', sa.Column('outbound_proxy', sa.String(40)))
    op.add_column('ps_contacts', sa.Column('path', sa.Text()))

    # new columns for aors
    op.add_column('ps_aors', sa.Column('maximum_expiration', sa.Integer))
    op.add_column('ps_aors', sa.Column('outbound_proxy', sa.String(40)))
    op.add_column('ps_aors', sa.Column('support_path', yesno_values))

def downgrade():
    ########################## drop columns ##########################

    with op.batch_alter_table('ps_aors') as batch_op:
        batch_op.drop_column('support_path')
        batch_op.drop_column('outbound_proxy')
        batch_op.drop_column('maximum_expiration')

    with op.batch_alter_table('ps_contacts') as batch_op:
        batch_op.drop_column('path')
        batch_op.drop_column('outbound_proxy')

    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.alter_column('mwi_from_user',
                    new_column_name='mwi_fromuser',
                    existing_type=sa.String(40))

        batch_op.drop_column('set_var')
        batch_op.drop_column('redirect_method')
        batch_op.drop_column('media_address')

    ########################## drop tables ###########################

    op.drop_table('ps_registrations')
    op.drop_table('ps_transports')
    op.drop_table('ps_globals')
    op.drop_table('ps_systems')

    ########################## drop enums ############################

    sa.Enum(name=PJSIP_TRANSPORT_PROTOCOL_NAME).drop(
        op.get_bind(), checkfirst=False)
    sa.Enum(name=PJSIP_TRANSPORT_METHOD_NAME).drop(
        op.get_bind(), checkfirst=False)
    sa.Enum(name=PJSIP_REDIRECT_METHOD_NAME).drop(
        op.get_bind(), checkfirst=False)
