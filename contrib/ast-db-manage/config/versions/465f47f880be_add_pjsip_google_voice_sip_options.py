"""add pjsip google voice sip options

Revision ID: 465f47f880be
Revises: 7f85dd44c775
Create Date: 2018-09-25 17:26:12.892161

"""

# revision identifiers, used by Alembic.
revision = '465f47f880be'
down_revision = '7f85dd44c775'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM
import sqlalchemy as sa

AST_BOOL_NAME = 'ast_bool_values'
# We'll just ignore the n/y and f/t abbreviations as Asterisk does not write
# those aliases.
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

PJSIP_TRANSPORT_PROTOCOL_OLD_NAME = 'pjsip_transport_protocol_values'
PJSIP_TRANSPORT_PROTOCOL_NEW_NAME = 'pjsip_transport_protocol_values_v2'

PJSIP_TRANSPORT_PROTOCOL_OLD_VALUES = ['udp', 'tcp', 'tls', 'ws', 'wss']
PJSIP_TRANSPORT_PROTOCOL_NEW_VALUES = ['udp', 'tcp', 'tls', 'ws', 'wss', 'flow']

PJSIP_TRANSPORT_PROTOCOL_OLD_TYPE = sa.Enum(*PJSIP_TRANSPORT_PROTOCOL_OLD_VALUES,
                                            name=PJSIP_TRANSPORT_PROTOCOL_OLD_NAME)
PJSIP_TRANSPORT_PROTOCOL_NEW_TYPE = sa.Enum(*PJSIP_TRANSPORT_PROTOCOL_NEW_VALUES,
                                            name=PJSIP_TRANSPORT_PROTOCOL_NEW_NAME)

PJSIP_AUTH_TYPE_OLD_NAME = 'pjsip_auth_type_values'
PJSIP_AUTH_TYPE_NEW_NAME = 'pjsip_auth_type_values_v2'

PJSIP_AUTH_TYPE_OLD_VALUES = ['md5', 'userpass']
PJSIP_AUTH_TYPE_NEW_VALUES = ['md5', 'userpass', 'google_oauth']

PJSIP_AUTH_TYPE_OLD_TYPE = sa.Enum(*PJSIP_AUTH_TYPE_OLD_VALUES,
                                   name=PJSIP_AUTH_TYPE_OLD_NAME)
PJSIP_AUTH_TYPE_NEW_TYPE = sa.Enum(*PJSIP_AUTH_TYPE_NEW_VALUES,
                                   name=PJSIP_AUTH_TYPE_NEW_NAME)


def upgrade():
    if op.get_context().bind.dialect.name == 'postgresql':
        enum = PJSIP_TRANSPORT_PROTOCOL_NEW_TYPE
        enum.create(op.get_bind(), checkfirst=False)
        op.execute('ALTER TABLE ps_transports ALTER COLUMN protocol TYPE'
                   ' ' + PJSIP_TRANSPORT_PROTOCOL_NEW_NAME + ' USING'
                   ' protocol::text::' + PJSIP_TRANSPORT_PROTOCOL_NEW_NAME)
        ENUM(name=PJSIP_TRANSPORT_PROTOCOL_OLD_NAME).drop(op.get_bind(), checkfirst=False)

        enum = PJSIP_AUTH_TYPE_NEW_TYPE
        enum.create(op.get_bind(), checkfirst=False)
        op.execute('ALTER TABLE ps_auths ALTER COLUMN auth_type TYPE'
                   ' ' + PJSIP_AUTH_TYPE_NEW_NAME + ' USING'
                   ' auth_type::text::' + PJSIP_AUTH_TYPE_NEW_NAME)
        ENUM(name=PJSIP_AUTH_TYPE_OLD_NAME).drop(op.get_bind(), checkfirst=False)
    else:
        op.alter_column('ps_transports', 'protocol',
                        type_=PJSIP_TRANSPORT_PROTOCOL_NEW_TYPE,
                        existing_type=PJSIP_TRANSPORT_PROTOCOL_OLD_TYPE)
        op.alter_column('ps_auths', 'auth_type',
                        type_=PJSIP_AUTH_TYPE_NEW_TYPE,
                        existing_type=PJSIP_AUTH_TYPE_OLD_TYPE)

    # ast_bool_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)

    op.add_column('ps_registrations', sa.Column('support_outbound', ast_bool_values))
    op.add_column('ps_registrations', sa.Column('contact_header_params', sa.String(255)))

    op.add_column('ps_auths', sa.Column('refresh_token', sa.String(255)))
    op.add_column('ps_auths', sa.Column('oauth_clientid', sa.String(255)))
    op.add_column('ps_auths', sa.Column('oauth_secret', sa.String(255)))

def downgrade():
    # First we need to ensure that columns are not using the enum values
    # that are going away.
    op.execute("UPDATE ps_transports SET protocol='udp' WHERE protocol='flow'")
    op.execute("UPDATE ps_auths SET auth_type='userpass' WHERE auth_type='google_oauth'")

    if op.get_context().bind.dialect.name == 'postgresql':
        enum = PJSIP_TRANSPORT_PROTOCOL_OLD_TYPE
        enum.create(op.get_bind(), checkfirst=False)
        op.execute('ALTER TABLE ps_transports ALTER COLUMN protocol TYPE'
                   ' ' + PJSIP_TRANSPORT_PROTOCOL_OLD_NAME + ' USING'
                   ' protocol::text::' + PJSIP_TRANSPORT_PROTOCOL_OLD_NAME)
        ENUM(name=PJSIP_TRANSPORT_PROTOCOL_NEW_NAME).drop(op.get_bind(), checkfirst=False)

        enum = PJSIP_AUTH_TYPE_OLD_TYPE
        enum.create(op.get_bind(), checkfirst=False)
        op.execute('ALTER TABLE ps_auths ALTER COLUMN auth_type TYPE'
                   ' ' + PJSIP_AUTH_TYPE_OLD_NAME + ' USING'
                   ' auth_type::text::' + PJSIP_AUTH_TYPE_OLD_NAME)
        ENUM(name=PJSIP_AUTH_TYPE_NEW_NAME).drop(op.get_bind(), checkfirst=False)
    else:
        op.alter_column('ps_transports', 'protocol',
                        type_=PJSIP_TRANSPORT_PROTOCOL_OLD_TYPE,
                        existing_type=PJSIP_TRANSPORT_PROTOCOL_NEW_TYPE)
        op.alter_column('ps_auths', 'auth_type',
                        type_=PJSIP_AUTH_TYPE_OLD_TYPE,
                        existing_type=PJSIP_AUTH_TYPE_NEW_TYPE)

    op.drop_column('ps_registrations', 'support_outbound')
    op.drop_column('ps_registrations', 'contact_header_params')

    op.drop_column('ps_auths', 'refresh_token')
    op.drop_column('ps_auths', 'oauth_clientid')
    op.drop_column('ps_auths', 'oauth_secret')
