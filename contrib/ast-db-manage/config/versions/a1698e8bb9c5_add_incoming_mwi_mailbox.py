"""add_incoming_mwi_mailbox

Revision ID: a1698e8bb9c5
Revises: b83645976fdd
Create Date: 2017-09-08 13:45:18.937571

"""

# revision identifiers, used by Alembic.
revision = 'a1698e8bb9c5'
down_revision = 'b83645976fdd'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('incoming_mwi_mailbox', sa.String(40)))

def downgrade():
    op.drop_column('ps_endpoints', 'incoming_mwi_mailbox')
