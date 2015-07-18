"""add pjsip timeout options

Revision ID: 5a6ccc758633
Revises: 498357a710ae
Create Date: 2015-07-21 07:49:05.060727

"""

# revision identifiers, used by Alembic.
revision = '5a6ccc758633'
down_revision = '498357a710ae'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('rtp_timeout', sa.Integer))
    op.add_column('ps_endpoints', sa.Column('rtp_timeout_hold', sa.Integer))


def downgrade():
    op.drop_column('ps_endpoints', 'rtp_timeout')
    op.drop_column('ps_endpoints', 'rtp_timeout_hold')
