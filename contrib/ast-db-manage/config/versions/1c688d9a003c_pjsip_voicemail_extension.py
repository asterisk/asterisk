"""pjsip voicemail extension

Revision ID: 1c688d9a003c
Revises: 5813202e92be
Create Date: 2016-03-24 22:31:45.537895

"""

# revision identifiers, used by Alembic.
revision = '1c688d9a003c'
down_revision = '5813202e92be'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('default_voicemail_extension', sa.String(40)))
    op.add_column('ps_aors', sa.Column('voicemail_extension', sa.String(40)))
    op.add_column('ps_endpoints', sa.Column('voicemail_extension', sa.String(40)))
    op.add_column('ps_endpoints', sa.Column('mwi_subscribe_replaces_unsolicited', sa.Integer))


def downgrade():
    op.drop_column('ps_globals', 'default_voicemail_extension')
    op.drop_column('ps_aors', 'voicemail_extension')
    op.drop_column('ps_endpoints', 'voicemail_extension')
    op.drop_column('ps_endpoints', 'mwi_subscribe_replaces_unsolicited')
