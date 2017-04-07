"""add publish tables

Revision ID: 2da192dbbc65
Revises: 8fce4c573e15
Create Date: 2017-04-05 10:16:52.504699

"""

# revision identifiers, used by Alembic.
revision = '2da192dbbc65'
down_revision = '8fce4c573e15'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
    ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.create_table(
        'ps_outbound_publishes',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('expiration', sa.Integer),
        sa.Column('outbound_auth', sa.String(40)),
        sa.Column('outbound_proxy', sa.String(256)),
        sa.Column('server_uri', sa.String(256)),
        sa.Column('from_uri', sa.String(256)),
        sa.Column('to_uri', sa.String(256)),
        sa.Column('event', sa.String(40)),
        sa.Column('max_auth_attempts', sa.Integer),
        sa.Column('transport', sa.String(40)),
        sa.Column('multi_user', yesno_values),
        sa.Column('@body', sa.String(40)),
        sa.Column('@context', sa.String(256)),
        sa.Column('@exten', sa.String(256)),
    )

    op.create_index('ps_outbound_publishes_id', 'ps_outbound_publishes', ['id'])

    op.create_table(
        'ps_inbound_publications',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('endpoint', sa.String(40)),
        sa.Column('event_asterisk-devicestate', sa.String(40)),
        sa.Column('event_asterisk-mwi', sa.String(40)),
    )

    op.create_index('ps_inbound_publications_id', 'ps_inbound_publications', ['id'])

    op.create_table(
        'ps_asterisk_publications',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
        sa.Column('devicestate_publish', sa.String(40)),
        sa.Column('mailboxstate_publish', sa.String(40)),
        sa.Column('device_state', yesno_values),
        sa.Column('device_state_filter', sa.String(256)),
        sa.Column('mailbox_state', yesno_values),
        sa.Column('mailbox_state_filter', sa.String(256)),
    )

    op.create_index('ps_asterisk_publications_id', 'ps_asterisk_publications', ['id'])

def downgrade():
    op.drop_table('ps_outbound_publishes')
    op.drop_table('ps_inbound_publications')
    op.drop_table('ps_asterisk_publications')
