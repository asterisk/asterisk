"""pjsip voicemail extension

Revision ID: 1c688d9a003c
Revises: 3bcc0b5bc2c9
Create Date: 2016-03-24 22:31:45.537895

"""

# revision identifiers, used by Alembic.
revision = '1c688d9a003c'
down_revision = '3bcc0b5bc2c9'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('default_voicemail_extension', sa.String(40)))
    op.add_column('ps_aors', sa.Column('voicemail_extension', sa.String(40)))
    op.add_column('ps_endpoints', sa.Column('voicemail_extension', sa.String(40)))
    op.add_column('ps_endpoints', sa.Column('mwi_subscribe_replaces_unsolicited', sa.Integer))


def downgrade():
    with op.batch_alter_table('ps_globals') as batch_op:
        batch_op.drop_column('default_voicemail_extension')
    with op.batch_alter_table('ps_aors') as batch_op:
        batch_op.drop_column('voicemail_extension')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('voicemail_extension')
        batch_op.drop_column('mwi_subscribe_replaces_unsolicited')
