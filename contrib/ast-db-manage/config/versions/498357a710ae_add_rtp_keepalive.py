"""Add RTP keepalive

Revision ID: 498357a710ae
Revises: 28b8e71e541f
Create Date: 2015-07-10 16:42:12.244421

"""

# revision identifiers, used by Alembic.
revision = '498357a710ae'
down_revision = '45e3f47c6c44'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('rtp_keepalive', sa.Integer))


def downgrade():
    op.drop_column('ps_endpoints', 'rtp_keepalive')
