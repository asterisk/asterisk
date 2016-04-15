"""pjsip: add contact reg_server

Revision ID: 81b01a191a46
Revises: 1c688d9a003c
Create Date: 2016-04-15 15:00:35.024525

"""

# revision identifiers, used by Alembic.
revision = '81b01a191a46'
down_revision = '1c688d9a003c'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_contacts', sa.Column('reg_server', sa.String(20)))


def downgrade():
    with op.batch_alter_table('ps_contacts') as batch_op:
        batch_op.drop_column('reg_server')
